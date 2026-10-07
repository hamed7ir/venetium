// js-jit kit tool (server only, not shipped): proves that js-jit.html's checks CAN fail. It copies the kit and the runner into a scratch
// folder (the real kit is never touched), applies one mutation at a time to the COPY (a replaced string in a page / lib / data file, a
// different lib/build.js, a stub of what navigator.userAgentData reports, or different --js-flags for the browser), runs the copy's
// runner on js-jit in the real browser (out\x86-rt21\venetium.exe, headless) and compares the result with the verdict the page MUST give.
// Exit code 0 only when every mutant came out as expected.
//   node js-jit-mutants.js --scratch <folder> [--exe <venetium.exe>] [--out <runs folder>] [--prefix <folder name prefix>] [--only <id prefix>] [--refresh]
// The result of mutant <id> is kept in <out>\<prefix><id>\ (js-jit.json, js-jit.png, runner.log); the prefix defaults to mut-js-jit-.
// Expectation fields: v (verdict), bad (exact number of failed checks), re (regexes, each must match the NAME of a failed check), got (regexes, each
// must match the got text of a failed check), tier (regex for the tier label), info ([[name regex, got regex], ...]: that row exists, is an info
// row (it can never fail) and has that got text).
// On the ia32 browser Maglev does not exist, so 'Maglev present' paths cannot be exercised here (js-jit-d8-matrix.js covers them with the
// ARM-simulator d8); the mutants below cover everything else, including the n/a decision for the Maglev configurations.
'use strict';
const fs = require('fs'), path = require('path'), { spawnSync } = require('child_process');
const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf('--' + n); return i >= 0 ? args[i + 1] : d; };
const SCRATCH = opt('scratch'); if (!SCRATCH) { console.error('--scratch <folder> is required'); process.exit(2); }
const EXE = opt('exe', 'F:\\cr\\src\\out\\x86-rt21\\venetium.exe'), OUT = opt('out', 'F:\\cr\\device-1\\runs'), ONLY = opt('only', ''), PREFIX = opt('prefix', 'mut-js-jit-');
const REAL = path.resolve(__dirname, '..', '..');                         // venetium\tests
const SK = path.join(SCRATCH, 'venetium', 'tests', 'kit'), SR = path.join(SCRATCH, 'venetium', 'tests', 'runner');
if (args.includes('--refresh') || !fs.existsSync(SK)) {
  fs.rmSync(path.join(SCRATCH, 'venetium'), { recursive: true, force: true });
  fs.cpSync(path.join(REAL, 'kit'), SK, { recursive: true }); fs.cpSync(path.join(REAL, 'runner'), SR, { recursive: true });
}
const IGN = ['--no-sparkplug', '--no-maglev', '--no-turbofan', '--allow-natives-syntax'];
const uad = v => ({ file: 'js-jit.html', from: '<script src="lib/kit.js"></script>', to: '<script>Object.defineProperty(navigator, "userAgentData", { configurable: true, value: ' + v + ' });</script>\n<script src="lib/kit.js"></script>' });
const stub = o => uad('{ getHighEntropyValues: () => Promise.resolve(' + JSON.stringify(o) + ') }');
const build = o => ({ file: 'lib/build.js', content: 'window.KIT_BUILD = ' + JSON.stringify(o) + ';\n' });
const rep = (file, from, to) => ({ file, from, to });
const NOMAGLEV = [/Maglev is available/];
const M = [];
// id, config, extra js-flags (null = the configuration's own), edits, expectation {v, bad, re, tier}
const add = (id, cfg, flags, edits, expect) => M.push({ id, cfg, flags, edits, expect });
add('ctl-maglev', 'maglev', null, [], { v: 'PASS', bad: 0, tier: /^n\/a \(no Maglev on ia32\)/ });
add('ctl-jitless', 'jitless', null, [], { v: 'PASS', bad: 0, tier: /^interpreter 13\/13 \(lite-mode set\)/ });
// the Maglev n/a decision: build record first, else the reported CPU; every doubtful case fails
add('b1-build-says-arm-maglev', 'maglev', null, [build({ v8_current_cpu: 'arm', has_maglev: true })], { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('b2-build-says-arm-maglev-caller', 'maglev-caller', null, [build({ v8_current_cpu: 'arm', has_maglev: true })], { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('b3-build-says-x86-no-maglev', 'maglev', null, [build({ v8_current_cpu: 'x86', has_maglev: false })], { v: 'PASS', bad: 0, tier: /^n\/a \(no Maglev on ia32\)/ });
add('b4-build-says-x86-but-cpu-reported-arm32', 'maglev', null, [build({ v8_current_cpu: 'x86', has_maglev: false }), stub({ architecture: '', bitness: '64', wow64: false })], { v: 'PASS', bad: 0, tier: /^n\/a/ });
add('b5-build-record-without-boolean-falls-back-to-cpu', 'maglev', null, [build({ v8_current_cpu: 'arm', has_maglev: 'yes' })], { v: 'PASS', bad: 0, tier: /^n\/a/ });
// a record grants n/a only with has_maglev false AND v8_current_cpu 'x86' (b3 / b4 above stay PASS): has_maglev false for a CPU V8 builds Maglev for
// is a FAILURE, even though this x86 browser reports ia32 (the record, not the browser, decides)
const SAYS = cpu => [new RegExp('the build record says ' + cpu + ' has no Maglev, but V8 builds Maglev for ' + cpu)];
add('b6-build-says-arm-has-no-maglev', 'maglev', null, [build({ v8_current_cpu: 'arm', has_maglev: false })], { v: 'FAIL', bad: 14, re: NOMAGLEV, got: SAYS('arm'), tier: /^NOT PROVEN/ });
add('b7-build-says-x64-has-no-maglev-caller', 'maglev-caller', null, [build({ v8_current_cpu: 'x64', has_maglev: false })], { v: 'FAIL', bad: 14, re: NOMAGLEV, got: SAYS('x64') });
add('b8-build-says-has-no-maglev-names-no-cpu', 'maglev', null, [build({ has_maglev: false })], { v: 'FAIL', bad: 14, re: NOMAGLEV, got: [/names no v8_current_cpu/] });
add('b9-build-says-ia32-has-no-maglev (only the string x86 counts)', 'maglev', null, [build({ v8_current_cpu: 'ia32', has_maglev: false })], { v: 'FAIL', bad: 14, re: NOMAGLEV, got: SAYS('ia32') });
add('b10-build-says-arm-has-no-maglev-and-cpu-reported-arm32', 'maglev-caller', null, [build({ v8_current_cpu: 'arm', has_maglev: false }), stub({ architecture: '', bitness: '64', wow64: false })], { v: 'FAIL', bad: 14, re: NOMAGLEV, got: SAYS('arm') });
add('a1-cpu-reported-arm32', 'maglev', null, [stub({ architecture: '', bitness: '64', wow64: false })], { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('a2-cpu-reported-x64', 'maglev', null, [stub({ architecture: 'x86', bitness: '64', wow64: false })], { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('a3-cpu-reported-arm64', 'maglev', null, [stub({ architecture: 'arm', bitness: '64', wow64: false })], { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('a4-getHighEntropyValues-never-answers', 'maglev', null, [uad('{ getHighEntropyValues: () => new Promise(() => {}) }')], { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('a5-getHighEntropyValues-rejects', 'maglev-caller', null, [uad('{ getHighEntropyValues: () => Promise.reject(new Error("stub")) }')], { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('a6-no-userAgentData', 'maglev', null, [uad('undefined')], { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('a7-cpu-reported-ia32-on-32-bit-os', 'maglev', null, [stub({ architecture: 'x86', bitness: '32', wow64: false })], { v: 'PASS', bad: 0, tier: /^n\/a/ });
add('a8-config-without-ia32-na', 'maglev', null, [rep('lib/configs.js', "tier: 'maglev', who: 'callee', ia32: 'n/a'", "tier: 'maglev', who: 'callee'")], { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('p1-probe-claims-maglev-present', 'maglev', null, [rep('lib/js-jit-probe.js', 'maglev: %IsMaglevEnabled()', 'maglev: true')], { v: 'FAIL', re: [/^tier\./], tier: /^NOT PROVEN/ });
// jitless against ignition, flags that did not reach V8, natives rejected
add('j1-jitless-config-with-ignition-flags', 'jitless', IGN, [], { v: 'FAIL', bad: 2, re: [/typeof WebAssembly/, /lite-mode bit/] });
add('j1b-same-without-the-wasm-check (lite-mode bit alone)', 'jitless', IGN, [rep('js-jit.html', "wasmType === 'undefined')", "true)")], { v: 'FAIL', bad: 1, re: [/lite-mode bit/] });
add('j2-ignition-config-with-jitless-flags', 'ignition', ['--jitless', '--allow-natives-syntax'], [], { v: 'FAIL', bad: 2, re: [/typeof WebAssembly/, /lite-mode bit/] });
add('j2b-same-without-the-wasm-check (lite-mode bit alone)', 'ignition', ['--jitless', '--allow-natives-syntax'], [rep('js-jit.html', "wasmType === 'object')", "true)")], { v: 'FAIL', bad: 1, re: [/lite-mode bit/] });
add('j3-jitless-config-without-jitless', 'jitless', ['--allow-natives-syntax'], [], { v: 'FAIL', re: [/typeof WebAssembly/, /lite-mode bit/, /flags reached V8: sparkplug/] });
add('j4-ignition-config-with-default-flags', 'ignition', ['--allow-natives-syntax'], [], { v: 'FAIL', re: [/flags reached V8: sparkplug/, /flags reached V8: turbofan/] });
add('j5-sparkplug-config-with-ignition-flags', 'sparkplug', IGN, [], { v: 'FAIL', re: [/flags reached V8: sparkplug/, /^tier\./] });
add('j6-turbofan-config-with-no-turbofan', 'turbofan', ['--allow-natives-syntax', '--no-turbofan'], [], { v: 'FAIL', re: [/^tier\./] });
// (kit.js's window error handler also records the SyntaxError of the %-syntax file as 'uncaught error | Script error.': 2 failed rows)
add('n1-maglev-config-natives-not-passed', 'maglev', ['--stack-size=900'], [], { v: 'FAIL', bad: 2, re: [/natives syntax accepted/, /uncaught error/], tier: /^NOT PROVEN: natives expected but rejected/ });
add('n2-jitless-config-natives-not-passed', 'jitless', ['--stack-size=900'], [], { v: 'FAIL', bad: 3, re: [/natives syntax accepted/, /typeof WebAssembly/], tier: /^NOT PROVEN: natives expected but rejected/ });
// the data file (the first occurrence of a key is in JIT_EXPECTED, the last one in JIT_NODE_X64)
const D = "data/js-jit-expected.js";
// the data-agreement row is information only now (a build-time property of the shipped file, pinned by the manifest sha256 and gen-expected.js):
// d1 changes ONLY the Node copy of imul, which no browser check reads, so the run still PASSES and the info row shows the disagreement
const AGREE = [/data file, not a browser check: host python and host Node x64 agree/, /^DISAGREE on \["imul"\]$/];
add("d1-node-answer-differs-from-python (info row shows it, verdict unchanged)", "default", null, [{ file: D, from: "\"imul\": \"-2080158760\"", to: "\"imul\": \"-2080158761\"", last: true }], { v: "PASS", bad: 0, info: [AGREE] });
const IMUL_LINE = '  "imul": "-2080158760",\n';
add("d2-key-missing-from-python-answers", "default", null, [{ file: D, from: IMUL_LINE, to: "" }], { v: "FAIL", bad: 1, re: [/answer imul/], info: [AGREE] });
add("d3-wrong-sin-crosstier", "default", null, [rep(D, "\"sin_crosstier\": \"0.393269602538\"", "\"sin_crosstier\": \"0.393269602539\"")], { v: "FAIL", bad: 1, re: [/answer sin_crosstier/] });
add("d4-wrong-python-answer", "default", null, [rep(D, "\"divmod\": \"-2268820\"", "\"divmod\": \"-2268821\"")], { v: "FAIL", bad: 1, re: [/answer divmod/], info: [[AGREE[0], /^DISAGREE on \["divmod"\]$/]] });
// print(): the collector assignment cannot be refused, so its row is information only; if print() were NOT the collector the run still fails, on core.js's
// own checks (nothing is collected: 'core.js ran to completion' and every answer), never on the print row
add("p2-print-is-not-the-collector (info row says so, core.js checks fail)", "default", null, [rep('js-jit.html', "window.print = collector;", "window.print = collector; Object.defineProperty(window, 'print', { value: function () {}, configurable: true, writable: true });")],
  { v: "FAIL", re: [/core\.js ran to completion/, /^answer /], info: [[/^print\(\) is the page collector/, /^window\.print$/]] });
add("d5-key-missing-from-both-objects", "default", null, [{ file: D, from: IMUL_LINE, to: "" }, { file: D, from: IMUL_LINE, to: "" }], { v: "FAIL", re: [/unexpected keys/] });

let unexpected = 0, n = 0;
const read = (f) => fs.readFileSync(f, 'utf8');
const touched = new Set();
for (const m of M) {
  if (ONLY && !m.id.startsWith(ONLY)) continue;
  for (const f of touched) fs.copyFileSync(path.join(REAL, 'kit', f), path.join(SK, f));            // pristine again
  for (const e of m.edits) {
    touched.add(e.file);
    const fp = path.join(SK, e.file);
    if (e.content !== undefined) fs.writeFileSync(fp, e.content);
    else {
      const t = read(fp), i = e.last ? t.lastIndexOf(e.from) : t.indexOf(e.from);
      if (i < 0) throw new Error(m.id + ': anchor not found in ' + e.file + ': ' + e.from.slice(0, 60));
      fs.writeFileSync(fp, t.slice(0, i) + e.to + t.slice(i + e.from.length));
    }
  }
  const out = path.join(OUT, PREFIX + m.id.split(' ')[0]);
  const a = [path.join(SR, 'run.js'), '--config', m.cfg, '--pages', 'js-jit', '--exe', EXE, '--out', out];
  if (m.flags) a.push('--extra', '--js-flags=' + m.flags.join('\t'));          // the last --js-flags wins; V8 splits the flags on any whitespace
  const p = spawnSync(process.execPath, a, { encoding: 'utf8', timeout: 300000 });
  let r = null;
  try { r = JSON.parse(read(path.join(out, 'js-jit.json'))).result; } catch (e) {}
  const e = m.expect, why = [];
  if (!r) why.push('no result (rc ' + p.status + ' ' + (p.stdout + p.stderr).trim().slice(-200) + ')');
  else {
    const bad = r.checks.filter(c => !c.ok);
    if (r.verdict !== e.v) why.push('verdict ' + r.verdict + ' want ' + e.v);
    if (e.bad !== undefined && bad.length !== e.bad) why.push('bad ' + bad.length + ' want ' + e.bad);
    for (const x of (e.re || [])) if (!bad.some(b => x.test(b.name))) why.push('no failed check matching ' + x);
    for (const x of (e.got || [])) if (!bad.some(b => x.test(b.got))) why.push('no failed check whose got text matches ' + x);
    for (const [nameRe, gotRe] of (e.info || [])) { const row = r.checks.find(c => nameRe.test(c.name)); if (!row) why.push('no row matching ' + nameRe); else if (row.info !== true || row.ok !== true) why.push('row ' + nameRe + ' is not an info row'); else if (!gotRe.test(row.got)) why.push('info row ' + nameRe + ' got "' + row.got + '" does not match ' + gotRe); }
    if (e.tier && !e.tier.test(r.tier || '')) why.push('tier "' + r.tier + '" does not match ' + e.tier);
    m.line = r.verdict + ' checks=' + r.checks.length + ' bad=' + bad.length + ' tier="' + r.tier + '" first bad: ' + (bad[0] ? bad[0].name.slice(0, 70) + ' | got ' + String(bad[0].got).slice(0, 60) : '-');
  }
  n++; if (why.length) unexpected++;
  console.log((why.length ? 'UNEXPECTED ' : 'ok         ') + m.id.padEnd(56) + ' ' + m.cfg.padEnd(13) + ' ' + (m.line || '') + (why.length ? '\n             ' + why.join('; ') : ''));
}
for (const f of touched) fs.copyFileSync(path.join(REAL, 'kit', f), path.join(SK, f));
console.log(n + ' mutants, ' + unexpected + ' unexpected');
process.exit(unexpected ? 1 : 0);
