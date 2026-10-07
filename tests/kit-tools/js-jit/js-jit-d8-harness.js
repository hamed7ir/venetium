// js-jit kit tool (server only, not shipped): a d8 replay of js-jit.html's REAL inline script. The script is extracted from the page at
// run time (not retyped) and evaluated against a stub DOM, a stub navigator (the CPU the browser would report), a stub lib/build.js
// record, and a stub Kit that has kit.js's own check / info / na / want / done semantics. Kit.arch is kit.js's own code too: the
// `async arch() {...}` method is cut out of lib/kit.js and evaluated, so the ia32 rule under test is the shipped one.
// It is NOT the browser and NOT real ARM32 hardware: a d8 (ia32 build, or the ARM simulator build with Maglev) running the page's logic.
//   d8 <config flags> js-jit-d8-harness.js -- <config> <armed 0|1> <arch> <build> <kitdir>
//     arch : x86 (x86/64/wow64 true: the ia32 browser on this server) | x86-32 (x86/32/false) | x64native (x86/64/false) |
//            arm32 (architecture '' / 64 / false: what Windows ARM32 reports) | arm64 | nouad (no navigator.userAgentData) |
//            hang (getHighEntropyValues never settles) | reject (it rejects)
//     build: none (window.KIT_BUILD null: the record copy) | arm ({v8_current_cpu:'arm', has_maglev:true}) |
//            x86 ({v8_current_cpu:'x86', has_maglev:false}) | junk (a record without a boolean has_maglev) |
//            armfalse ({v8_current_cpu:'arm', has_maglev:false}: a record that says an ARM build has no Maglev) |
//            x64false ({v8_current_cpu:'x64', has_maglev:false}) | nocpufalse ({has_maglev:false}, no v8_current_cpu)
//   Prints one RESULT line (verdict, check counts, tier label, tier mode) and one BAD line per failed check. js-jit-d8-matrix.js drives it.
var __print = print;
var __args = (typeof arguments !== 'undefined') ? arguments : (typeof scriptArgs !== 'undefined' ? scriptArgs : []);
var CONFIG = __args[0] || 'default', ARMED = __args[1] === '1', ARCHMODE = __args[2] || 'x86', BUILDMODE = __args[3] || 'none';
var KITDIR = __args[4];
if (!KITDIR) throw new Error('usage: d8 <flags> js-jit-d8-harness.js -- <config> <armed> <arch> <build> <kitdir with trailing slash>');
globalThis.window = globalThis;
globalThis.location = { search: '?config=' + CONFIG + (ARMED ? '&armed=1' : '') };
var UAD = {
  x86:       { architecture: 'x86', bitness: '64', wow64: true },
  'x86-32':  { architecture: 'x86', bitness: '32', wow64: false },
  x64native: { architecture: 'x86', bitness: '64', wow64: false },
  arm32:     { architecture: '',    bitness: '64', wow64: false },
  arm64:     { architecture: 'arm', bitness: '64', wow64: false },
}[ARCHMODE];
globalThis.navigator = { userAgent: 'd8-harness/' + ARCHMODE };
if (ARCHMODE === 'hang') navigator.userAgentData = { getHighEntropyValues: function () { return new Promise(function () {}); } };
else if (ARCHMODE === 'reject') navigator.userAgentData = { getHighEntropyValues: function () { return Promise.reject(new Error('stub reject')); } };
else if (UAD) navigator.userAgentData = { getHighEntropyValues: function () { return Promise.resolve(Object.assign({}, UAD)); } };
globalThis.KIT_BUILD = { none: null,
  arm: { v8_current_cpu: 'arm', has_maglev: true, source: 'harness stub' },
  x86: { v8_current_cpu: 'x86', has_maglev: false, source: 'harness stub' },
  junk: { v8_current_cpu: 'arm', has_maglev: 'yes', source: 'harness stub' },
  armfalse: { v8_current_cpu: 'arm', has_maglev: false, source: 'harness stub' },
  x64false: { v8_current_cpu: 'x64', has_maglev: false, source: 'harness stub' },
  nocpufalse: { has_maglev: false, source: 'harness stub' } }[BUILDMODE];
if (KIT_BUILD === undefined) throw new Error('unknown build mode ' + BUILDMODE);
var rawEl = { textContent: '' };
globalThis.document = { getElementById: function () { return rawEl; } };
// timers: an own registry, pumped from d8's task queue, so a wait that never settles is rejected by its timeout without a real delay
var __d8ST = setTimeout;
var timers = new Map(), tid = 0;
globalThis.setTimeout = function (f, ms) { var id = ++tid; timers.set(id, { f: f, ms: ms || 0 }); return id; };
globalThis.clearTimeout = function (id) { timers.delete(id); };
load(KITDIR + 'lib/configs.js');
load(KITDIR + 'data/js-jit-expected.js');
var cfg = KIT_CONFIGS[CONFIG] || { flags: [], natives: false };
var checks = [], planted = false, finished = false, result = null;
var Kit = {
  config: CONFIG, armed: ARMED, cfg: cfg, natives: !!cfg.natives, build: KIT_BUILD || null,
  want: function (v, wrong) { if (ARMED && !planted) { planted = true; return wrong; } return v; },
  check: function (name, want, got, ok) { checks.push({ name: name, want: String(want), got: String(got), ok: !!ok }); return !!ok; },
  info: function (name, got) { checks.push({ name: name, want: '(info)', got: String(got), ok: true, info: true }); },
  na: function (name, why) { checks.push({ name: name, want: 'n/a', got: String(why), ok: true, na: true }); },
  // a browser fires the <script> load event after a SyntaxError or an exception in the script too (the error goes to window.onerror);
  // d8's load() throws a generic Error for those, so a file that can be read counts as loaded; a missing file is the only real failure
  loadScript: function (src) {
    return new Promise(function (res, rej) {
      try { read(KITDIR + src); } catch (e) { rej(new Error('cannot load ' + src + ': ' + e)); return; }
      try { load(KITDIR + src); } catch (e) { /* SyntaxError (natives syntax rejected) or a throwing script: the load event fires anyway */ }
      res();
    });
  },
  done: async function (extra) {
    finished = true;
    var real = checks.filter(function (c) { return !c.info && !c.na; });
    if (ARMED && !planted) Kit.check('ARMED RUN: this page planted no wrong expectation (page bug)', 'planted', 'none', false);
    var verdict = real.length > 0 && checks.every(function (c) { return c.ok; }) ? 'PASS' : 'FAIL';
    result = Object.assign({ config: CONFIG, armed: ARMED, planted: planted, checks: checks, verdict: verdict }, extra || {});
    var bad = checks.filter(function (c) { return !c.ok; });
    __print('RESULT config=' + CONFIG + ' armed=' + ARMED + ' arch=' + ARCHMODE + ' build=' + BUILDMODE + ' verdict=' + verdict + ' checks=' + checks.length +
            ' bad=' + bad.length + ' planted=' + planted + ' tier="' + result.tier + '" mode=' + result.tierMode + ' proof=' + result.tierProof);
    for (var i = 0; i < bad.length; i++) __print('  BAD: ' + bad[i].name + ' | want=' + bad[i].want.slice(0, 80) + ' | got=' + bad[i].got.slice(0, 260));
    if (result.tierNa) __print('  NA: ' + String(result.tierNa).slice(0, 200));
    if (result.switches) __print('  probe=' + JSON.stringify(result.switches) + ' arch=' + JSON.stringify(result.arch && { architecture: result.arch.architecture, ia32: result.arch.ia32, note: result.arch.note }));
    return result;
  },
};
globalThis.Kit = Kit;
// Kit.arch: kit.js's own method, cut out of the shipped file (a missing match means kit.js changed shape: fix this tool, do not retype the rule)
var kitSrc = read(KITDIR + 'lib/kit.js');
var am = kitSrc.match(/\n    async arch\(\) \{[\s\S]*?\n    \},\n(?=    async done)/);
if (!am) throw new Error('async arch() not found in lib/kit.js');
Kit.arch = (0, eval)('({' + am[0] + '})').arch;
// the page's inline script (the one <script> without src)
var html = read(KITDIR + 'js-jit.html');
var m = html.match(/<script>\n([\s\S]*?)<\/script>/);
if (!m) throw new Error('inline script not found in js-jit.html');
(0, eval)(m[1]);
// pump: fire the registered timers (shortest first) only while the page has not finished
function pump() {
  if (finished || timers.size === 0) return;
  var best = null;
  timers.forEach(function (t, id) { if (!best || t.ms < best.t.ms) best = { id: id, t: t }; });
  timers.delete(best.id); best.t.f();
  __d8ST(pump, 0);
}
__d8ST(pump, 0);
