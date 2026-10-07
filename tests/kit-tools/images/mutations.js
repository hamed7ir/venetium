// Page-level mutations of the images page (development tool, not shipped; BATCH-DEVICE-1). Each variant damages a SCRATCH COPY of the
// kit so that one named check of images.html must change its verdict, which proves that check can fail (or, for 'relabel-*',
// 'valid-as-broken' and 'gallery-garbage', documents what the page deliberately does NOT see).
//   node mutations.js <variant> <scratch kit folder>
// The folder must be a full copy of the kit (copy tests/kit and tests/runner into a scratch tests/ folder, then run the COPY's runner:
//   node <scratch>/tests/runner/run.js --config default --pages images --out <run folder>). It is refused if it is the record's own
// kit (../../kit of this script). images.html, lib/images-lib.js and data/images-*.js of the copy are restored from the record before
// every mutation, so variants never compound. Expected verdict of the page on the copy, as of BATCH-DEVICE-1:
//   control                 PASS
//   relabel-suspension      PASS  (the JPEG error-exit info row now lists 14 files; no verdict depends on the phase labels)
//   relabel-all             PASS  (the info row lists 0 files)
//   valid-as-broken         PASS  (six no-base files replaced by valid pictures: accepted, and their rows are named SURVIVES, not 'fails cleanly')
//   gallery-garbage         PASS  (route 3 shows a damaged picture of known size: the page does not read what was painted - disclosed)
//   wheel-garbage-corrupt   FAIL  2 bad (webp-corrupt-vp8l-codes gives same-size wrong pixels = class corrupt on both routes, no longer allowed)
//   avif-garbage-corrupt    FAIL  2 bad (avif-bad-obu gives a same-size wrong picture on both routes, no longer allowed)
//   valid-wrong             FAIL  11 bad (valid-file checks fail for a truncated / swapped / damaged file)
//   x86-ref-wrong           FAIL  1 bad (the REGRESSION row of jpeg-gray-islow, an opaque file: still judged)
//   x86-ref-wrong-gif       FAIL  1 bad (the REGRESSION row of wheel-gif: semi = 0, still judged)
//   semi-x86-wrong          PASS  (BATCH-DEVICE-1 follow-up: the x86 checksums of wheel-png, wheel-webp and wheel-avif are wrong; their rows are info and say 'different')
//   sim-x86-unpremul        PASS  (control for the next one: the canvas un-premultiply re-done with Skia's float chain and x86 tie rounding; the three info rows say 'same')
//   sim-armv7-unpremul      PASS  (same, with ARMv7 NEON tie rounding (v + 0.5 truncated): wheel-png and wheel-avif read back another checksum, the info rows say 'different', no bad row)
//   sim-armv7-no-semi-flag  FAIL  2 bad (the ARMv7 simulation with the semi field removed from the data: the REGRESSION rows of wheel-png and wheel-avif are judged again and fail)
//   heartbeat-dead          PASS  (setInterval does nothing: the heartbeat row is an info row, '0 new ticks'; it used to be a check and would have failed)
//   gallery-error-valid     FAIL  2 bad (a valid file shown as a header-truncated picture: img.decode() rejects and naturalWidth is 0)
//   decode-reject-valid     FAIL  1 bad (img.decode() of the first valid file rejects; the older size row still passes)
//   decode-hang-broken      FAIL  1 bad (img.decode() of one broken file never answers: NO HANG row, after 20 s)
//   unstable-survives       FAIL  1 bad (gif-bad-code gives another size on its 3rd attempt: the SURVIVES row for <img> fails as UNSTABLE)
//   unstable-clean          FAIL  1 bad (jpeg-trunc-scan gives another size on its 3rd attempt: UNSTABLE)
//   bitmap-hang-survives    FAIL  1 bad (createImageBitmap of gif-bad-code never answers once: timeout after 10 s, the SURVIVES row for createImageBitmap)
'use strict';
const fs = require('fs'), path = require('path'), zlib = require('zlib');
const REAL = path.resolve(__dirname, '..', '..', 'kit');
const [variant, dst] = process.argv.slice(2);
if (!variant || !dst) { console.error('usage: node mutations.js <variant> <scratch kit folder>'); process.exit(2); }
const D = path.resolve(dst);
if (!fs.existsSync(path.join(D, 'images.html'))) { console.error('not a kit copy: ' + D); process.exit(2); }
if (fs.realpathSync(D).toLowerCase() === fs.realpathSync(REAL).toLowerCase()) { console.error('refusing to touch the record\'s own kit'); process.exit(2); }
for (const f of ['images.html', 'lib/images-lib.js', 'data/images-valid.js', 'data/images-broken.js']) fs.copyFileSync(path.join(REAL, f), path.join(D, f));
let h = fs.readFileSync(path.join(D, 'images.html'), 'utf8');
let valid = fs.readFileSync(path.join(D, 'data/images-valid.js'), 'utf8');
let lib = fs.readFileSync(path.join(D, 'lib/images-lib.js'), 'utf8');
const AFTER = '<script src="data/images-broken.js"></script>\n';
function inject(code) { if (!h.includes(AFTER)) throw new Error('anchor missing'); h = h.replace(AFTER, () => AFTER + '<script>\n' + code + '\n</script>\n'); }
function rep(s, a, b) { if (!s.includes(a)) throw new Error('anchor not found: ' + a.slice(0, 70)); return s.replace(a, () => b); }
function flatPng(w, h2, rgb) {   // a w x h2 opaque single-colour PNG
  const crcT = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; crcT[n] = c >>> 0; }
  const crc = b => { let c = 0xffffffff; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (t, d) => { const len = Buffer.alloc(4); len.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([len, td, c]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h2, 4); ihdr[8] = 8; ihdr[9] = 2;
  const row = Buffer.concat([Buffer.from([0]), Buffer.from(Array.from({ length: w }, () => rgb).flat())]);
  const raw = Buffer.concat(Array.from({ length: h2 }, () => row));
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}
// code run before the page: tag every blob URL with its size so that a patched ImgLib.decodeImg / createImageBitmap can recognise one file
const SIZES = `const __sizes = new Map(); const __oc = URL.createObjectURL.bind(URL); URL.createObjectURL = b => { const u = __oc(b); __sizes.set(u, b.size); return u; };
const __len = id => atob(IMG_BROKEN.find(b => b.id === id).b64).length;
if (IMG_BROKEN.filter(b => __len(b.id) === __len('%ID%')).length !== 1) throw new Error('target size is not unique');`;
switch (variant) {
  case 'control': break;
  case 'relabel-suspension': inject("IMG_BROKEN.find(b => b.id === 'jpeg-trunc-scan-early').phase = 'scan (error_exit)';"); break;
  case 'relabel-all': inject("for (const b of IMG_BROKEN) b.phase = b.phase.replace('error_exit', 'x');"); break;
  case 'valid-as-broken':
    inject(`const g = IMG_VALID.find(x => x.id === 'gif-static'), p = IMG_VALID.find(x => x.id === 'png-rgb8');
for (const id of ['gif-bad-initial-code', 'gif-bad-code', 'gif-broken']) IMG_BROKEN.find(b => b.id === id).b64 = g.b64;
for (const id of ['png-apng-with-malformed-2nd-frame', 'png-actl-num-frames-0', 'png-plte-weirdness']) IMG_BROKEN.find(b => b.id === id).b64 = p.b64;`);
    break;
  case 'gallery-garbage':   // route 3 shows a DAMAGED jpeg (size known) in place of jpeg-baseline-420 and a damaged png in place of png-rgb8
    h = rep(h, "const t = tile(Kit.blobUrl(v.b64, v.type), v.id + (v.exact ? '' : ' ~'), validOk[v.id]);",
      "const gb64 = v.id === 'jpeg-baseline-420' ? IMG_BROKEN.find(b => b.id === 'jpeg-badhuff-kraft').b64 : v.id === 'png-rgb8' ? IMG_BROKEN.find(b => b.id === 'png-crc-idat-first').b64 : v.b64;\n      const t = tile(Kit.blobUrl(gb64, v.type), v.id + (v.exact ? '' : ' ~'), validOk[v.id]);");
    break;
  case 'gallery-error-valid':   // route 3 shows a header-truncated jpeg (onerror) in place of jpeg-baseline-420
    h = rep(h, "const t = tile(Kit.blobUrl(v.b64, v.type), v.id + (v.exact ? '' : ' ~'), validOk[v.id]);",
      "const gb64 = v.id === 'jpeg-baseline-420' ? IMG_BROKEN.find(b => b.id === 'jpeg-trunc-header').b64 : v.b64;\n      const t = tile(Kit.blobUrl(gb64, v.type), v.id + (v.exact ? '' : ' ~'), validOk[v.id]);");
    break;
  case 'wheel-garbage-corrupt':   // a same-size (128x128) wrong picture, the lossy webp test image, in place of webp-corrupt-vp8l-codes
    inject("IMG_BROKEN.find(b => b.id === 'webp-corrupt-vp8l-codes').b64 = IMG_VALID.find(x => x.id === 'webp-lossy').b64;");
    break;
  case 'avif-garbage-corrupt':    // a flat magenta 64x64 picture (same size as avif-8bit, wrong pixels) in place of avif-bad-obu
    inject("IMG_BROKEN.find(b => b.id === 'avif-bad-obu').b64 = '" + flatPng(64, 64, [255, 0, 255]).toString('base64') + "'; IMG_BROKEN.find(b => b.id === 'avif-bad-obu').type = 'image/png';");
    break;
  case 'valid-wrong':   // a truncated jpeg stands in for jpeg-baseline-420, png-rgb8's bytes for png-rgba8, a bad-Huffman jpeg for jpeg-444-islow
    inject(`IMG_VALID.find(x => x.id === 'jpeg-baseline-420').b64 = IMG_BROKEN.find(b => b.id === 'jpeg-trunc-scan').b64;
IMG_VALID.find(x => x.id === 'png-rgba8').b64 = IMG_VALID.find(x => x.id === 'png-rgb8').b64;
IMG_VALID.find(x => x.id === 'jpeg-444-islow').b64 = IMG_BROKEN.find(b => b.id === 'jpeg-badhuff-kraft').b64;`);
    break;
  case 'x86-ref-wrong': {
    const v2 = rep(valid, '"x86":"050c1b05"', '"x86":"050c1b06"'); fs.writeFileSync(path.join(D, 'data/images-valid.js'), v2); break;
  }
  case 'x86-ref-wrong-gif': {   // wheel-gif has alpha 0 or 255 only (semi 0): its REGRESSION row is still a check
    const v2 = rep(valid, '"x86":"5da87330"', '"x86":"5da87331"'); fs.writeFileSync(path.join(D, 'data/images-valid.js'), v2); break;
  }
  case 'semi-x86-wrong': {      // the three pictures with semi-transparent pixels: wrong x86 checksums change nothing but the 'same' / 'different' word in the info rows
    let v2 = rep(valid, '"x86":"def8a2af"', '"x86":"def8a2ae"'); v2 = rep(v2, '"x86":"5e1ade54"', '"x86":"5e1ade55"'); v2 = rep(v2, '"x86":"dd41dfd2"', '"x86":"dd41dfd3"');
    fs.writeFileSync(path.join(D, 'data/images-valid.js'), v2); break;
  }
  case 'sim-x86-unpremul': case 'sim-armv7-unpremul': case 'sim-armv7-no-semi-flag': {
    // The canvas read-back of a semi-transparent pixel is un-premultiplied by Skia: float chain, then round. x86 rounds ties to even (_mm_cvtps_epi32),
    // ARMv7 NEON truncates v + 0.5 (vcvtq_u32_f32), Skia SkSwizzler_opts.inc pixel_round_as_RP. This variant re-does that step in the page's own read-back
    // (ImgLib.pixels), from the premultiplied value recovered from the x86 read-back, with the chosen tie rule. Model validated on x86: the control gives the
    // recorded x86 checksum of wheel-png (def8a2af); with ARMv7 rounding the checksum of wheel-png and wheel-avif changes. No ARM hardware is involved.
    const mode = variant === 'sim-x86-unpremul' ? 'x86' : 'arm';
    const sim = `
  const K = Math.fround(1 / 255);
  function sim(d) {
    for (let i = 0; i < d.length; i += 4) {
      const a = d[i + 3]; if (a === 0 || a === 255) continue;
      const nA = Math.fround(a * K), rA = Math.fround(1 / nA);
      for (let k = 0; k < 3; k++) {
        const P = Math.floor((d[i + k] * a + 127) / 255);
        const nC = Math.fround(P * K);
        const ans = Math.min(255, Math.fround(Math.fround(nC * rA) * 255));
        let o;
        if ('${mode}' === 'arm') o = Math.floor(Math.fround(ans + 0.5));
        else { const f = Math.floor(ans), r = ans - f; o = r < 0.5 ? f : r > 0.5 ? f + 1 : (f % 2 === 0 ? f : f + 1); }
        d[i + k] = o;
      }
    }
    return d;
  }
`;
    lib = rep(lib, "  function pixels(src, w, h) {", sim + "  function pixels(src, w, h) {");
    lib = rep(lib, "return { w, h, data: x.getImageData(0, 0, w, h).data };", "return { w, h, data: sim(x.getImageData(0, 0, w, h).data) };");
    if (variant === 'sim-armv7-no-semi-flag') { const n = valid.split('"semi":2763').length - 1; if (n !== 3) throw new Error('expected 3 semi flags, found ' + n); fs.writeFileSync(path.join(D, 'data/images-valid.js'), valid.split('"semi":2763').join('"semi":null')); }
    break;
  }
  case 'heartbeat-dead':        // setInterval never fires: the heartbeat counts 0 ticks
    inject('window.setInterval = function () { return 0; };');
    break;
  case 'decode-reject-valid':   // the 1st img.decode() call of the page is the first valid file (png-rgb8): reject it
    inject("{ const od = HTMLImageElement.prototype.decode; let n = 0; HTMLImageElement.prototype.decode = function () { return ++n === 1 ? Promise.reject(new DOMException('mutation', 'EncodingError')) : od.call(this); }; }");
    break;
  case 'decode-hang-broken':    // the 60th call is a broken file (53 valid files come first): never answer
    inject("{ const od = HTMLImageElement.prototype.decode; let n = 0; HTMLImageElement.prototype.decode = function () { return ++n === 60 ? new Promise(() => {}) : od.call(this); }; }");
    break;
  case 'unstable-survives': case 'unstable-clean': {
    const id = variant === 'unstable-survives' ? 'gif-bad-code' : 'jpeg-trunc-scan';
    inject(SIZES.replace(/%ID%/g, id) + `\n{ const L = ImgLib, od = L.decodeImg; const T = __len('${id}'); let k = 0; L.decodeImg = async (u, ms) => { const r = await od(u, ms); if (__sizes.get(u) === T && ++k === 3) r.w += 1; return r; }; }`);
    break;
  }
  case 'bitmap-hang-survives':
    inject(SIZES.replace(/%ID%/g, 'gif-bad-code') + "\n{ const ocb = window.createImageBitmap.bind(window), T = __len('gif-bad-code'); let k = 0; window.createImageBitmap = (b, ...a) => (b.size === T && ++k === 1) ? new Promise(() => {}) : ocb(b, ...a); }");
    break;
  default: console.error('unknown variant ' + variant); process.exit(2);
}
fs.writeFileSync(path.join(D, 'images.html'), h);
fs.writeFileSync(path.join(D, 'lib/images-lib.js'), lib);
console.log('wrote variant', variant, 'into', D);
