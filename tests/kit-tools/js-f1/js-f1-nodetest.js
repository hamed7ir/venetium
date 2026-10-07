// js-f1 rehearsal in Node (server only; NOT part of the kit, NOT the product): Node's V8 (13.6, x64) is not Chromium 150's, so this only checks
// the kit's own code - the derived probe, the glue (lib/js-f1-host.js) and the decisions it takes - with the REAL lib/kit.js (reporter,
// verdicts, Kit.arch(), Kit.build, Kit.want) running in a stub DOM, so that "PASS" and "FAIL" mean what they mean in the browser.
//   node js-f1-nodetest.js [--kit <kit dir>] [--only <part>[,<part>...]]     the gate: runs every part in child processes, exit 0 only if all ok
//        parts: static, derive, config:<name> (one per lib/configs.js configuration), matrix:absent, matrix:present
//   (children, started by the gate)   node --allow-natives-syntax <that configuration's js-flags> js-f1-nodetest.js --config <name>
//                                     node --allow-natives-syntax --no-maglev js-f1-nodetest.js --matrix absent
//                                     node --allow-natives-syntax            js-f1-nodetest.js --matrix present
//                                     node js-f1-nodetest.js --static
// Parts:
//  static   the page's script order (configs.js, build.js, kit.js, probe, host), the manifest (header, no row outside the kit, sha256 of every
//           listed file, every owned file listed), exactly one Kit.want in the glue, no fetch/XHR/worker/absolute path, ASCII-only page + glue.
//  derive   js-f1-derive.js --check: lib/js-f1-probe.js and lib/js-f1-natives.js are exactly what f1probe.js derives to.
//  config   A. ORIGINAL f1probe.js (print = collector) vs the derived F1Probe + F1Natives: identical CHECK / TIER / SUMMARY lines.
//           B. the glue with the real Kit: normal = PASS, armed = FAIL with exactly one bad row (the planted one), the F1 bug planted into the
//              probe = FAIL and seen, wrong tier wanted = FAIL, natives off with a named tier = FAIL, and for jitless / ignition / sparkplug /
//              the flags-reached-V8 and lite-mode rows must FAIL when the (wrapped) natives claim the wrong thing.
//  matrix   the Maglev "n/a" decision: reported CPU (9 variants) x build record (none / x86 false / arm true / arm without has_maglev ...) x
//           {Maglev absent: node --no-maglev, Maglev present} x {maglev, maglev-caller}; plus "Maglev present but the tier is not reached".
//           A build record grants n/a ONLY with has_maglev false AND v8_current_cpu "x86" (whatever CPU the browser reports); has_maglev false
//           with any other v8_current_cpu (arm, arm64, x64, ia32, X86, none) is a FAILED check "the build record says <cpu> has no Maglev, but
//           V8 builds Maglev for <cpu>" (matrix:absent), and with Maglev present it is the kit/build mismatch row (matrix:present).
// Node numbers V8's OptimizationStatus bits differently from Chromium 150: the tier decode inside F1Natives.tier is shifted by regex and the
// printed status number is translated bit by bit back to Chromium's (below), so the glue's lite-mode test sees Chromium's number.
'use strict';
const fs = require('fs'), vm = require('vm'), path = require('path'), cp = require('child_process'), crypto = require('crypto');
const TESTS = path.resolve(__dirname, '..', '..');                         // venetium/tests
const argv = process.argv.slice(2);
const opt = (n, d) => { const i = argv.indexOf('--' + n); return i >= 0 ? argv[i + 1] : d; };
const KIT = path.resolve(opt('kit', path.join(TESTS, 'kit')));
const F1PROBE = path.join(TESTS, 'f1probe.js');
const read = p => fs.readFileSync(p, 'utf8');
const sha = p => crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const lib = f => path.join(KIT, 'lib', f);
// the three browser flags the rules forbid anywhere, built from fragments so that this file does not itself contain them as literal text
const FORBIDDEN_FLAGS = new RegExp(['--no-' + 'sandbox', '--disable-web-' + 'security', '--allow-file-' + 'access-from-files'].join('|'));

let fails = 0;
const say = (ok, msg) => { if (!ok) fails++; console.log((ok ? 'ok   ' : 'FAIL ') + msg); };
const short = (s, n) => { s = String(s); return s.length > n ? s.slice(0, n - 3) + '...' : s; };

// ======================================================================================================== static
function partStatic() {
  const html = read(path.join(KIT, 'js-f1.html')), host = read(lib('js-f1-host.js'));
  const order = ['lib/configs.js', 'lib/build.js', 'lib/kit.js', 'lib/js-f1-probe.js', 'lib/js-f1-host.js'];
  const pos = order.map(s => html.indexOf('<script src="' + s + '"></script>'));
  say(pos.every(p => p >= 0) && pos.every((p, i) => i === 0 || p > pos[i - 1]), 'static  js-f1.html loads ' + order.join(' < ') + ' in this order (positions ' + pos.join(',') + ')');
  say((host.match(/Kit\.want\(/g) || []).length === 1, 'static  exactly one Kit.want( in lib/js-f1-host.js (the planted expectation)');
  const bad = []; const mine = { 'js-f1.html': html, 'lib/js-f1-host.js': host, 'lib/js-f1-probe.js': read(lib('js-f1-probe.js')), 'lib/js-f1-natives.js': read(lib('js-f1-natives.js')) };
  for (const [f, t] of Object.entries(mine)) {
    const code = t.replace(/\/\/[^\n]*/g, '');                            // comments may talk about these words
    if (/\b(fetch|XMLHttpRequest|Worker|SharedWorker|importScripts|sendBeacon|WebSocket)\b/.test(code)) bad.push(f + ': network/worker API');
    if (/file:\/\/|[A-Za-z]:\\\\|https?:\/\//.test(code)) bad.push(f + ': absolute path or URL');
    if (FORBIDDEN_FLAGS.test(t)) bad.push(f + ': forbidden browser flag');
  }
  say(bad.length === 0, 'static  no fetch / XHR / worker / absolute path / forbidden flag in the page, glue and derived files' + (bad.length ? ': ' + bad.join('; ') : ''));
  const nonAscii = ['js-f1.html', 'lib/js-f1-host.js'].filter(f => /[^\x00-\x7f]/.test(mine[f]));
  say(nonAscii.length === 0, 'static  js-f1.html and lib/js-f1-host.js are ASCII-only' + (nonAscii.length ? ': ' + nonAscii.join(', ') : ''));
  const noCR = Object.keys(mine).filter(f => mine[f].includes('\r'));
  say(noCR.length === 0, 'static  LF line endings only' + (noCR.length ? ': CR in ' + noCR.join(', ') : ''));
  // manifest
  const mpath = path.join(KIT, 'manifest', 'js-f1.tsv');
  const raw = fs.readFileSync(mpath, 'utf8'), rows = raw.replace(/\n$/, '').split('\n');
  say(rows[0] === 'path\tsha256\tsource', 'static  manifest/js-f1.tsv starts with the header row path<TAB>sha256<TAB>source');
  const listed = rows.slice(1).map(r => r.split('\t'));
  say(listed.every(r => r.length === 3 && /^[0-9a-f]{64}$/.test(r[1]) && r[2].length > 0), 'static  every manifest row has path, a 64-hex sha256 and a source (' + listed.length + ' rows)');
  say(listed.every(r => !r[0].startsWith('..') && !path.isAbsolute(r[0]) && !r[0].includes('\\') && fs.existsSync(path.join(KIT, r[0]))), 'static  every manifest path is inside the kit and exists');
  const absSrc = listed.filter(r => /(^|[^A-Za-z0-9_])[A-Za-z]:[\\/]/.test(r[2]) || /(^|\s)\/[A-Za-z0-9_.-]+\//.test(r[2])).map(r => r[0]);
  say(absSrc.length === 0, 'static  no manifest source names an absolute path (drive letter or leading slash)' + (absSrc.length ? ': ' + absSrc.join(', ') : ''));
  const wrong = listed.filter(r => fs.existsSync(path.join(KIT, r[0])) && sha(path.join(KIT, r[0])) !== r[1]).map(r => r[0]);
  say(wrong.length === 0, 'static  every manifest sha256 equals the file on disk' + (wrong.length ? ': STALE ' + wrong.join(', ') : ''));
  const owned = ['js-f1.html', 'lib/js-f1-probe.js', 'lib/js-f1-natives.js', 'lib/js-f1-host.js'];
  const missing = owned.filter(f => !listed.some(r => r[0] === f)), extraRows = listed.filter(r => !owned.includes(r[0])).map(r => r[0]);
  say(missing.length === 0 && extraRows.length === 0, 'static  the manifest lists exactly the 4 owned files' + (missing.length ? '; missing ' + missing.join(', ') : '') + (extraRows.length ? '; unexpected ' + extraRows.join(', ') : ''));
  say(!raw.includes('\r') && !/^﻿/.test(raw), 'static  manifest is LF, no BOM');
  say(!fs.existsSync(lib('js-f1-derive.js')), 'static  lib/js-f1-derive.js is not in the shipped kit (the tool lives in kit-tools/js-f1)');
}
function partDerive() {
  const r = cp.spawnSync(process.execPath, [path.join(__dirname, 'js-f1-derive.js'), '--check', '--kit', KIT], { encoding: 'utf8' });
  say(r.status === 0, 'derive  js-f1-derive.js --check: ' + (r.stdout || r.stderr || '').trim().split('\n')[0].replace(/\(f1probe.*$/, '').trim());
}

// ================================================================================================ shared machinery
const needNatives = () => { try { vm.runInThisContext('%GetOptimizationStatus(function () {})'); } catch (e) { throw new Error('run with --allow-natives-syntax (' + e.message + ')'); } };
let CONFIGS, probeSrc, hostSrc, kitSrc, configsSrc, F1Natives;
function loadSources() {
  configsSrc = read(lib('configs.js')); kitSrc = read(lib('kit.js')); hostSrc = read(lib('js-f1-host.js')); probeSrc = read(lib('js-f1-probe.js'));
  const c = { window: {} }; vm.runInNewContext(configsSrc, c); CONFIGS = c.window.KIT_CONFIGS;
}
function loadNatives() {
  vm.runInThisContext(read(lib('js-f1-natives.js')), { filename: 'js-f1-natives.js' });
  F1Natives = vm.runInThisContext('F1Natives');
}
// Node (V8 13.6) OptimizationStatus bits -> Chromium 150's (runtime.h): maybe-deopted, maglev, turbofan, interpreted, lite-mode, baseline
const TO_CHROMIUM = [[8, 4], [32, 16], [64, 32], [128, 64], [8192, 4096], [32768, 16384]];
const SHIFT = { 32: 64, 16: 32, 16384: 32768, 64: 128, 4: 8 };           // Chromium mask used by F1Natives.tier -> Node's
const toChromium = n => TO_CHROMIUM.reduce((o, [nb, cb]) => (n & nb) ? (o | cb) : o, 0);
function nodeNatives(over) {
  const shifted = F1Natives.tier.toString().replace(/\(s & (\d+)\)/g, (m, n) => '(s & ' + (SHIFT[n] || n) + ')');
  const inner = vm.runInThisContext('(' + shifted + ')');
  const tier = f => inner(f).replace(/\((\d+)\)$/, (m, n) => '(' + toChromium(+n) + ')');
  return Object.assign({}, F1Natives, { tier }, over || {});
}
const uad = (a, b, w) => ({ getHighEntropyValues: async () => { const o = {}; if (a !== undefined) o.architecture = a; if (b !== undefined) o.bitness = b; if (w !== undefined) o.wow64 = w; return o; } });
const CPU = {
  'ia32 on 64-bit Windows (x86/64/wow64 true: this server)': { uad: uad('x86', '64', true), ia32: true },
  'ia32 on 32-bit Windows (x86/32/false)': { uad: uad('x86', '32', false), ia32: true },
  'x64 (x86/64/false)': { uad: uad('x86', '64', false), ia32: false },
  'Windows ARM32 as Chromium reports it ("" /64/false)': { uad: uad('', '64', false), ia32: false },
  'arm/32': { uad: uad('arm', '32', false), ia32: false },
  'arm/64 (ARM64)': { uad: uad('arm', '64', false), ia32: false },
  'no navigator.userAgentData': { uad: undefined, ia32: false },
  'getHighEntropyValues rejects': { uad: { getHighEntropyValues: () => Promise.reject(new Error('boom')) }, ia32: false },
  'getHighEntropyValues never answers (3 s timeout)': { uad: { getHighEntropyValues: () => new Promise(() => {}) }, ia32: false },
};
const X64 = uad('x86', '64', false);                                       // what this (x64) Node pretends to be where the CPU does not matter

// the real lib/kit.js (+ configs.js, + a lib/build.js with `build`) in a stub DOM; the result of Kit.done() is the page's published result
function makeKit(o) {
  const sb = { setTimeout, clearTimeout, URLSearchParams, URL, Blob, atob, console };
  sb.window = sb; sb.parent = sb; sb.addEventListener = () => {};
  sb.location = { search: '?config=' + encodeURIComponent(o.config) + (o.armed ? '&armed=1' : ''), pathname: '/kit/js-f1.html' };
  sb.document = { body: null, head: null, title: '', addEventListener(ev, fn) { fn(); }, getElementById() { return {}; }, createElement() { return {}; } };
  sb.navigator = { userAgent: 'js-f1-nodetest (node ' + process.version + ')', userAgentData: o.uad };
  vm.createContext(sb);
  vm.runInContext(configsSrc, sb, { filename: 'configs.js' });
  vm.runInContext(o.build === undefined || o.build === null ? 'window.KIT_BUILD = null;' : 'window.KIT_BUILD = ' + JSON.stringify(o.build) + ';', sb, { filename: 'build.js' });
  vm.runInContext(kitSrc, sb, { filename: 'kit.js' });
  return sb.Kit;
}
// one run of the page's glue; o: {config, armed, build, uad, natives (wrapped natives or null = default), mutate, cfg (overrides), kitNatives}
async function runPage(o) {
  const Kit = makeKit(o);
  if (o.cfg) Kit.cfg = Object.assign({}, Kit.cfg, o.cfg);
  if (o.kitNatives !== undefined) Kit.natives = o.kitNatives;
  const g = vm.runInThisContext('(function(){' + hostSrc + '; return F1Host; })()');            // a fresh F1Host (and its `extra`) per run
  let src = probeSrc;
  if (o.mutate) src = src.replace('function S3(a, b, c) { return [this, arguments.length, a, b, c]; }',
    'function S3(a, b, c) { if (arguments.length === 2) return [this, 2, this, a, b]; return [this, arguments.length, a, b, c]; }');   // F1: [global],1,2
  globalThis.F1_MODE = undefined;
  const probe = vm.runInThisContext(src + '\n;F1Probe');
  await g.run(Kit, { probe, natives: Kit.natives ? (o.natives || nodeNatives()) : null });
  const r = await Kit.done(g.extra);                                                            // what the page publishes
  r.bad = r.checks.filter(c => !c.ok); r.na = r.checks.filter(c => c.na); r.real = r.checks.filter(c => !c.info && !c.na);
  return r;
}
const names = r => r.bad.map(c => c.name);
const PLANTED = /^sloppy plain 2of3 \| params$/;
const sum = r => r.verdict + ' real ' + r.real.length + ' bad ' + r.bad.length + ' n/a ' + r.na.length + ' tier [' + short(r.tier, 70) + ']' + (r.bad.length ? ' {' + short(names(r).join('; '), 150) + '}' : '');

// ======================================================================================================== config
function runOriginal(mode) {
  const lines = [];
  globalThis.print = s => lines.push(String(s)); globalThis.F1_MODE = mode;
  vm.runInThisContext(read(F1PROBE), { filename: 'f1probe.js' });
  return lines;
}
function runDerived(mode) {
  const lines = [];
  globalThis.F1_MODE = mode;
  const probe = vm.runInThisContext(probeSrc + '\n;F1Probe', { filename: 'js-f1-probe.js' });
  probe({ print: s => lines.push(String(s)), tier: F1Natives.tier, warmup: F1Natives.warmup });
  return lines;
}
async function partConfig(config) {
  needNatives(); loadSources(); loadNatives();
  const cfg = CONFIGS[config], wantTier = cfg.tier || null;
  console.log('node ' + process.version + ' v8 ' + process.versions.v8 + ' ' + process.arch + '; config ' + config + ' flags ' + (cfg.flags.join(' ') || '(none)') + '; %IsMaglevEnabled() = ' + F1Natives.maglevEnabled());
  // ---- A. the derived probe is the original probe (every CHECK / TIER / SUMMARY line identical, with this configuration's flags) ----
  { const mode = wantTier ? config : 'default';
    const a = runOriginal(mode), b = runDerived(mode);
    const same = a.length === b.length && a.every((l, i) => l === b[i]);
    say(same, 'A  ' + config.padEnd(16) + ' original f1probe.js ' + a.length + ' lines, derived F1Probe ' + b.length + ' lines, ' + (same ? 'identical' : 'DIFFER'));
    if (!same) for (let i = 0; i < Math.max(a.length, b.length); i++) if (a[i] !== b[i]) { console.log('   first diff @' + i + '\n   orig: ' + a[i] + '\n   deri: ' + b[i]); break; } }
  // ---- B. the glue, real Kit; this Node is an x64 browser pretending: no build record, CPU x86/64/false ----
  const base = { config, uad: X64 };
  const wantWord = wantTier ? 'proven ' + wantTier : 'n/a (configuration selects no JS tier)';
  const n = await runPage(base);
  say(n.verdict === 'PASS' && n.bad.length === 0 && n.tier.startsWith(wantWord), 'B  ' + config.padEnd(16) + ' normal: ' + sum(n));
  const a = await runPage(Object.assign({ armed: true }, base));
  say(a.verdict === 'FAIL' && a.planted === true && a.bad.length === 1 && PLANTED.test(a.bad[0].name), 'B  ' + config.padEnd(16) + ' ARMED: ' + sum(a) + ' planted ' + a.planted);
  const m = await runPage(Object.assign({ mutate: true }, base));
  say(m.verdict === 'FAIL' && m.f1PatternSeen === true && names(m).some(x => /F1 pattern/.test(x)) && names(m).some(x => PLANTED.test(x)), 'B  ' + config.padEnd(16) + ' F1 MUTANT in the probe: ' + sum(m) + ' f1PatternSeen ' + m.f1PatternSeen);
  if (wantTier) {
    const who = cfg.who, nTier = who === 'caller' ? 24 : 6;
    // a wrong tier wanted: the same functions, another tier -> every tier row bad
    const other = wantTier === 'sparkplug' ? 'turbofan' : 'sparkplug';
    const w = await runPage(Object.assign({ cfg: { tier: other } }, base));
    const rows = w.checks.filter(c => /^tier proof (callee|caller) /.test(c.name));
    say(w.verdict === 'FAIL' && rows.length === nTier && rows.every(c => !c.ok), 'B  ' + config.padEnd(16) + ' wrong tier wanted (' + other + '): FAIL, ' + rows.length + ' tier rows, bad ' + rows.filter(c => !c.ok).length);
    // a named tier with natives syntax off in the configuration is a failed check, never an n/a
    const o = await runPage(Object.assign({ kitNatives: false }, base));
    say(o.verdict === 'FAIL' && names(o).some(x => /^tier proof: /.test(x)) && o.na.length === 0, 'B  ' + config.padEnd(16) + ' natives off with a named tier: ' + sum(o));
    // the natives script cannot be used (the glue gets none): FAIL, not n/a
    // (nothing to run in the page: Kit.loadScript of the stub DOM rejects, as a missing / unloadable lib/js-f1-natives.js would)
    const Kit0 = makeKit(base); const g0 = vm.runInThisContext('(function(){' + hostSrc + '; return F1Host; })()');
    globalThis.F1_MODE = undefined; await g0.run(Kit0, { probe: vm.runInThisContext(probeSrc + '\n;F1Probe') });
    const r0 = await Kit0.done(g0.extra);
    say(r0.verdict === 'FAIL' && r0.checks.some(c => !c.ok && /natives-only script loaded/.test(c.name)) && r0.checks.some(c => !c.ok && /^tier proof: /.test(c.name)), 'B  ' + config.padEnd(16) + ' natives script not loadable: FAIL (' + short(r0.checks.filter(c => !c.ok).map(c => c.name).join('; '), 140) + ')');
  }
  // ---- B2. the flags reached V8 and the lite-mode bit, from the configuration's flags only: wrapped natives that claim the wrong thing must FAIL ----
  const claim = (over, why) => runPage(Object.assign({ natives: nodeNatives(over) }, base)).then(r => ({ r, why }));
  const fl = f => cfg.flags.includes(f);
  const jobs = [];
  if (fl('--jitless')) jobs.push(claim({ sparkplugEnabled: () => true }, 'sparkplug claimed on under --jitless'), claim({ turbofanEnabled: () => true }, 'turbofan claimed on under --jitless'),
    claim({ maglevEnabled: () => true }, 'maglev claimed on under --jitless'));
  if (fl('--no-turbofan')) jobs.push(claim({ turbofanEnabled: () => true }, 'turbofan claimed on under --no-turbofan'));
  if (fl('--no-sparkplug')) jobs.push(claim({ sparkplugEnabled: () => true }, 'sparkplug claimed on under --no-sparkplug'));
  if (fl('--no-maglev')) jobs.push(claim({ maglevEnabled: () => true }, 'maglev claimed on under --no-maglev'));
  if (fl('--always-sparkplug')) jobs.push(claim({ sparkplugEnabled: () => false }, 'sparkplug claimed off under --always-sparkplug'));
  for (const { r, why } of await Promise.all(jobs)) say(r.verdict === 'FAIL' && names(r).some(x => /^flags reached V8: /.test(x)), 'B2 ' + config.padEnd(16) + ' ' + why + ': ' + sum(r));
  if (wantTier === 'interpreter') {
    const wantLite = fl('--jitless');
    const wrong = await runPage(Object.assign({ natives: nodeNatives({ tier: f => nodeNatives().tier(f).replace(/\((\d+)\)$/, (m, v) => '(' + (wantLite ? (+v & ~4096) : (+v | 4096)) + ')') }) }, base));
    const lite = wrong.checks.filter(c => /^lite-mode bit /.test(c.name));
    say(wrong.verdict === 'FAIL' && lite.length === 6 && lite.every(c => !c.ok), 'B2 ' + config.padEnd(16) + ' lite-mode bit ' + (wantLite ? 'cleared' : 'set') + ' by the wrapped natives: ' + sum(wrong));
    const right = n.checks.filter(c => /^lite-mode bit /.test(c.name));
    say(right.length === 6 && right.every(c => c.ok && c.want === (wantLite ? 'set' : 'clear')), 'B2 ' + config.padEnd(16) + ' lite-mode bit rows in the normal run: ' + right.length + ' rows, want ' + (right[0] && right[0].want) + ', got ' + (right[0] && right[0].got));
  }
}

// ======================================================================================================== matrix
async function partMatrix(kind) {
  needNatives(); loadSources(); loadNatives();
  const present = kind === 'present';
  const maglevNow = !!F1Natives.maglevEnabled();
  console.log('node ' + process.version + ' v8 ' + process.versions.v8 + ' ' + process.arch + '; matrix ' + kind + '; %IsMaglevEnabled() = ' + maglevNow);
  say(maglevNow === present, 'M  this process has Maglev ' + (present ? 'present' : 'absent (--no-maglev)') + ': %IsMaglevEnabled() = ' + maglevNow);
  const nat = nodeNatives();
  const BUILDS = {
    'no build record (the record\'s copy)': null,
    'record x86 / has_maglev false': { v8_current_cpu: 'x86', has_maglev: false, source: 'nodetest' },
    'record arm / has_maglev true': { v8_current_cpu: 'arm', has_maglev: true, source: 'nodetest' },
    'record arm without has_maglev': { v8_current_cpu: 'arm', source: 'nodetest' },
    'record with has_maglev "false" (a string)': { v8_current_cpu: 'x86', has_maglev: 'false' },
  };
  for (const config of ['maglev', 'maglev-caller']) {
    const who = CONFIGS[config].who, nTier = who === 'caller' ? 24 : 6;
    const row = async (label, o, expect) => {
      const r = await runPage(Object.assign({ config }, o));
      const okIf = expect(r);
      say(okIf, 'M  ' + config.padEnd(13) + ' ' + label.padEnd(100) + ' ' + sum(r));
      return r;
    };
    const PASS_NA = r => r.verdict === 'PASS' && r.bad.length === 0 && /^n\/a \(no Maglev in this build/.test(r.tier) && r.na.length === 1 && r.tierProof === 'n/a';
    const PASS_PROVEN = r => r.verdict === 'PASS' && r.bad.length === 0 && r.tier === 'proven maglev (' + who + 's, ' + nTier + ' functions)' && r.na.length === 0;
    const FAIL_ONLY = rx => r => r.verdict === 'FAIL' && r.bad.length === 1 && rx.test(r.bad[0].name) && r.tier === 'NOT PROVEN';
    // ---- no build record: the reported CPU decides (the 9 variants) ----
    for (const [label, c] of Object.entries(CPU)) {
      if (present) await row(label + ' | no record | Maglev present -> proven', { uad: c.uad }, PASS_PROVEN);
      else if (c.ia32) await row(label + ' | no record | no Maglev -> PASS n/a', { uad: c.uad }, PASS_NA);
      else await row(label + ' | no record | no Maglev -> FAIL', { uad: c.uad }, FAIL_ONLY(/^tier proof: maglev \(a missing Maglev is n\/a only where the build has none\)$/));
    }
    // ---- with a build record: it decides, the CPU does not ----
    const B = BUILDS;
    if (!present) {
      await row('record x86/has_maglev false | no userAgentData | no Maglev -> PASS n/a', { uad: undefined, build: B['record x86 / has_maglev false'] }, PASS_NA);
      await row('record x86/has_maglev false | CPU reported as ARM32 | no Maglev -> PASS n/a', { uad: CPU['Windows ARM32 as Chromium reports it ("" /64/false)'].uad, build: B['record x86 / has_maglev false'] }, PASS_NA);
      // has_maglev false is believable only for v8_current_cpu "x86" (V8 builds Maglev for every other CPU): a record naming any other CPU (or none)
      // is a FAILED check whatever the browser reports, with the sentence "the build record says <cpu> has no Maglev, but V8 builds Maglev for <cpu>"
      const IA32 = CPU['ia32 on 64-bit Windows (x86/64/wow64 true: this server)'].uad, ARM32 = CPU['Windows ARM32 as Chromium reports it ("" /64/false)'].uad;
      const FALSE_REC = c => (c === undefined ? { has_maglev: false, source: 'nodetest' } : { v8_current_cpu: c, has_maglev: false, source: 'nodetest' });
      const FAIL_REC = cpuName => r => FAIL_ONLY(/^tier proof: maglev \(a missing Maglev is n\/a only where the build has none\)$/)(r) && (cpuName === undefined
        ? r.bad[0].got.includes('the build record says has_maglev false but names no v8_current_cpu')
        : r.bad[0].got.includes('the build record says ' + cpuName + ' has no Maglev, but V8 builds Maglev for ' + cpuName));
      for (const [cpuName, uadLabel, u] of [['arm', 'CPU reported as ARM32', ARM32], ['arm', 'CPU claims ia32', IA32], ['arm', 'no userAgentData', undefined],
        ['arm64', 'CPU claims ia32', IA32], ['x64', 'CPU claims ia32', IA32], ['ia32', 'CPU claims ia32', IA32], ['X86', 'CPU claims ia32', IA32], ['', 'CPU claims ia32', IA32], [undefined, 'CPU claims ia32', IA32]]) {
        await row('record ' + (cpuName === undefined ? '(no v8_current_cpu)' : JSON.stringify(cpuName)) + '/has_maglev false | ' + uadLabel + ' | no Maglev -> FAIL (only x86 may lack Maglev)',
          { uad: u, build: FALSE_REC(cpuName) }, FAIL_REC(cpuName === '' ? undefined : cpuName));
      }
      await row('record arm/has_maglev true | CPU claims ia32 | no Maglev -> FAIL (record wins)', { uad: CPU['ia32 on 64-bit Windows (x86/64/wow64 true: this server)'].uad, build: B['record arm / has_maglev true'] },
        r => r.verdict === 'FAIL' && r.bad.length === 2 && r.tier === 'NOT PROVEN' && names(r).some(x => /^build record \(has_maglev\) vs V8: maglev enabled$/.test(x)) && names(r).some(x => /^tier proof: maglev \(a missing/.test(x)));
      await row('record arm/has_maglev true | ARM32 | no Maglev -> FAIL', { uad: CPU['Windows ARM32 as Chromium reports it ("" /64/false)'].uad, build: B['record arm / has_maglev true'] },
        r => r.verdict === 'FAIL' && r.bad.length === 2 && r.tier === 'NOT PROVEN');
      await row('record arm without has_maglev | CPU claims ia32 | no Maglev -> FAIL (unknown record is not n/a)', { uad: CPU['ia32 on 64-bit Windows (x86/64/wow64 true: this server)'].uad, build: B['record arm without has_maglev'] },
        FAIL_ONLY(/^tier proof: maglev \(a missing/));
      await row('record has_maglev "false" (string) | CPU claims ia32 | no Maglev -> FAIL (only boolean false counts)', { uad: CPU['ia32 on 64-bit Windows (x86/64/wow64 true: this server)'].uad, build: B['record with has_maglev "false" (a string)'] },
        FAIL_ONLY(/^tier proof: maglev \(a missing/));
      // armed: exactly the planted row fails, in the n/a case and in the FAIL-free case alike
      await row('ARMED | CPU ia32, no record | no Maglev -> exactly the planted row bad', { uad: CPU['ia32 on 64-bit Windows (x86/64/wow64 true: this server)'].uad, armed: true },
        r => r.verdict === 'FAIL' && r.planted && r.bad.length === 1 && PLANTED.test(r.bad[0].name) && /^n\/a/.test(r.tier));
    } else {
      await row('record x86/has_maglev false | Maglev present -> FAIL (kit/build mismatch), proof branch still runs', { uad: X64, build: B['record x86 / has_maglev false'] },
        r => r.verdict === 'FAIL' && r.bad.length === 1 && /^build record \(has_maglev\) vs V8: maglev enabled$/.test(r.bad[0].name) && r.tier === 'proven maglev (' + who + 's, ' + nTier + ' functions)');
      await row('record arm/has_maglev false | Maglev present -> FAIL (kit/build mismatch), proof branch still runs', { uad: X64, build: { v8_current_cpu: 'arm', has_maglev: false, source: 'nodetest' } },
        r => r.verdict === 'FAIL' && r.bad.length === 1 && /^build record \(has_maglev\) vs V8: maglev enabled$/.test(r.bad[0].name) && r.tier === 'proven maglev (' + who + 's, ' + nTier + ' functions)');
      await row('record arm/has_maglev true | CPU ARM32 | Maglev present -> proven', { uad: CPU['Windows ARM32 as Chromium reports it ("" /64/false)'].uad, build: B['record arm / has_maglev true'] }, PASS_PROVEN);
      await row('record arm without has_maglev | Maglev present -> proven', { uad: X64, build: B['record arm without has_maglev'] }, PASS_PROVEN);
      await row('ARMED | ARM32 + record arm/true | Maglev present -> exactly the planted row bad', { uad: CPU['Windows ARM32 as Chromium reports it ("" /64/false)'].uad, build: B['record arm / has_maglev true'], armed: true },
        r => r.verdict === 'FAIL' && r.planted && r.bad.length === 1 && PLANTED.test(r.bad[0].name) && /^proven maglev/.test(r.tier));
      // Maglev claimed present, tier not reached: the warm-up does nothing, so the functions stay in the interpreter
      const noWarm = nodeNatives({ warmup: () => {}, maglevEnabled: () => true });
      await row('Maglev CLAIMED present (%IsMaglevEnabled() true) but the warm-up is a no-op -> FAIL, NOT PROVEN, ' + nTier + ' tier rows bad', { uad: CPU['Windows ARM32 as Chromium reports it ("" /64/false)'].uad, build: B['record arm / has_maglev true'], natives: noWarm },
        r => r.verdict === 'FAIL' && r.tier === 'NOT PROVEN' && r.checks.filter(c => /^tier proof (callee|caller) /.test(c.name) && !c.ok).length === nTier);
    }
  }
}

// ======================================================================================================== gate / dispatch
function child(args, nodeFlags) {
  return new Promise(res => {
    let out = ''; const p = cp.spawn(process.execPath, [...(nodeFlags || []), __filename, '--kit', KIT, ...args], { stdio: ['ignore', 'pipe', 'pipe'] });
    p.stdout.on('data', d => out += d); p.stderr.on('data', d => out += d);
    p.on('close', code => res({ code, out }));
  });
}
async function gate() {
  loadSources();
  const parts = [['static', ['--static'], []], ['derive', ['--derive'], []]];
  for (const c of Object.keys(CONFIGS)) parts.push(['config:' + c, ['--config', c], ['--allow-natives-syntax', ...CONFIGS[c].flags.filter(f => f !== '--allow-natives-syntax')]]);
  parts.push(['matrix:absent', ['--matrix', 'absent'], ['--allow-natives-syntax', '--no-maglev']], ['matrix:present', ['--matrix', 'present'], ['--allow-natives-syntax']]);
  const only = (opt('only', '') || '').split(',').filter(Boolean);
  const todo = parts.filter(p => !only.length || only.includes(p[0]));
  if (!todo.length) { console.log('nothing selected; parts: ' + parts.map(p => p[0]).join(' ')); process.exit(2); }
  const results = new Array(todo.length); let next = 0;
  await Promise.all([0, 1, 2, 3].map(async () => { while (next < todo.length) { const i = next++; results[i] = await child(todo[i][1], todo[i][2]); } }));
  let bad = 0;
  todo.forEach((p, i) => { const r = results[i]; console.log('---- ' + p[0] + ' (exit ' + r.code + ')'); console.log(r.out.trimEnd()); if (r.code !== 0 || /^FAIL /m.test(r.out)) bad++; });
  console.log('\n' + (bad ? bad + ' of ' + todo.length + ' parts FAILED' : 'all ' + todo.length + ' rehearsal parts ok'));
  process.exit(bad ? 1 : 0);
}
(async () => {
  if (argv.includes('--static')) partStatic();
  else if (argv.includes('--derive')) partDerive();
  else if (opt('config')) await partConfig(opt('config'));
  else if (opt('matrix')) await partMatrix(opt('matrix'));
  else return gate();
  console.log(fails ? ('\n' + fails + ' FAILED') : '\nall ok');
  process.exit(fails ? 1 : 0);
})().catch(e => { console.log('FAIL rehearsal crashed: ' + (e.stack || e)); process.exit(1); });
