// Host cross-check for wasm.html's extra cases: runs the kit's own lib/wasm-cases.js + data/wasm-data.js in Node (V8 on x64)
// and compares every answer with the python answers embedded in the data file. Writes node-answers.json (merged by gen_wasm_data.py).
const fs = require('fs');
const KIT = require('path').resolve(__dirname, '..', '..', 'kit');   // venetium/tests/kit, relative to this file
global.window = globalThis;
require(KIT + '/data/wasm-data.js'); require(KIT + '/lib/wasm-asm.js'); require(KIT + '/lib/wasm-cases.js');
const D = KIT_WASM, C = KitWasmCases;
let bad = 0;
const say = (ok, m) => { if (!ok) bad++; console.log((ok ? 'ok   ' : 'FAIL ') + m); };
// s10 module: assembled == embedded
const asm = KitWasmAsm.s10Bytes(), emb = C.bytes(D.s10.b64);
say(asm.length === emb.length && asm.every((b, i) => b === emb[i]), 's10 assembler == embedded bytes (' + emb.length + ')');
const e1 = new WebAssembly.Instance(new WebAssembly.Module(emb)).exports;
const r32 = C.reps(() => C.caseI32(e1)), r64 = C.reps(() => C.caseI64(e1));
say(r32.every(v => v === D.s10.host_python.wasm_i32) , 'wasm_i32 x4 = ' + r32[3]);
say(r64.every(v => v === D.s10.host_python.wasm_i64), 'wasm_i64 x4 = ' + r64[3]);
// ext
const e2 = new WebAssembly.Instance(new WebAssembly.Module(C.bytes(D.ext.b64))).exports;
const dg = C.digestOps(e2, D);
let nd = 0; for (const o of D.ext.ops) if (dg[o.fn] !== D.ext.host_python[o.fn]) { nd++; console.log('FAIL digest ' + o.name + ' node ' + dg[o.fn] + ' python ' + D.ext.host_python[o.fn]); }
say(nd === 0, 'ext digests: ' + D.ext.ops.length + ' ops, ' + nd + ' differ from python');
{ const sh = C.opsShape(dg, D); say(sh.ok, 'ext operators: the replay digested exactly the host record (data ext.expect): ' + sh.got + (sh.ok ? '' : '   WANT ' + sh.want)); }
const sp = C.spinProbe(e2, D);
for (const n of D.ext.spin.n) say(String(sp[n]) === String(D.ext.spin.host_python[n]), 'spin(' + n + ') = ' + sp[n]);
const mod2 = new WebAssembly.Module(C.bytes(D.ext.b64));
const ml = C.memloopProbe(mod2, D);
for (const n of D.ext.memloop.n) say(String(ml[n]) === String(D.ext.memloop.host_python[n]), 'memloop(' + n + ') = ' + ml[n]);
ml[D.ext.memloop.timing_n] = new WebAssembly.Instance(mod2).exports.memloop(D.ext.memloop.timing_n);
say(String(ml[D.ext.memloop.timing_n]) === String(D.ext.memloop.host_python[D.ext.memloop.timing_n]), 'memloop(' + D.ext.memloop.timing_n + ') = ' + ml[D.ext.memloop.timing_n]);
for (const [n, w, g] of C.memProbe(new WebAssembly.Instance(mod2).exports)) say(w === g, 'mem: ' + n + ' want ' + w + ' got ' + g);
for (const [n, w, g] of C.jsMemory()) say(w === g, 'jsmem: ' + n + ' want ' + w + ' got ' + g);
// NEW: the memory script, replayed on a fresh instance: python MemModel (wants) vs Node (V8 x64); and the shape the page asserts (phases, step counts, functions)
{
  const res = C.memScript(new WebAssembly.Instance(new WebAssembly.Module(C.bytes(D.ext.b64))).exports, D.ext.mem);
  const phases = {}; for (const [ph, n, w, g] of res) { (phases[ph] = phases[ph] || { n: 0, bad: [] }).n++; if (w !== g) phases[ph].bad.push(n + ' want ' + w + ' got ' + g); }
  { const sh = C.memShape(res, D.ext.mem); say(sh.ok, 'mem script: the replay executed exactly the host record (data ext.mem.expect): ' + sh.got + (sh.ok ? '' : '   WANT ' + sh.want)); }
  for (const ph in phases) say(phases[ph].bad.length === 0, 'mem script [' + ph + ']: ' + phases[ph].n + ' steps, ' + phases[ph].bad.length + ' differ' + (phases[ph].bad.length ? ' e.g. ' + phases[ph].bad.slice(0, 3).join('; ') : ''));
}
fs.writeFileSync(__dirname + '/node-answers.json', JSON.stringify({ node: process.version, v8: process.versions.v8, digests: dg, spin: Object.fromEntries(D.ext.spin.n.map(n => [String(n), sp[n]])), memloop: Object.fromEntries(Object.keys(ml).map(n => [n, ml[n]])) }, null, 1));
console.log(bad ? bad + ' FAILURES' : 'node cross-check: all agree');
process.exit(bad ? 1 : 0);
