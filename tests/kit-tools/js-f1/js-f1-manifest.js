// Node tool (server only; NOT part of the kit): writes / checks kit/manifest/js-f1.tsv, the list of the files the js-f1 page owns.
//   node js-f1-manifest.js            rewrites the manifest with the sha256 of the files as they are now
//   node js-f1-manifest.js --check    compares the manifest on disk with what would be written (exit 1 on any difference)
//   [--kit <dir>]                     another copy of the kit folder (a scratch copy)
// Format: header `path<TAB>sha256<TAB>source`, then one row per owned file, path relative to the kit folder (nothing outside the kit).
// The files: js-f1.html, lib/js-f1-probe.js, lib/js-f1-natives.js, lib/js-f1-host.js. lib/configs.js, lib/kit.js and lib/build.js are shared
// kit files (not listed here); the packaging step writes each package's lib/build.js.
'use strict';
const fs = require('fs'), path = require('path'), crypto = require('crypto');
const TESTS = path.resolve(__dirname, '..', '..');
const argv = process.argv.slice(2);
const i = argv.indexOf('--kit');
const KIT = i >= 0 ? path.resolve(argv[i + 1]) : path.join(TESTS, 'kit');
const sha = p => crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const f1 = sha(path.join(TESTS, 'f1probe.js'));
const derived = what => 'derived mechanically from venetium/tests/f1probe.js (sha256 ' + f1 + ', 120 lines) by kit-tools/js-f1/js-f1-derive.js: ' + what;
const FILES = [
  ['js-f1.html', 'hand-written: the page (loads lib/configs.js, lib/build.js, lib/kit.js, the probe and the glue, runs F1Host.run(Kit), publishes with Kit.done)'],
  ['lib/js-f1-probe.js', derived('lines 21-71, 95-113, 115-119 and 120 copied byte for byte inside function F1Probe(host); no natives syntax')],
  ['lib/js-f1-natives.js', derived('lines 73-77 (function tier) and 79-93 (warm-up) copied byte for byte, plus three kit-added probes (%IsMaglevEnabled, %IsSparkplugEnabled, %IsTurbofanEnabled); needs --allow-natives-syntax, loaded only when Kit.natives')],
  ['lib/js-f1-host.js', 'hand-written: the glue (f1probe CHECK / TIER / SUMMARY lines to kit rows; the tier proof; the Maglev n/a decision from Kit.build (has_maglev false AND v8_current_cpu x86) or, with no record, Kit.arch() (ia32); flags-reached-V8 and lite-mode rows; the one planted expectation); rehearsed by kit-tools/js-f1/js-f1-nodetest.js'],
];
const text = 'path\tsha256\tsource\n' + FILES.map(([p, s]) => p + '\t' + sha(path.join(KIT, p)) + '\t' + s + '\n').join('');
const out = path.join(KIT, 'manifest', 'js-f1.tsv');
if (argv.includes('--check')) {
  const same = fs.existsSync(out) && fs.readFileSync(out, 'utf8') === text;
  console.log('manifest/js-f1.tsv ' + (same ? 'matches' : 'DIFFERS') + ' (' + KIT + ')');
  process.exit(same ? 0 : 1);
}
fs.writeFileSync(out, text);
console.log('wrote ' + out + ' (' + FILES.length + ' rows)');
