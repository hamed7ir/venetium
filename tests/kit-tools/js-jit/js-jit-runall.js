// js-jit kit tool (server only, not shipped): the js-jit runtime proof. Runs the js-jit page through venetium\tests\runner\run.js in all 10
// configurations, normal and --armed, then reads every result and prints the table. Exit 0 only when the gate holds:
//   normal: verdict PASS, planted false, 0 bad checks, no crash / console error / exception, and the tier label is the expected one:
//           'interpreter 13/13 (lite-mode set|clear)', 'sparkplug 13/13', 'turbofan 13/13' for the configurations that select that tier,
//           'n/a (natives off)' / 'n/a (this configuration selects no JS tier)' for the configurations with no JS tier, and for maglev /
//           maglev-caller 'n/a (no Maglev on ia32)' with --maglev na (an ia32 build: this x86 server) or 'maglev 13/13' with --maglev proven
//   armed:  verdict FAIL, planted true, exactly ONE bad check and it is 'answer overflow (host python)' (the planted one)
//   node js-jit-runall.js [--prefix <out folder prefix>] [--exe <venetium.exe>] [--maglev na|proven] [--table-only] [--jobs 3]
//   default prefix F:\cr\device-1\runs\fix2-js-jit-  ->  <prefix><config>[-armed]\js-jit.json, js-jit.png (runner output)
'use strict';
const fs = require('fs'), path = require('path'), vm = require('vm'), { spawn } = require('child_process');
const argv = process.argv.slice(2);
const opt = (n, d) => { const i = argv.indexOf('--' + n); return i >= 0 ? argv[i + 1] : d; };
const TESTS = path.resolve(__dirname, '..', '..');
const RUN = path.join(TESTS, 'runner', 'run.js');
const prefix = opt('prefix', 'F:\\cr\\device-1\\runs\\fix2-js-jit-');
const exe = opt('exe', null), maglev = opt('maglev', 'na'), jobs = Number(opt('jobs', 3));
const ctx = { window: {} }; vm.runInNewContext(fs.readFileSync(path.join(TESTS, 'kit', 'lib', 'configs.js'), 'utf8'), ctx);
const CONFIGS = ctx.window.KIT_CONFIGS;
const PLANTED = 'answer overflow (host python)';

function runOne(config, armed) {
  const out = prefix + config + (armed ? '-armed' : '');
  const args = [RUN, '--config', config, '--pages', 'js-jit', '--out', out, ...(armed ? ['--armed'] : []), ...(exe ? ['--exe', exe] : [])];
  return new Promise(res => { const p = spawn(process.execPath, args, { stdio: 'ignore' }); p.on('close', code => res(code)); });
}
function tierOk(c, tier) {
  const t = CONFIGS[c].tier;
  if (!t) return /^n\/a \((natives off|this configuration selects no JS tier)\)$/.test(tier);
  if (t === 'interpreter') return new RegExp('^interpreter 13/13 \\(lite-mode ' + (CONFIGS[c].flags.includes('--jitless') ? 'set' : 'clear') + '\\)$').test(tier);
  if (t === 'maglev') return maglev === 'na' ? /^n\/a \(no Maglev on ia32\)/.test(tier) : /^maglev 13\/13$/.test(tier);
  return new RegExp('^' + t + ' 13/13$').test(tier);
}
(async () => {
  const todo = []; for (const c of Object.keys(CONFIGS)) for (const a of [false, true]) todo.push([c, a]);
  if (!argv.includes('--table-only')) {
    let next = 0;
    await Promise.all(Array.from({ length: jobs }, async () => { while (next < todo.length) { const [c, a] = todo[next++]; await runOne(c, a); } }));
  }
  let bad = 0;
  const line = (ok, s) => { if (!ok) bad++; console.log((ok ? 'ok   ' : 'FAIL ') + s); };
  for (const [c, a] of todo) {
    const f = path.join(prefix + c + (a ? '-armed' : ''), 'js-jit.json');
    let rec = null; try { rec = JSON.parse(fs.readFileSync(f, 'utf8')); } catch (e) {}
    const label = (c + (a ? ' ARMED' : '')).padEnd(22);
    if (!rec || !rec.result) { line(false, label + ' NO RESULT (' + (rec ? (rec.crashes.length ? 'CRASH' : 'TIMEOUT') : f + ' missing') + ')'); continue; }
    const r = rec.result, badRows = r.checks.filter(k => !k.ok), real = r.checks.filter(k => !k.info && !k.na), na = r.checks.filter(k => k.na), info = r.checks.filter(k => k.info);
    const clean = rec.crashes.length === 0 && rec.consoleErrors.length === 0 && rec.exceptions.length === 0;
    const tier = r.tier || '';
    const ok = a ? (r.verdict === 'FAIL' && r.planted === true && badRows.length === 1 && badRows[0].name === PLANTED && clean && tierOk(c, tier))
                 : (r.verdict === 'PASS' && r.planted === false && badRows.length === 0 && clean && tierOk(c, tier));
    line(ok, label + ' ' + r.verdict + ' checks ' + r.checks.length + ' (real ' + real.length + ', info ' + info.length + ', n/a ' + na.length + ') bad ' + badRows.length + ' planted ' + r.planted +
      ' crashes ' + rec.crashes.length + ' console ' + rec.consoleErrors.length + ' ' + rec.ms + ' ms | tier ' + tier.slice(0, 60) + (badRows.length && !(a && ok) ? ' | bad: ' + badRows.map(k => k.name).join('; ') : ''));
  }
  console.log(bad ? '\n' + bad + ' of ' + todo.length + ' runs FAILED the gate' : '\nall ' + todo.length + ' runs meet the gate');
  process.exit(bad ? 1 : 0);
})();
