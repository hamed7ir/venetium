// js-jit kit tool (server only, not shipped): runs js-jit-d8-harness.js (the page's real inline script under a d8) over a matrix of
// configurations x reported CPUs x build records x mutated launch flags, and compares every result with the verdict the page MUST give.
// Exit code 0 only when every case came out as expected. Not the browser, not real ARM32: an ia32 d8 (no Maglev) and the ARM-simulator d8
// (Maglev built, V8 arm32 code generator) standing in for the two V8 situations; the architecture is a stub.
//   node js-jit-d8-matrix.js [--kit <kit folder>] [--arm-d8 <d8.exe of the ARM simulator>] [--ia32-d8 <d8.exe of the ia32 build>] [--only <id prefix>]
// Defaults: the kit next to this tools folder, F:\cr\src\out\x86-arm-sim\d8.exe and F:\cr\src\out\x86-rt21\d8.exe.
'use strict';
const fs = require('fs'), path = require('path'), vm = require('vm'), { spawnSync } = require('child_process');
const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf('--' + n); return i >= 0 ? args[i + 1] : d; };
const KIT = path.resolve(opt('kit', path.join(__dirname, '..', '..', 'kit'))).replace(/\\/g, '/') + '/';
const D8 = { arm: opt('arm-d8', 'F:/cr/src/out/x86-arm-sim/d8.exe'), ia32: opt('ia32-d8', 'F:/cr/src/out/x86-rt21/d8.exe') };
const ONLY = opt('only', '');
const ctx = { window: {} }; vm.runInNewContext(fs.readFileSync(KIT + 'lib/configs.js', 'utf8'), ctx);
const C = ctx.window.KIT_CONFIGS, NAMES = Object.keys(C);
const IGN = ['--no-sparkplug', '--no-maglev', '--no-turbofan', '--allow-natives-syntax'];
const cases = [];
// expect: {v: verdict, bad: exact number of failed checks (omit = any), re: regexes that must each match some failed check name,
//          got: regexes that must each match the got text of some failed check, tier: regex for the tier label}
const add = (id, d8, cfg, armed, arch, build, expect, flags) => cases.push({ id, d8, cfg, armed, arch, build, expect, flags });
const TIER = { default: /^n\/a \(natives off\)/, 'liftoff-only': /^n\/a \(this configuration selects no JS tier\)/, 'no-liftoff': /^n\/a \(this configuration selects no JS tier\)/,
  jitless: /^interpreter 13\/13 \(lite-mode set\)/, ignition: /^interpreter 13\/13 \(lite-mode clear\)/, sparkplug: /^sparkplug 13\/13/,
  turbofan: /^turbofan 13\/13/, 'turbofan-caller': /^turbofan 13\/13/ };
for (const n of NAMES) {
  const maglev = C[n].tier === 'maglev';
  // A: ARM simulator d8 (Maglev built), the CPU Windows ARM32 reports, no build record
  add('A-' + n, 'arm', n, false, 'arm32', 'none', { v: 'PASS', bad: 0, tier: maglev ? /^maglev 13\/13/ : TIER[n] });
  add('A-' + n + '-armed', 'arm', n, true, 'arm32', 'none', { v: 'FAIL', bad: 1, re: [/answer overflow/] });
  // B: ia32 d8 (no Maglev), the CPU the ia32 browser on this server reports
  add('B-' + n, 'ia32', n, false, 'x86', 'none', { v: 'PASS', bad: 0, tier: maglev ? /^n\/a \(no Maglev on ia32\)/ : TIER[n] });
  add('B-' + n + '-armed', 'ia32', n, true, 'x86', 'none', { v: 'FAIL', bad: 1, re: [/answer overflow/] });
}
for (const n of ['maglev', 'maglev-caller']) {
  add('A2-' + n + '-buildarm', 'arm', n, false, 'arm32', 'arm', { v: 'PASS', bad: 0, tier: /^maglev 13\/13/ });
  add('B2-' + n + '-buildx86', 'ia32', n, false, 'x86', 'x86', { v: 'PASS', bad: 0, tier: /^n\/a \(no Maglev on ia32\)/ });
}
add('B3-ia32-bitness32', 'ia32', 'maglev', false, 'x86-32', 'none', { v: 'PASS', bad: 0, tier: /^n\/a \(no Maglev on ia32\)/ });
add('B4-build-decides-no-uad', 'ia32', 'maglev', false, 'nouad', 'x86', { v: 'PASS', bad: 0, tier: /^n\/a \(no Maglev on ia32\)/ });
// C: Maglev missing where it must exist, or a doubtful CPU report: every one a FAILURE
const NOMAGLEV = [/Maglev is available/];
add('C1-arm32-maglev-off', 'arm', 'maglev', false, 'arm32', 'none', { v: 'FAIL', bad: 14, re: NOMAGLEV, tier: /^NOT PROVEN/ }, ['--allow-natives-syntax', '--no-maglev']);
add('C1b-arm32-maglev-off-caller', 'arm', 'maglev-caller', false, 'arm32', 'none', { v: 'FAIL', bad: 14, re: NOMAGLEV }, ['--allow-natives-syntax', '--no-maglev-inlining', '--no-maglev']);
add('C1c-arm32-maglev-off-buildarm', 'arm', 'maglev', false, 'arm32', 'arm', { v: 'FAIL', bad: 14, re: NOMAGLEV }, ['--allow-natives-syntax', '--no-maglev']);
add('C1d-spoofed-x86-maglev-off-no-build (documented limit: userAgentData is the only source)', 'arm', 'maglev', false, 'x86', 'none', { v: 'PASS', bad: 0, tier: /^n\/a/ }, ['--allow-natives-syntax', '--no-maglev']);
add('C1e-spoofed-x86-maglev-off-buildarm (the build record closes it)', 'arm', 'maglev', false, 'x86', 'arm', { v: 'FAIL', bad: 14, re: NOMAGLEV }, ['--allow-natives-syntax', '--no-maglev']);
add('C2-ia32-claims-arm32', 'ia32', 'maglev', false, 'arm32', 'none', { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('C3-ia32-claims-arm64', 'ia32', 'maglev', false, 'arm64', 'none', { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('C4-x64-like-maglev-off', 'ia32', 'maglev', false, 'x64native', 'none', { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('C5a-no-userAgentData', 'ia32', 'maglev', false, 'nouad', 'none', { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('C5b-getHighEntropyValues-hangs', 'ia32', 'maglev', false, 'hang', 'none', { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('C5c-getHighEntropyValues-rejects', 'ia32', 'maglev', false, 'reject', 'none', { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('C5d-junk-build-record-falls-back-to-arch', 'ia32', 'maglev', false, 'x86', 'junk', { v: 'PASS', bad: 0, tier: /^n\/a/ });
add('C6-ia32-build-says-arm', 'ia32', 'maglev', false, 'x86', 'arm', { v: 'FAIL', bad: 14, re: NOMAGLEV });
add('C6b-ia32-build-says-arm-caller', 'ia32', 'maglev-caller', false, 'x86', 'arm', { v: 'FAIL', bad: 14, re: NOMAGLEV });
// a build record grants n/a only with has_maglev false AND v8_current_cpu 'x86' (B2 / B4 above): has_maglev false for a CPU V8 builds Maglev for
// (arm, x64, none named) is a FAILURE whatever the browser reports (BATCH-DEVICE-1 follow-up)
const SAYS = (cpu) => [new RegExp('the build record says ' + cpu + ' has no Maglev, but V8 builds Maglev for ' + cpu)];
add('C6c-build-says-arm-has-no-maglev (ia32 reports x86)', 'ia32', 'maglev', false, 'x86', 'armfalse', { v: 'FAIL', bad: 14, re: NOMAGLEV, got: SAYS('arm'), tier: /^NOT PROVEN/ });
add('C6d-build-says-arm-has-no-maglev-caller', 'ia32', 'maglev-caller', false, 'x86', 'armfalse', { v: 'FAIL', bad: 14, re: NOMAGLEV, got: SAYS('arm') });
add('C6e-build-says-arm-has-no-maglev (ARM32 report)', 'ia32', 'maglev', false, 'arm32', 'armfalse', { v: 'FAIL', bad: 14, re: NOMAGLEV, got: SAYS('arm') });
add('C6f-build-says-arm-has-no-maglev (no userAgentData)', 'ia32', 'maglev', false, 'nouad', 'armfalse', { v: 'FAIL', bad: 14, re: NOMAGLEV, got: SAYS('arm') });
add('C6g-build-says-x64-has-no-maglev', 'ia32', 'maglev', false, 'x86', 'x64false', { v: 'FAIL', bad: 14, re: NOMAGLEV, got: SAYS('x64') });
add('C6h-build-names-no-cpu-has-no-maglev', 'ia32', 'maglev', false, 'x86', 'nocpufalse', { v: 'FAIL', bad: 14, re: NOMAGLEV, got: [/names no v8_current_cpu/] });
add('C6i-armed-build-says-arm-has-no-maglev', 'ia32', 'maglev', true, 'x86', 'armfalse', { v: 'FAIL', bad: 15, re: NOMAGLEV });
// D: Maglev present: proven whatever the CPU report says; a kit/build mismatch is its own failed check
add('D1-maglev-present-arch-hangs', 'arm', 'maglev', false, 'hang', 'none', { v: 'PASS', bad: 0, tier: /^maglev 13\/13/ });
add('D2-maglev-present-arch-rejects', 'arm', 'maglev-caller', false, 'reject', 'none', { v: 'PASS', bad: 0, tier: /^maglev 13\/13/ });
add('D3-maglev-present-no-uad', 'arm', 'maglev', false, 'nouad', 'none', { v: 'PASS', bad: 0, tier: /^maglev 13\/13/ });
add('D4-maglev-present-claims-x86', 'arm', 'maglev', false, 'x86', 'none', { v: 'PASS', bad: 0, tier: /^maglev 13\/13/ });
add('D5-maglev-present-build-says-no-maglev', 'arm', 'maglev', false, 'arm32', 'x86', { v: 'FAIL', bad: 1, re: [/lib\/build\.js and this V8 agree/], tier: /^maglev 13\/13/ });
add('D5b-maglev-present-build-says-arm-has-no-maglev', 'arm', 'maglev', false, 'arm32', 'armfalse', { v: 'FAIL', bad: 1, re: [/lib\/build\.js and this V8 agree/], tier: /^maglev 13\/13/ });
add('D6-maglev-present-max-opt-1', 'arm', 'maglev', false, 'arm32', 'none', { v: 'FAIL', re: [/^tier\./] }, ['--allow-natives-syntax', '--max-opt=1']);
// E: jitless vs ignition, flags that did not reach V8, natives rejected
add('E1-jitless-with-ignition-flags', 'arm', 'jitless', false, 'arm32', 'none', { v: 'FAIL', re: [/typeof WebAssembly/, /lite-mode bit/] }, IGN);
add('E2-ignition-with-jitless-flags', 'arm', 'ignition', false, 'arm32', 'none', { v: 'FAIL', re: [/typeof WebAssembly/, /lite-mode bit/] }, ['--jitless', '--allow-natives-syntax']);
add('E3-jitless-without-jitless', 'arm', 'jitless', false, 'arm32', 'none', { v: 'FAIL', re: [/typeof WebAssembly/, /lite-mode bit/, /flags reached V8: sparkplug/] }, ['--allow-natives-syntax']);
add('E4-ignition-with-default-flags', 'arm', 'ignition', false, 'arm32', 'none', { v: 'FAIL', re: [/flags reached V8: sparkplug/, /flags reached V8: maglev/, /flags reached V8: turbofan/] }, ['--allow-natives-syntax']);
add('E5-sparkplug-with-ignition-flags', 'arm', 'sparkplug', false, 'arm32', 'none', { v: 'FAIL', re: [/flags reached V8: sparkplug/, /^tier\./] }, IGN);
add('E6-turbofan-with-no-turbofan', 'arm', 'turbofan', false, 'arm32', 'none', { v: 'FAIL', re: [/^tier\./] }, ['--allow-natives-syntax', '--no-turbofan']);
add('E7-turbofan-caller-without-no-maglev', 'arm', 'turbofan-caller', false, 'arm32', 'none', { v: 'FAIL', re: [/flags reached V8: maglev/] }, ['--allow-natives-syntax', '--no-turbo-inlining']);
// (in a browser the SyntaxError of the %-syntax file also reaches kit.js's window error handler: one more failed row 'uncaught error | Script error.',
//  see js-jit-mutants.js n1; a d8 has no such handler, hence bad 1 here)
add('E8-maglev-natives-not-passed', 'arm', 'maglev', false, 'arm32', 'none', { v: 'FAIL', bad: 1, re: [/natives syntax accepted/], tier: /^NOT PROVEN: natives expected but rejected/ }, []);
add('E9-jitless-natives-not-passed', 'arm', 'jitless', false, 'arm32', 'none', { v: 'FAIL', bad: 2, re: [/natives syntax accepted/, /typeof WebAssembly/], tier: /^NOT PROVEN: natives expected but rejected/ }, []);

let unexpected = 0, n = 0;
for (const c of cases) {
  if (ONLY && !c.id.startsWith(ONLY)) continue;
  const flags = c.flags || C[c.cfg].flags;
  const a = [...flags, path.join(__dirname, 'js-jit-d8-harness.js'), '--', c.cfg, c.armed ? '1' : '0', c.arch, c.build, KIT];
  const t0 = Date.now();
  const p = spawnSync(D8[c.d8], a, { cwd: path.dirname(D8[c.d8]), encoding: 'utf8', timeout: 300000 });
  const out = (p.stdout || '').split(/\r?\n/);
  const line = out.find(l => l.startsWith('RESULT ')) || '';
  const g = k => { const m = new RegExp(' ' + k + '=("([^"]*)"|(\\S+))').exec(line); return m ? (m[2] !== undefined ? m[2] : m[3]) : null; };
  const badNames = out.filter(l => l.startsWith('  BAD: ')).map(l => l.slice(7).split(' | want=')[0]);
  const badGots = out.filter(l => l.startsWith('  BAD: ')).map(l => l.split(' | got=').slice(1).join(' | got='));
  const e = c.expect, why = [];
  if (!line) why.push('no RESULT line (rc ' + p.status + ' ' + (p.stderr || '').trim().slice(0, 200) + ')');
  else {
    if (g('verdict') !== e.v) why.push('verdict ' + g('verdict') + ' want ' + e.v);
    if (e.bad !== undefined && Number(g('bad')) !== e.bad) why.push('bad ' + g('bad') + ' want ' + e.bad);
    for (const r of (e.re || [])) if (!badNames.some(b => r.test(b))) why.push('no failed check matching ' + r);
    for (const r of (e.got || [])) if (!badGots.some(b => r.test(b))) why.push('no failed check whose got text matches ' + r);
    if (e.tier && !e.tier.test(g('tier') || '')) why.push('tier "' + g('tier') + '" does not match ' + e.tier);
    if (c.armed && g('planted') !== 'true') why.push('armed run did not plant');
  }
  n++; if (why.length) unexpected++;
  console.log((why.length ? 'UNEXPECTED ' : 'ok         ') + c.id.padEnd(48) + ' d8=' + c.d8 + ' ' + (line ? line.replace(/^RESULT /, '').replace(/config=\S+ armed=\S+ arch=\S+ build=\S+ /, '') : '') +
    (why.length ? '\n             ' + why.join('; ') + '\n             ' + out.filter(l => l.startsWith('  BAD: ')).slice(0, 3).join('\n             ') : '') + '  [' + (Date.now() - t0) + ' ms]');
}
console.log(n + ' cases, ' + unexpected + ' unexpected');
process.exit(unexpected ? 1 : 0);
