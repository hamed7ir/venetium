// Venetium test kit — the shared GPU/compositor torture scene (BATCH-GPU-1 §5/§7). Built once, driven by a requestAnimationFrame
// meter. Used by stall/stall.html (60 s hang meter), video/video.html (as the moving backdrop) and the §5 WARP runs. No build
// step, no network: everything is inline or a blob: URL from the kit's embedded clips (lib/kit.js Kit.blobUrl / data/media-*.js).
//
//   GPUScene.build(root, {video}) -> creates the composited layers inside `root`:
//       many rounded, blurred, drop-shadowed, gradient-filled layers with opacity+transform CSS animations; a 2-D canvas redrawn
//       every frame; a minimal WebGL 1 scene (a spinning, shaded, multi-varying triangle) redrawn every frame; and, when
//       {video:true}, a looping <video> layer the caller fills. Returns a handle {webgl, draw(), video}.
//   GPUScene.meter({seconds, onFrame, onDone}) -> runs rAF for `seconds`, calls draw() each frame, records every inter-frame gap
//       over 100 ms, returns via onDone: {seconds, frames, fps, longestGapMs, gapsOver100, gaps:[{tMs,gapMs}], p50Ms, p95Ms}.
//   GPUScene.webglReport() -> { ok, renderer, vendor, maxVaryingVectors, maxFragmentUniformVectors, varyingsUsed, drewPixel, error }
//       the UNMASKED renderer string is the proof of which ANGLE backend/feature level is live (e.g. "... Direct3D11 ... level_9_3"
//       or "... Direct3D9 ... ps_2_a"); maxVaryingVectors shows whether this context hit the 7-varying case that patch 0035 relaxes.
(function () {
  'use strict';
  const NS = {};

  // ---- minimal WebGL 1 scene: a triangle whose fragment colour comes through several varyings (so the context must support
  // them); readPixels proves it actually drew. One context is created and reused for webglReport() and for per-frame draws.
  const VSRC =
    'attribute vec2 aPos;\n' +
    'uniform float uT;\n' +
    'varying vec4 vA; varying vec4 vB; varying vec2 vC;\n' +   // 2.5 vec4-equivalents of varyings; D3D9 SM2 exposes 7/8
    'void main(){\n' +
    '  float c = cos(uT), s = sin(uT);\n' +
    '  mat2 r = mat2(c,-s,s,c);\n' +
    '  vec2 p = r * aPos;\n' +
    '  vA = vec4(aPos*0.5+0.5, 0.5+0.5*c, 1.0);\n' +
    '  vB = vec4(0.5+0.5*s, aPos.y*0.5+0.5, 0.3, 1.0);\n' +
    '  vC = aPos*0.5+0.5;\n' +
    '  gl_Position = vec4(p, 0.0, 1.0);\n' +
    '}\n';
  const FSRC =
    'precision mediump float;\n' +
    'varying vec4 vA; varying vec4 vB; varying vec2 vC;\n' +
    'void main(){\n' +
    '  vec3 col = vA.rgb*0.5 + vB.rgb*0.4 + vec3(vC, 0.2)*0.3;\n' +
    '  gl_FragColor = vec4(col, 1.0);\n' +
    '}\n';

  function makeWebGL(canvas) {
    const gl = canvas.getContext('webgl', { antialias: false, preserveDrawingBuffer: true }) ||
               canvas.getContext('experimental-webgl', { antialias: false, preserveDrawingBuffer: true });
    if (!gl) return { gl: null, error: 'no webgl context' };
    function sh(type, src) {
      const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error('shader: ' + gl.getShaderInfoLog(s));
      return s;
    }
    let prog;
    try {
      prog = gl.createProgram();
      gl.attachShader(prog, sh(gl.VERTEX_SHADER, VSRC));
      gl.attachShader(prog, sh(gl.FRAGMENT_SHADER, FSRC));
      gl.linkProgram(prog);
      if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error('link: ' + gl.getProgramInfoLog(prog));
    } catch (e) { return { gl, error: String(e.message || e) }; }
    const buf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0.8, -0.8, -0.6, 0.8, -0.6]), gl.STATIC_DRAW);
    const aPos = gl.getAttribLocation(prog, 'aPos');
    const uT = gl.getUniformLocation(prog, 'uT');
    gl.useProgram(prog);
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);
    return { gl, prog, uT, draw(t) {
      gl.viewport(0, 0, canvas.width, canvas.height);
      gl.clearColor(0.06, 0.07, 0.10, 1); gl.clear(gl.COLOR_BUFFER_BIT);
      gl.uniform1f(uT, t); gl.drawArrays(gl.TRIANGLES, 0, 3);
    } };
  }

  NS.webglReport = function () {
    const r = { ok: false, renderer: '', vendor: '', maxVaryingVectors: null, maxFragmentUniformVectors: null, varyingsUsed: 3, drewPixel: false, error: '' };
    try {
      const c = document.createElement('canvas'); c.width = 64; c.height = 64;
      const w = makeWebGL(c);
      if (!w.gl) { r.error = w.error; return r; }
      const gl = w.gl;
      const dbg = gl.getExtension('WEBGL_debug_renderer_info');
      r.renderer = dbg ? String(gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL)) : String(gl.getParameter(gl.RENDERER));
      r.vendor = dbg ? String(gl.getParameter(dbg.UNMASKED_VENDOR_WEBGL)) : String(gl.getParameter(gl.VENDOR));
      r.maxVaryingVectors = gl.getParameter(gl.MAX_VARYING_VECTORS);
      r.maxFragmentUniformVectors = gl.getParameter(gl.MAX_FRAGMENT_UNIFORM_VECTORS);
      if (w.error) { r.error = w.error; return r; }
      w.draw(0.7);
      const px = new Uint8Array(4); gl.readPixels(20, 20, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
      r.drewPixel = px[0] + px[1] + px[2] > 0;
      r.ok = r.drewPixel;
    } catch (e) { r.error = String(e.message || e); }
    return r;
  };

  NS.build = function (root, opts) {
    opts = opts || {};
    const wrap = document.createElement('div');
    wrap.style.cssText = 'position:relative;width:100%;height:420px;overflow:hidden;background:#101317;border-radius:12px';
    // composited, animated, filtered layers
    for (let i = 0; i < 16; i++) {
      const d = document.createElement('div');
      const hue = (i * 23) % 360;
      d.style.cssText =
        'position:absolute;width:140px;height:96px;border-radius:18px;' +
        'left:' + (6 + (i % 8) * 120) + 'px;top:' + (10 + Math.floor(i / 8) * 150) + 'px;' +
        'background:linear-gradient(135deg,hsl(' + hue + ',70%,55%),hsl(' + ((hue + 60) % 360) + ',70%,45%));' +
        'box-shadow:0 10px 24px rgba(0,0,0,.5);filter:drop-shadow(0 4px 6px rgba(0,0,0,.4));' +
        'opacity:.85;will-change:transform,opacity;' +
        'animation:gpuspin ' + (3 + (i % 5)) + 's ease-in-out ' + (i * 0.1) + 's infinite alternate;';
      wrap.appendChild(d);
    }
    // a blurred, semi-transparent overlay to force backdrop compositing
    const blur = document.createElement('div');
    blur.style.cssText = 'position:absolute;inset:40px 30px auto 30px;height:140px;border-radius:16px;' +
      'backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);background:rgba(255,255,255,.06);' +
      'box-shadow:inset 0 0 40px rgba(255,255,255,.08);animation:gpufade 4s ease-in-out infinite alternate;';
    wrap.appendChild(blur);
    // a 2-D canvas redrawn every frame
    const c2d = document.createElement('canvas'); c2d.width = 360; c2d.height = 200;
    c2d.style.cssText = 'position:absolute;right:16px;bottom:12px;width:360px;height:200px;border-radius:10px;opacity:.9';
    wrap.appendChild(c2d);
    const ctx = c2d.getContext('2d');
    // a WebGL 1 canvas redrawn every frame
    const glc = document.createElement('canvas'); glc.width = 320; glc.height = 200;
    glc.style.cssText = 'position:absolute;left:16px;bottom:12px;width:320px;height:200px;border-radius:10px';
    wrap.appendChild(glc);
    const w = makeWebGL(glc);
    // optional looping video layer the caller fills
    let video = null;
    if (opts.video) {
      video = document.createElement('video');
      video.loop = true; video.muted = true; video.playsInline = true; video.autoplay = true;
      video.style.cssText = 'position:absolute;left:50%;top:8px;transform:translateX(-50%);width:300px;height:170px;border-radius:10px;object-fit:cover;background:#000';
      wrap.appendChild(video);
    }
    const style = document.createElement('style');
    style.textContent =
      '@keyframes gpuspin{from{transform:translateY(0) rotate(0) scale(1)}to{transform:translateY(16px) rotate(8deg) scale(1.06)}}' +
      '@keyframes gpufade{from{opacity:.25}to{opacity:.75}}';
    document.head.appendChild(style);
    root.appendChild(wrap);

    function draw(tSec) {
      // canvas 2-D: moving gradient + shapes
      const W = c2d.width, H = c2d.height;
      const g = ctx.createLinearGradient(0, 0, W, H);
      g.addColorStop(0, 'hsl(' + ((tSec * 40) % 360) + ',80%,55%)');
      g.addColorStop(1, 'hsl(' + ((tSec * 40 + 120) % 360) + ',80%,40%)');
      ctx.fillStyle = g; ctx.fillRect(0, 0, W, H);
      ctx.fillStyle = 'rgba(255,255,255,.85)';
      for (let i = 0; i < 24; i++) {
        const a = tSec + i * 0.5;
        ctx.beginPath();
        ctx.arc(W / 2 + Math.cos(a) * (40 + i * 4), H / 2 + Math.sin(a * 1.3) * (30 + i * 3), 5, 0, 7);
        ctx.fill();
      }
      if (w && w.draw) { try { w.draw(tSec); } catch (e) {} }
    }
    return { wrap, webgl: w, draw, video, canvas2d: c2d, glcanvas: glc };
  };

  NS.meter = function (opts) {
    const seconds = opts.seconds || 60;
    const handle = opts.handle;
    return new Promise(resolve => {
      const gaps = [];
      const frameMs = [];
      let frames = 0, last = null, t0 = null, longest = 0;
      function tick(now) {
        if (t0 === null) { t0 = now; last = now; requestAnimationFrame(tick); return; }
        const dt = now - last; last = now; frames++; frameMs.push(dt);
        if (dt > 100) { gaps.push({ tMs: Math.round(now - t0), gapMs: Math.round(dt) }); if (dt > longest) longest = dt; }
        if (handle && handle.draw) handle.draw((now - t0) / 1000);
        if (opts.onFrame) opts.onFrame((now - t0) / 1000, dt);
        if (now - t0 < seconds * 1000) requestAnimationFrame(tick);
        else {
          const sorted = frameMs.slice().sort((a, b) => a - b);
          const pct = p => sorted.length ? Math.round(sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * p))]) : 0;
          resolve({
            seconds, frames, fps: +(frames / ((now - t0) / 1000)).toFixed(1),
            longestGapMs: Math.round(longest), gapsOver100: gaps.length, gaps,
            p50Ms: pct(0.5), p95Ms: pct(0.95),
          });
        }
      }
      requestAnimationFrame(tick);
    });
  };

  window.GPUScene = NS;
})();
