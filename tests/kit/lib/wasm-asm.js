// Venetium test kit - wasm.html helper: the module of s10/jit/wasm.js, assembled byte by byte exactly as that script does (the body
// of s10Bytes() is lines 3-13 of s10/jit/wasm.js, unchanged except for the wrapper), so the page can prove that the base64 bytes in
// data/wasm-data.js are the very module the V8SIM-2 comparison ran. Also: withNonce(), which appends a custom section so that a
// copy of the module is a different module to V8's compilation cache (a fresh NativeModule that has not been tiered up yet).
(function (g) {
  'use strict';
  function s10Bytes() {
    function leb(n) { var out = []; do { var b = n & 0x7f; n >>>= 7; if (n) b |= 0x80; out.push(b); } while (n); return out; }
    function section(id, bytes) { return [id].concat(leb(bytes.length), bytes); }
    function str(s) { var b = leb(s.length); for (var i = 0; i < s.length; i++) b.push(s.charCodeAt(i)); return b; }
    var I32 = 0x7f, I64 = 0x7e;
    var types = [2, 0x60, 2, I32, I32, 1, I32, 0x60, 2, I64, I64, 1, I64];
    var funcs = [6, 0, 0, 0, 1, 1, 1];
    var names = ['add32', 'mul32', 'div32', 'add64', 'mul64', 'div64'];
    var ops = [0x6a, 0x6c, 0x6d, 0x7c, 0x7e, 0x7f];   // i32.add i32.mul i32.div_s i64.add i64.mul i64.div_s
    var exps = [6]; names.forEach(function (n, i) { exps = exps.concat(str(n), [0, i]); });
    var code = [6]; ops.forEach(function (op) { var body = [0, 0x20, 0, 0x20, 1, op, 0x0b]; code = code.concat(leb(body.length), body); });
    var bytes = [0, 0x61, 0x73, 0x6d, 1, 0, 0, 0].concat(section(1, types), section(3, funcs), section(7, exps), section(10, code));
    return new Uint8Array(bytes);
  }
  // module bytes + custom section "kit-nonce" holding `tag`: same code, different bytes
  function withNonce(u8, tag) {
    var name = 'kit-nonce', payload = [], i;
    for (i = 0; i < name.length; i++) payload.push(name.charCodeAt(i));
    payload.unshift(name.length);
    for (i = 0; i < tag.length; i++) payload.push(tag.charCodeAt(i) & 0x7f);
    var size = payload.length, lebSize = [];
    do { var b = size & 0x7f; size >>>= 7; if (size) b |= 0x80; lebSize.push(b); } while (size);
    var extra = [0].concat(lebSize, payload);
    var out = new Uint8Array(u8.length + extra.length);
    out.set(u8, 0); out.set(extra, u8.length);
    return out;
  }
  g.KitWasmAsm = { s10Bytes: s10Bytes, withNonce: withNonce };
})(typeof window !== 'undefined' ? window : globalThis);
