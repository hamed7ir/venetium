// Node tool (server only; NOT part of the kit, loaded by no page): derives the two script files of the kit's js-f1 page mechanically
// from venetium/tests/f1probe.js.
//   node js-f1-derive.js                       writes kit/lib/js-f1-probe.js and kit/lib/js-f1-natives.js
//   node js-f1-derive.js --check               regenerates in memory and compares with the files on disk (exit 1 on any difference)
//   node js-f1-derive.js [--check] --kit <dir> the same against another copy of the kit folder (a scratch copy for a mutation run)
// Run it with the server's Node (F:\cr\src\third_party\node\win\node.exe); paths are relative to this file:
//   <tests>\f1probe.js (input, stays in the record)  <tests>\kit\lib\js-f1-probe.js, js-f1-natives.js (output)  <tests>\kit-tools\js-f1\ (here)
// f1probe.js is cut at six anchor lines into segments; the segments that carry the probe's logic are copied BYTE FOR BYTE (the tool
// asserts that every one of them is a contiguous substring of the output), nothing is re-indented or rewritten:
//   decls   `var MODE ...` up to `function tier`    the call table (f1probe.js lines 21-71; blank line 72 trimmed) -> js-f1-probe.js
//   tier    `function tier(f) {...}`                decodes %GetOptimizationStatus (lines 73-77)           -> js-f1-natives.js  (needs %)
//   warmup  `// Bring the right functions ...` block  %PrepareFunctionForOptimization / %Optimize...  (79-93)   -> js-f1-natives.js  (needs %)
//   checks  `var checks = 0, bad = 0;` ... measured calls (lines 95-113)                                   -> js-f1-probe.js
//   tierln  the `TIER caller|callee` print block (lines 115-119)                                           -> js-f1-probe.js, run only if a tier function exists
//   summary `print('SUMMARY ...')` (line 120)                                                              -> js-f1-probe.js
// What the tool adds (and nothing else): the wrapper `function F1Probe(host) {...}`, `var print = host.print; var tier = host.tier;`,
// the call `host.warmup(MODE, CALLEES, CALLS, KEYS)` at the place of the warm-up block, `if (tier) { ... }` around the TIER lines, and,
// in js-f1-natives.js only, a block of probes marked "ADDED by the kit" (%IsMaglevEnabled etc.).
'use strict';
const fs = require('fs'), path = require('path'), crypto = require('crypto');
const TESTS = path.resolve(__dirname, '..', '..');                       // venetium/tests
const argv = process.argv.slice(2);
const kitArg = argv.indexOf('--kit');
const KIT = kitArg >= 0 ? path.resolve(argv[kitArg + 1]) : path.join(TESTS, 'kit');
const SRC = path.join(TESTS, 'f1probe.js');
const OUT_PROBE = path.join(KIT, 'lib', 'js-f1-probe.js'), OUT_NAT = path.join(KIT, 'lib', 'js-f1-natives.js');
const TOOL = 'kit-tools/js-f1/js-f1-derive.js';
const raw = fs.readFileSync(SRC);
const srcSha = crypto.createHash('sha256').update(raw).digest('hex');
const text = raw.toString('utf8');
if (text.includes('\r')) throw new Error('f1probe.js has CR characters; the tool expects LF');
const L = text.replace(/\n$/, '').split('\n');   // L[i] is line i+1

function find(re, from) {                          // the single line index matching re (at or after `from`)
  const hits = []; for (let i = from || 0; i < L.length; i++) if (re.test(L[i])) hits.push(i);
  if (hits.length !== 1) throw new Error('anchor ' + re + ' matched ' + hits.length + ' lines (want exactly 1) - f1probe.js changed?');
  return hits[0];
}
const iDecls = find(/^var MODE = globalThis\.F1_MODE \|\| 'default';$/);
const iTier = find(/^function tier\(f\) \{$/);
const iWarm = find(/^\/\/ Bring the right functions into the right tier/);
const iChecks = find(/^var checks = 0, bad = 0;$/);
const iTierLn = find(/^if \(MODE === 'maglev-caller' \|\| MODE === 'turbofan-caller'\) \{$/, iChecks);   // the one after the measured calls
const iSummary = find(/^print\('SUMMARY /);
if (!(iDecls < iTier && iTier < iWarm && iWarm < iChecks && iChecks < iTierLn && iTierLn < iSummary)) throw new Error('anchors out of order');
if (!/^\}$/.test(L[iTier + 4])) throw new Error('function tier is not 5 lines long');
const trimEnd = a => { a = a.slice(); while (a.length && a[a.length - 1].trim() === '') a.pop(); return a; };
const seg = {
  decls: trimEnd(L.slice(iDecls, iTier)),
  tier: L.slice(iTier, iTier + 5),
  warmup: trimEnd(L.slice(iWarm, iChecks)),
  checks: trimEnd(L.slice(iChecks, iTierLn)),
  tierln: trimEnd(L.slice(iTierLn, iSummary)),
  summary: L.slice(iSummary, iSummary + 1),
};
const range = (a, b) => 'lines ' + (a + 1) + '-' + b;
const where = {
  decls: range(iDecls, iDecls + seg.decls.length), tier: range(iTier, iTier + 5), warmup: range(iWarm, iWarm + seg.warmup.length),
  checks: range(iChecks, iChecks + seg.checks.length), tierln: range(iTierLn, iTierLn + seg.tierln.length), summary: range(iSummary, iSummary + 1),
};
// the segments that go to the probe file must be free of V8 natives syntax; the ones that go to the natives file must contain it
for (const k of ['decls', 'checks', 'tierln', 'summary']) if (/%[A-Za-z]/.test(seg[k].join('\n').replace(/'[^'\n]*'/g, ''))) throw new Error('natives syntax in segment ' + k);
for (const k of ['tier', 'warmup']) if (!/%[A-Z]/.test(seg[k].join('\n'))) throw new Error('segment ' + k + ' has no natives syntax?');

const header = k => '// GENERATED by ' + TOOL + ' from venetium/tests/f1probe.js (sha256 ' + srcSha + ', ' + L.length + ' lines). DO NOT EDIT; rerun the tool.\n';
const probe = [
  header(),
  '// The F1 probe\'s logic. The text of f1probe.js ' + where.decls + ' (the call table), ' + where.checks + ' (the checks and the measured calls),',
  '// ' + where.tierln + ' (the TIER lines) and ' + where.summary + ' (the SUMMARY line) is copied byte for byte. Differences from f1probe.js:',
  '//   - the whole is wrapped in function F1Probe(host), so the kit page runs it with its own reporter (f1probe.js runs at top level in d8);',
  '//   - `print` is host.print: the kit\'s reporter receives f1probe.js\'s CHECK / TIER / SUMMARY lines unchanged and turns them into kit checks;',
  '//   - what needs V8 natives syntax (%GetOptimizationStatus, %PrepareFunctionForOptimization, %Optimize...OnNextCall) is NOT in this file -',
  '//     a SyntaxError without --allow-natives-syntax. function tier (' + where.tier + ') and the warm-up block (' + where.warmup + ') are in lib/js-f1-natives.js,',
  '//     loaded only when the configuration enables natives syntax; here they are host.tier and host.warmup (absent -> no warm-up, no TIER lines).',
  '// The configuration comes through globalThis.F1_MODE exactly as in d8 (f1probe.py sets it with -e); the page sets it before calling F1Probe.',
  'function F1Probe(host) {',
  '  var print = host.print;',
  '  var tier = host.tier;                       // f1probe.js function tier (natives), or undefined',
  seg.decls.join('\n'),
  '',
  '  // f1probe.js ' + where.warmup + ' (brings the right functions into the right tier) lives in lib/js-f1-natives.js:',
  '  if (host.warmup) host.warmup(MODE, CALLEES, CALLS, KEYS);',
  '',
  seg.checks.join('\n'),
  '',
  '// f1probe.js ' + where.tierln + ' - only where a tier function exists (natives syntax on):',
  'if (tier) {',
  seg.tierln.join('\n'),
  '}',
  seg.summary.join('\n'),
  '}',
  '',
].join('\n');
const natives = [
  header(),
  '// NEEDS --allow-natives-syntax: without it this whole file is a SyntaxError. The page loads it only when Kit.natives is true.',
  '// tier and warmup are f1probe.js ' + where.tier + ' and ' + where.warmup + ', copied byte for byte (the warm-up block only becomes the body of a function whose',
  '// parameters carry the names the block uses: MODE, CALLEES, CALLS, KEYS - the variables of f1probe.js\'s top level).',
  'var F1Natives = {};',
  '',
  'F1Natives.tier = ' + seg.tier.join('\n') + ';',
  '',
  'F1Natives.warmup = function (MODE, CALLEES, CALLS, KEYS) {',
  seg.warmup.join('\n'),
  '};',
  '',
  '// ---- ADDED by the kit (not in f1probe.js): what the V8 build offers, so a tier that cannot exist is reported as such, not as a failure ----',
  'F1Natives.maglevEnabled = function () { return !!%IsMaglevEnabled(); };       // v8_flags.maglev: a read-only false in a V8 built without Maglev',
  'F1Natives.sparkplugEnabled = function () { return !!%IsSparkplugEnabled(); };',
  'F1Natives.turbofanEnabled = function () { return !!%IsTurbofanEnabled(); };',
  '',
].join('\n');

// every verbatim segment must appear unchanged in its output
for (const k of ['decls', 'checks', 'tierln', 'summary']) if (!probe.includes(seg[k].join('\n'))) throw new Error('segment ' + k + ' is not verbatim in the probe file');
for (const k of ['tier', 'warmup']) if (!natives.includes(seg[k].join('\n'))) throw new Error('segment ' + k + ' is not verbatim in the natives file');

if (argv.includes('--check')) {
  const same = (f, s) => fs.existsSync(f) && fs.readFileSync(f, 'utf8') === s;
  const a = same(OUT_PROBE, probe), b = same(OUT_NAT, natives);
  console.log('js-f1-probe.js ' + (a ? 'matches' : 'DIFFERS') + ', js-f1-natives.js ' + (b ? 'matches' : 'DIFFERS') + ' (f1probe.js sha256 ' + srcSha + '; kit ' + KIT + ')');
  process.exit(a && b ? 0 : 1);
}
fs.writeFileSync(OUT_PROBE, probe); fs.writeFileSync(OUT_NAT, natives);
console.log('wrote ' + OUT_PROBE + ' (' + probe.length + ' bytes) and ' + OUT_NAT + ' (' + natives.length + ' bytes) from ' + SRC + ' sha256 ' + srcSha);
console.log(JSON.stringify(where));
