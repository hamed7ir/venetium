// Venetium test kit, page canvas-webgl: the reference model of the "exact" 2D scene (BATCH-X86-2 §1).
// Loaded by canvas-webgl.html with <script src>; also runs under Node (module.exports) to produce data/canvas-webgl-golden.js.
//   scene()          the deterministic list of drawing operations (an LCG with Math.imul: identical in every JS engine and tier)
//   model(ops)       an independent software rasteriser for those operations (integer geometry only) -> RGBA Uint8ClampedArray
//   render(ctx, ops) the same operations drawn through a CanvasRenderingContext2D (or OffscreenCanvasRenderingContext2D)
//   crc32(u8)        CRC-32 (IEEE) of a byte array
// Every operation uses integer coordinates, opaque colours and axis-aligned geometry, so every correct 2D backend (CPU raster,
// GPU raster, any SIMD path) must give the same bytes, and the model predicts them without calling any canvas API.
// Text, anti-aliased shapes, gradients and blurs are NOT in this scene (they are checked with tolerances, or only reported).
(function (root) {
  'use strict';
  const W = 160, H = 120, SEED = 0x13579BDF;

  function rng(seed) {
    let s = seed >>> 0;
    return function (n) { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return (s >>> 8) % n; };
  }

  // The operation list. Kinds: rect, hline, vline, frame, evenodd, clear, clipfill, xform, blit.
  function scene() {
    const r = rng(SEED), ops = [];
    const R = (lo, hi) => lo + r(hi - lo + 1);          // inclusive range
    const col = () => [r(256), r(256), r(256)];
    ops.push({ t: 'rect', x: 0, y: 0, w: W, h: H, c: [16, 32, 48] });
    const MATS = [[1, 0, 0, 1], [2, 0, 0, 3], [0, 1, -1, 0], [0, -1, 1, 0], [-1, 0, 0, -1], [3, 0, 0, 2], [-2, 0, 0, 1]];
    for (let i = 0; i < 330; i++) {
      const k = r(100);
      if (k < 28) {
        ops.push({ t: 'rect', x: R(-10, 150), y: R(-10, 110), w: R(1, 40), h: R(1, 30), c: col() });
      } else if (k < 44) {
        ops.push({ t: 'rect', x: R(0, 158), y: R(0, 118), w: R(1, 3), h: R(1, 3), c: col() });
      } else if (k < 56) {
        const x = R(0, 150), y = R(0, 119);
        ops.push({ t: 'hline', x1: x, x2: x + R(1, 60), y, c: col() });
      } else if (k < 68) {
        const x = R(0, 159), y = R(0, 110);
        ops.push({ t: 'vline', x, y1: y, y2: y + R(1, 50), c: col() });
      } else if (k < 76) {
        ops.push({ t: 'frame', x: R(2, 120), y: R(2, 90), w: R(2, 50), h: R(2, 40), c: col() });
      } else if (k < 82) {
        const x = R(0, 120), y = R(0, 90), w = R(6, 40), h = R(6, 30);
        const ix = x + R(1, w - 3), iy = y + R(1, h - 3);
        ops.push({ t: 'evenodd', x, y, w, h, ix, iy, iw: R(1, x + w - 1 - ix), ih: R(1, y + h - 1 - iy), c: col() });
      } else if (k < 85) {
        ops.push({ t: 'clear', x: R(0, 150), y: R(0, 110), w: R(1, 14), h: R(1, 14) });
      } else if (k < 90) {
        const x = R(0, 140), y = R(0, 100), w = R(3, 20), h = R(3, 20);
        ops.push({ t: 'clipfill', cx: x, cy: y, cw: w, ch: h, x: x - 5, y: y - 5, w: w + 10, h: h + 10, c: col() });
      } else if (k < 96) {
        ops.push({ t: 'xform', m: MATS[r(MATS.length)].concat([R(-20, 150), R(-20, 110)]), x: R(0, 10), y: R(0, 10), w: R(1, 12), h: R(1, 12), c: col() });
      } else {
        const w = R(2, 30), h = R(2, 30);
        ops.push({ t: 'blit', sx: R(0, W - w), sy: R(0, H - h), w, h, dx: R(-5, W - 5), dy: R(-5, H - 5) });
      }
    }
    return ops;
  }

  // ---- the independent software model ----
  function model(ops, w, h) {
    w = w || W; h = h || H;
    const px = new Uint8ClampedArray(w * h * 4);
    function fill(x0, y0, x1, y1, r, g, b, a, clip) {
      if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > w) x1 = w; if (y1 > h) y1 = h;
      if (clip) { if (x0 < clip[0]) x0 = clip[0]; if (y0 < clip[1]) y0 = clip[1]; if (x1 > clip[2]) x1 = clip[2]; if (y1 > clip[3]) y1 = clip[3]; }
      for (let y = y0; y < y1; y++) {
        let i = (y * w + x0) * 4;
        for (let x = x0; x < x1; x++, i += 4) { px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = a; }
      }
    }
    for (const op of ops) {
      const c = op.c || [0, 0, 0];
      switch (op.t) {
        case 'rect': fill(op.x, op.y, op.x + op.w, op.y + op.h, c[0], c[1], c[2], 255); break;
        case 'hline': fill(op.x1, op.y, op.x2, op.y + 1, c[0], c[1], c[2], 255); break;
        case 'vline': fill(op.x, op.y1, op.x + 1, op.y2, c[0], c[1], c[2], 255); break;
        case 'frame': {                                   // lineWidth 2 strokeRect at integer coordinates, mitred corners
          const x = op.x, y = op.y, fw = op.w, fh = op.h;
          fill(x - 1, y - 1, x + fw + 1, y + 1, c[0], c[1], c[2], 255);
          fill(x - 1, y + fh - 1, x + fw + 1, y + fh + 1, c[0], c[1], c[2], 255);
          fill(x - 1, y + 1, x + 1, y + fh - 1, c[0], c[1], c[2], 255);
          fill(x + fw - 1, y + 1, x + fw + 1, y + fh - 1, c[0], c[1], c[2], 255);
          break;
        }
        case 'evenodd': {                                  // outer rect minus the inner rect (even-odd winding)
          const x = op.x, y = op.y, ow = op.w, oh = op.h;
          fill(x, y, x + ow, op.iy, c[0], c[1], c[2], 255);
          fill(x, op.iy + op.ih, x + ow, y + oh, c[0], c[1], c[2], 255);
          fill(x, op.iy, op.ix, op.iy + op.ih, c[0], c[1], c[2], 255);
          fill(op.ix + op.iw, op.iy, x + ow, op.iy + op.ih, c[0], c[1], c[2], 255);
          break;
        }
        case 'clear': fill(op.x, op.y, op.x + op.w, op.y + op.h, 0, 0, 0, 0); break;
        case 'clipfill': fill(op.x, op.y, op.x + op.w, op.y + op.h, c[0], c[1], c[2], 255, [op.cx, op.cy, op.cx + op.cw, op.cy + op.ch]); break;
        case 'xform': {                                    // device rect of the transformed rectangle (axis-aligned matrices only)
          const [a, b, cc, d, e, f] = op.m;
          const xs = [], ys = [];
          for (const [px0, py0] of [[op.x, op.y], [op.x + op.w, op.y], [op.x, op.y + op.h], [op.x + op.w, op.y + op.h]]) { xs.push(a * px0 + cc * py0 + e); ys.push(b * px0 + d * py0 + f); }
          fill(Math.min(...xs), Math.min(...ys), Math.max(...xs), Math.max(...ys), c[0], c[1], c[2], 255);
          break;
        }
        case 'blit': {                                     // drawImage(self, sx, sy, w, h, dx, dy, w, h): source-over, snapshot first
          const snap = new Uint8ClampedArray(op.w * op.h * 4);
          for (let y = 0; y < op.h; y++) for (let x = 0; x < op.w; x++) { const s = ((op.sy + y) * w + op.sx + x) * 4, t = (y * op.w + x) * 4; snap[t] = px[s]; snap[t + 1] = px[s + 1]; snap[t + 2] = px[s + 2]; snap[t + 3] = px[s + 3]; }
          for (let y = 0; y < op.h; y++) {
            const dy = op.dy + y; if (dy < 0 || dy >= h) continue;
            for (let x = 0; x < op.w; x++) {
              const dx = op.dx + x; if (dx < 0 || dx >= w) continue;
              const t = (y * op.w + x) * 4, d = (dy * w + dx) * 4;
              if (snap[t + 3] === 255) { px[d] = snap[t]; px[d + 1] = snap[t + 1]; px[d + 2] = snap[t + 2]; px[d + 3] = 255; }
              // alpha 0 source pixels leave the destination alone (the scene only has alpha 0 or 255)
            }
          }
          break;
        }
        default: throw new Error('model: unknown op ' + op.t);
      }
    }
    return px;
  }

  // ---- the same operations through a 2D context ----
  function render(ctx, ops) {
    const rgb = c => 'rgb(' + c[0] + ',' + c[1] + ',' + c[2] + ')';
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    for (const op of ops) {
      switch (op.t) {
        case 'rect': ctx.fillStyle = rgb(op.c); ctx.fillRect(op.x, op.y, op.w, op.h); break;
        case 'hline':
          ctx.strokeStyle = rgb(op.c); ctx.lineWidth = 1; ctx.lineCap = 'butt';
          ctx.beginPath(); ctx.moveTo(op.x1, op.y + 0.5); ctx.lineTo(op.x2, op.y + 0.5); ctx.stroke(); break;
        case 'vline':
          ctx.strokeStyle = rgb(op.c); ctx.lineWidth = 1; ctx.lineCap = 'butt';
          ctx.beginPath(); ctx.moveTo(op.x + 0.5, op.y1); ctx.lineTo(op.x + 0.5, op.y2); ctx.stroke(); break;
        case 'frame': ctx.strokeStyle = rgb(op.c); ctx.lineWidth = 2; ctx.lineJoin = 'miter'; ctx.strokeRect(op.x, op.y, op.w, op.h); break;
        case 'evenodd':
          ctx.fillStyle = rgb(op.c); ctx.beginPath(); ctx.rect(op.x, op.y, op.w, op.h); ctx.rect(op.ix, op.iy, op.iw, op.ih); ctx.fill('evenodd'); break;
        case 'clear': ctx.clearRect(op.x, op.y, op.w, op.h); break;
        case 'clipfill':
          ctx.save(); ctx.beginPath(); ctx.rect(op.cx, op.cy, op.cw, op.ch); ctx.clip();
          ctx.fillStyle = rgb(op.c); ctx.fillRect(op.x, op.y, op.w, op.h); ctx.restore(); break;
        case 'xform':
          ctx.setTransform(op.m[0], op.m[1], op.m[2], op.m[3], op.m[4], op.m[5]);
          ctx.fillStyle = rgb(op.c); ctx.fillRect(op.x, op.y, op.w, op.h);
          ctx.setTransform(1, 0, 0, 1, 0, 0); break;
        case 'blit': ctx.drawImage(ctx.canvas, op.sx, op.sy, op.w, op.h, op.dx, op.dy, op.w, op.h); break;
        default: throw new Error('render: unknown op ' + op.t);
      }
    }
  }

  let TABLE = null;
  function crc32(u8) {
    if (!TABLE) {
      TABLE = new Uint32Array(256);
      for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1); TABLE[n] = c >>> 0; }
    }
    let c = 0xFFFFFFFF;
    for (let i = 0; i < u8.length; i++) c = TABLE[(c ^ u8[i]) & 255] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
  }
  const hex = n => ('00000000' + (n >>> 0).toString(16)).slice(-8);
  // CRC of a string (the operation list) through its UTF-8-free ASCII JSON
  function crcStr(s) { const u = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i) & 255; return crc32(u); }

  const CW = { W, H, SEED, scene, model, render, crc32, crcStr, hex };
  if (typeof module !== 'undefined' && module.exports) module.exports = CW; else root.CW = CW;
})(typeof window !== 'undefined' ? window : globalThis);
