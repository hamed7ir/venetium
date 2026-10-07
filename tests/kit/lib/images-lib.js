// Venetium test kit, images page: helpers (no V8 natives syntax here - this file loads in every configuration).
// Decode path under test: a blob: URL -> <img> -> canvas drawImage -> getImageData. blob: URLs are same-origin to the document that
// made them, so the canvas stays readable even though the page itself is an opaque file:// origin.
window.ImgLib = (function () {
  'use strict';
  const hex8 = n => ('00000000' + (n >>> 0).toString(16)).slice(-8);
  // FNV-1a 32 over the RGBA bytes (the generators in kit-tools/images/ compute the same value from their own pixel arrays)
  function fnv(d) { let h = 0x811c9dc5; for (let i = 0; i < d.length; i++) { h ^= d[i]; h = Math.imul(h, 0x01000193) >>> 0; } return h >>> 0; }
  // the same for images with anti-aliased alpha edges: only fully opaque pixels keep their colour, the others count (0,0,0,alpha) - the
  // canvas un-premultiplies semi-transparent pixels with rounding, so their colour is decoder- and canvas-dependent, their alpha is not
  function fnvOpaque(d) {
    let h = 0x811c9dc5;
    for (let i = 0; i < d.length; i += 4) {
      const a = d[i + 3], op = a === 255;
      h ^= op ? d[i] : 0; h = Math.imul(h, 0x01000193) >>> 0;
      h ^= op ? d[i + 1] : 0; h = Math.imul(h, 0x01000193) >>> 0;
      h ^= op ? d[i + 2] : 0; h = Math.imul(h, 0x01000193) >>> 0;
      h ^= a; h = Math.imul(h, 0x01000193) >>> 0;
    }
    return h >>> 0;
  }
  function timeout(ms, v) { return new Promise(r => setTimeout(() => r(v), ms)); }

  // load an image from a URL: {ev: 'load' | 'error' | 'timeout', img}
  function load(url, ms) {
    return new Promise(res => {
      const img = new Image(); let done = false;
      const t = setTimeout(() => fin('timeout'), ms);
      function fin(ev) { if (!done) { done = true; clearTimeout(t); res({ ev, img }); } }
      img.onload = () => fin('load'); img.onerror = () => fin('error');
      img.src = url;
    });
  }
  // draw a decoded image (<img> or ImageBitmap) into a canvas and read the pixels back (RGBA, un-premultiplied)
  function pixels(src, w, h) {
    const c = document.createElement('canvas'); c.width = w; c.height = h;
    const x = c.getContext('2d', { willReadFrequently: true });   // software canvas: the read-back is what the decoder produced, not a GPU round trip
    x.drawImage(src, 0, 0);
    return { w, h, data: x.getImageData(0, 0, w, h).data };
  }
  // <img> path: {ev, w, h, px} ; px is null when the image did not load or its size is 0
  async function decodeImg(url, ms) {
    const L = await load(url, ms);
    const r = { ev: L.ev, w: L.img.naturalWidth, h: L.img.naturalHeight, px: null };
    if (L.ev === 'load' && r.w > 0 && r.h > 0 && r.w <= 4096 && r.h <= 4096) r.px = pixels(L.img, r.w, r.h);
    return r;
  }
  // createImageBitmap path (a different route into the same decoders: the decode happens off the main thread): {ev: 'ok' | 'rejected' | 'timeout', ...}
  async function decodeBitmap(blob, ms) {
    try {
      const bm = await Promise.race([createImageBitmap(blob), timeout(ms, 'timeout')]);
      if (bm === 'timeout') return { ev: 'timeout', px: null };
      const r = { ev: 'ok', w: bm.width, h: bm.height, px: bm.width > 0 && bm.height > 0 ? pixels(bm, bm.width, bm.height) : null };
      bm.close(); return r;
    } catch (e) { return { ev: 'rejected', err: e.name, px: null }; }
  }
  // mean colour of a gx x gy grid, each pixel composited on white (so alpha edges and the canvas' un-premultiply rounding do not matter)
  function grid(px, gx, gy) {
    const out = [];
    for (let j = 0; j < gy; j++) {
      const y0 = Math.floor(j * px.h / gy), y1 = Math.floor((j + 1) * px.h / gy);
      for (let i = 0; i < gx; i++) {
        const x0 = Math.floor(i * px.w / gx), x1 = Math.floor((i + 1) * px.w / gx);
        const s = [0, 0, 0]; let n = 0;
        for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) {
          const o = (y * px.w + x) * 4, a = px.data[o + 3];
          for (let k = 0; k < 3; k++) s[k] += (px.data[o + k] * a + 255 * (255 - a)) / 255;
          n++;
        }
        out.push(s.map(v => Math.floor(v / n + 0.5)));
      }
    }
    return out;
  }
  function gridDiff(a, b) { let m = 0; for (let i = 0; i < a.length; i++) for (let k = 0; k < 3; k++) m = Math.max(m, Math.abs(a[i][k] - b[i][k])); return m; }
  // per row: S = identical to the valid sibling's row, T = identical and the sibling's row is empty too (trivially so: a transparent row),
  // E = empty (every pixel transparent) while the sibling's is not, O = anything else
  function emptyRows(base) {
    if (base.er) return base.er;
    const er = [];
    for (let y = 0; y < base.h; y++) { let e = true; for (let o = y * base.w * 4 + 3, end = (y + 1) * base.w * 4; o < end; o += 4) if (base.data[o]) { e = false; break; } er.push(e); }
    return (base.er = er);
  }
  function rowKinds(px, base) {
    const er = emptyRows(base); let k = '';
    for (let y = 0; y < px.h; y++) {
      let same = true, empty = true;
      const o0 = y * px.w * 4, o1 = o0 + px.w * 4;
      for (let o = o0; o < o1; o += 4) {
        if (same && (px.data[o] !== base.data[o] || px.data[o + 1] !== base.data[o + 1] || px.data[o + 2] !== base.data[o + 2] || px.data[o + 3] !== base.data[o + 3])) same = false;
        if (empty && px.data[o + 3] !== 0) empty = false;
        if (!same && !empty) break;
      }
      k += same ? (er[y] ? 'T' : 'S') : empty ? 'E' : 'O';
    }
    return k;
  }
  const runs = k => k.replace(/(.)\1*/g, (m, c) => c + m.length);   // 'SSSSEEE' -> 'S4E3'
  const count = (k, c) => k.split(c).length - 1;
  // The outcome class of one decode attempt of a broken file, against its valid sibling `base` (decoded px) or null:
  //   error    no image (onerror / createImageBitmap rejected)
  //   empty    an image of the right size whose pixels are all transparent (the header was fine, the decode failed)
  //   partial  some rows are exactly right, some are empty, and at most 24 boundary rows are in between: decoded as far as the data went
  //            (top-down for most formats, bottom-up for BMP / ICO)
  //   degraded the whole image is there but not identical, and still coarsely the same picture (a progressive JPEG stopped after some scans)
  //   corrupt  pixels that are neither (wrong content)
  //   complete identical to the valid sibling (the damage was not noticed)
  //   timeout / resized / content (no sibling given: pixels present)
  function classify(r, base, baseGrid) {
    if (r.ev === 'timeout') return { cls: 'timeout' };
    if (r.ev === 'error' || r.ev === 'rejected') return { cls: 'error' };
    const px = r.px;
    if (!px) return { cls: 'error', note: 'no pixels (size ' + r.w + 'x' + r.h + ')' };
    if (!base) {
      let any = false; for (let o = 3; o < px.data.length; o += 4) if (px.data[o]) { any = true; break; }
      return { cls: any ? 'content' : 'empty', note: px.w + 'x' + px.h };
    }
    if (px.w !== base.w || px.h !== base.h) return { cls: 'resized', note: px.w + 'x' + px.h + ' vs ' + base.w + 'x' + base.h };
    const k = rowKinds(px, base);
    const note = px.w + 'x' + px.h + ' rows ' + runs(k);
    const nS = count(k, 'S'), nE = count(k, 'E'), nO = count(k, 'O');
    if (nE === 0 && nO === 0) return { cls: 'complete', note };
    if (nS === 0 && nO === 0) return { cls: 'empty', note };
    if (nS >= 1 && nE >= 1 && nO <= 24) return { cls: 'partial', note, rowsDecoded: nS + nO };
    if (nE === 0 && baseGrid && gridDiff(grid(px, 8, 8), baseGrid) <= 40) return { cls: 'degraded', note };
    return { cls: 'corrupt', note };
  }
  return { hex8, fnv, fnvOpaque, load, pixels, decodeImg, decodeBitmap, grid, gridDiff, rowKinds, runs, classify, timeout };
})();
