// Mutation gate for wasm.html: makes copies of the kit + the runner in which ONE byte pattern of the embedded ext module (base64 in
// data/wasm-data.js) is changed, so that a correct page must FAIL on each copy, naming the step. The real kit is never touched.
//   node mutate.js <scratch-root>        creates <scratch-root>/<mutant>/{kit,runner} for every mutant, and <scratch-root>/list.txt
//   then, per mutant, e.g.:  node <scratch-root>/<mutant>/runner/run.js --config turbofan --pages wasm --out <dir>   (must FAIL)
// The tier mutants need no copy: run the real runner with   --config liftoff-only --extra "--js-flags=--allow-natives-syntax"   (the later
// --js-flags wins, which is what a dropped flag looks like) and the same with --config no-liftoff: both must FAIL on the tier rows.
const fs = require('fs'), path = require('path');
const TESTS = path.resolve(__dirname, '..', '..');                          // venetium/tests
const root = path.resolve(process.argv[2] || '');
if (!process.argv[2]) { console.error('usage: node mutate.js <scratch-root>'); process.exit(2); }
const dataTxt = fs.readFileSync(path.join(TESTS, 'kit', 'data', 'wasm-data.js'), 'utf8');
const m = dataTxt.match(/"ext": \{\s*"b64": "([A-Za-z0-9+/=]+)"/);
const bytes = Uint8Array.from(Buffer.from(m[1], 'base64'));
const find = pat => { const hits = []; outer: for (let i = 0; i + pat.length <= bytes.length; i++) { for (let j = 0; j < pat.length; j++) if (bytes[i + j] !== pat[j]) continue outer; hits.push(i); } return hits; };
// name: [pattern, replacement, which hit (null: the pattern must be unique; 0 = the first, -1 = the last), what it models]
const MUTANTS = {
  'ld64_16s-as-16u':       [[0, 0x20, 0, 0x32, 1, 0, 0x0b], [0, 0x20, 0, 0x33, 1, 0, 0x0b], null, 'i64.load16_s loads zero-extended'],
  'ld8_s-as-ld8_u':        [[0, 0x20, 0, 0x2c, 0, 0, 0x0b], [0, 0x20, 0, 0x2d, 0, 0, 0x0b], null, 'i32.load8_s loads zero-extended'],
  'st16-as-st8':           [[0, 0x20, 0, 0x20, 1, 0x3b, 1, 0, 0x0b], [0, 0x20, 0, 0x20, 1, 0x3a, 0, 0, 0x0b], null, 'i32.store16 stores one byte'],
  'shr_s_const32-as-shr_u': [[0, 0x20, 0, 0x42, 0x20, 0x87, 0x0b], [0, 0x20, 0, 0x42, 0x20, 0x88, 0x0b], null, 'i64.shr_s by constant 32 is a logical shift'],
  'rotl_c32-as-c33':       [[0, 0x20, 0, 0x42, 0x20, 0x89, 0x0b], [0, 0x20, 0, 0x42, 0x21, 0x89, 0x0b], null, 'i64.rotl by constant 32 rotates by 33'],
  'rotl_vsh-as-rotr':      [[0, 0x20, 0, 0x20, 1, 0x89, 0x0b], [0, 0x20, 0, 0x20, 1, 0x8a, 0x0b], -1, 'variable-amount i64.rotl rotates right (the pattern also matches the old i64_rotl: the LAST hit is the new function)'],
  'ld32_o4G4-offset-0':    [[0x28, 2, 0xfc, 0xff, 0xff, 0xff, 0x0f], [0x28, 2, 0x80, 0x80, 0x80, 0x80, 0x00], 0, 'offset 0xfffffffc of ld32_o4G4 (the first hit; ld32_c8_o4G4 has the same bytes) treated as 0: no trap where the 32-bit sum must not wrap'],
  'f64_copy-reversed':     [[0, 0x20, 1, 0x20, 0, 0x2b, 3, 0, 0x39, 3, 0, 0x0b], [0, 0x20, 0, 0x20, 1, 0x2b, 3, 0, 0x39, 3, 0, 0x0b], null, 'f64 copy goes the wrong way'],
  'ld16_u_c65535-as-c65534': [[0, 0x41, 0xff, 0xff, 0x03, 0x2f, 1, 0, 0x0b], [0, 0x41, 0xfe, 0xff, 0x03, 0x2f, 1, 0, 0x0b], null, 'i32.load16_u at the constant address 65535 (straddles the end: must trap) uses 65534'],
  'ld32_c8_o4G4-offset-0': [[0, 0x41, 8, 0x28, 2, 0xfc, 0xff, 0xff, 0xff, 0x0f, 0x0b], [0, 0x41, 8, 0x28, 2, 0x80, 0x80, 0x80, 0x80, 0x00, 0x0b], null, 'constant address 8 + offset 0xfffffffc (must trap) with the offset dropped'],
  'f32_st_bits-offset-1':  [[0, 0x20, 0, 0x20, 1, 0xbe, 0x38, 2, 0, 0x0b], [0, 0x20, 0, 0x20, 1, 0xbe, 0x38, 2, 1, 0x0b], null, 'f32.reinterpret_i32 + f32.store writes one byte further'],
  'f64_ld_bits-offset-1':  [[0, 0x20, 0, 0x2b, 3, 0, 0xbd, 0x0b], [0, 0x20, 0, 0x2b, 3, 1, 0xbd, 0x0b], null, 'f64.load + i64.reinterpret reads one byte further'],
};
fs.mkdirSync(root, { recursive: true });
const list = [];
for (const [name, [pat, rep, which, what]] of Object.entries(MUTANTS)) {
  const hits = find(pat);
  console.log(name.padEnd(26), 'pattern hits:', hits.length, hits.slice(0, 6).join(','));
  if (hits.length < 1 || (which === null && hits.length > 1)) { console.log('  PATTERN NOT UNIQUE OR NOT FOUND - mutant skipped'); continue; }
  const b2 = Uint8Array.from(bytes), at = which === null ? hits[0] : which < 0 ? hits[hits.length + which] : hits[which];
  rep.forEach((v, i) => { b2[at + i] = v; });
  const out = path.join(root, name);
  fs.rmSync(out, { recursive: true, force: true });
  fs.mkdirSync(out, { recursive: true });
  fs.cpSync(path.join(TESTS, 'kit'), path.join(out, 'kit'), { recursive: true });
  fs.cpSync(path.join(TESTS, 'runner'), path.join(out, 'runner'), { recursive: true });
  fs.writeFileSync(path.join(out, 'kit', 'data', 'wasm-data.js'), dataTxt.replace(m[1], Buffer.from(b2).toString('base64')));
  list.push(name);
  console.log('  ->', out, '(' + what + ')');
}
fs.writeFileSync(path.join(root, 'list.txt'), list.join('\n') + '\n');
