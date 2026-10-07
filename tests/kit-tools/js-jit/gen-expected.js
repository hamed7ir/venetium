// js-jit kit tool: (re)generate kit\data\js-jit-expected.js — the host answers js-jit.html compares with — from s10\jit\expected.txt
// (the host python's answers) and s10\jit\node-x64.txt (host Node.js, V8 on x64). Plain Node, nothing to install.
//   node gen-expected.js [--s10 <s10\jit folder>] [--out <file>] [--check] [--python <python.exe>]
//     (no flag)   write the data file (default: ..\..\kit\data\js-jit-expected.js)
//     --check     write nothing; exit 1 unless the existing data file is byte-identical to what would be generated
//     --python P  also run `P <s10\jit>\expected.py` and require its output to equal expected.txt (the provenance of expected.txt)
// Always checked: the 12 core answers of expected.txt (everything except wasm_*) equal the same keys in node-x64.txt, and node-x64.txt
// has sin_crosstier. The wasm_i32 / wasm_i64 lines of expected.txt are not used (they belong to wasm.html).
'use strict';
const fs = require('fs'), path = require('path'), cp = require('child_process');
const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf('--' + n); return i >= 0 ? args[i + 1] : d; };
const S10 = path.resolve(opt('s10', path.join(__dirname, '..', '..', '..', '..', 's10', 'jit')));
const OUT = path.resolve(opt('out', path.join(__dirname, '..', '..', 'kit', 'data', 'js-jit-expected.js')));
const read = f => fs.readFileSync(path.join(S10, f), 'utf8').replace(/\r\n/g, '\n');
const kv = txt => { const o = {}; for (const l of txt.split('\n')) { const i = l.indexOf('='); if (i > 0) o[l.slice(0, i).trim()] = l.slice(i + 1).trim(); } return o; };
const core = o => Object.fromEntries(Object.entries(o).filter(([k]) => k !== 'done' && !k.startsWith('wasm')));
const E = core(kv(read('expected.txt'))), NODE = core(kv(read('node-x64.txt')));
let problems = 0;
const bad = m => { console.error('PROBLEM: ' + m); problems++; };
if (Object.keys(E).length !== 12) bad('expected.txt has ' + Object.keys(E).length + ' core answers, 12 expected');
if (!('sin_crosstier' in NODE)) bad('node-x64.txt has no sin_crosstier');
for (const k of Object.keys(E)) if (NODE[k] !== E[k]) bad('python and Node x64 disagree on ' + k + ': ' + E[k] + ' vs ' + NODE[k]);
if (opt('python')) {
  const p = cp.spawnSync(opt('python'), [path.join(S10, 'expected.py')], { encoding: 'utf8' });
  if (p.status !== 0) bad('expected.py failed: ' + p.stderr);
  else if (p.stdout.replace(/\r\n/g, '\n') !== read('expected.txt')) bad('output of expected.py differs from expected.txt');
  else console.log('expected.py output == expected.txt');
}
const body = o => Object.entries(o).map(([k, v]) => '  ' + JSON.stringify(k) + ': ' + JSON.stringify(v) + ',\n').join('');
const text =
`// Venetium test kit — js-jit.html's embedded host answers (BATCH-X86-2 §1). GENERATED from s10\\jit\\expected.txt and s10\\jit\\node-x64.txt
// (the two files are not read at run time: every file:// document is its own opaque origin, so data lives in a .js file).
// JIT_EXPECTED: the host's independent answers for core.js, as printed by \`python s10\\jit\\expected.py\` (x64 CPython, exact integers;
//   JS semantics emulated explicitly). expected.txt also holds wasm_i32 / wasm_i64: those belong to wasm.html, not here.
// JIT_NODE_X64: the answers of host Node.js (V8 on x64) for the same cases, node-x64.txt. run_jit.py uses it only for sin_crosstier (libm-
//   dependent, not recomputable on the host); this page embeds all of them, and the build step verified they agree with JIT_EXPECTED.
window.JIT_EXPECTED = {
${body(E)}};
window.JIT_NODE_X64 = {
${body(NODE)}};
`;
if (problems) { console.error(problems + ' problem(s); nothing written'); process.exit(1); }
if (args.includes('--check')) {
  const have = fs.existsSync(OUT) ? fs.readFileSync(OUT, 'utf8') : null;
  if (have === text) console.log('OK: ' + OUT + ' is byte-identical to the generated data'); else { console.error('DIFFERENT: ' + OUT); process.exit(1); }
} else { fs.writeFileSync(OUT, text); console.log('wrote ' + OUT + ' (' + Object.keys(E).length + ' + ' + Object.keys(NODE).length + ' answers)'); }
