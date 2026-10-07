// Node tool (server only; NOT part of the kit): the js-f1 runtime proof. Runs the js-f1 page through venetium\tests\runner\run.js in all 10
// configurations, normal and --armed, then reads every result and prints the table. Exit 0 only when the gate holds:
//   normal: verdict PASS, 0 bad checks, no crash / console error / exception, and the tier word is the expected one:
//           'proven <tier>' for the configurations that select a tier, 'n/a (configuration selects no JS tier)' for default / liftoff-only /
//           no-liftoff, and for maglev / maglev-caller 'n/a (no Maglev in this build ...' with --maglev na (an ia32 build: this x86 server)
//           or 'proven maglev (...' with --maglev proven (a build that has Maglev: ARM32)
//   armed:  verdict FAIL, planted true, exactly ONE bad check and it is 'sloppy plain 2of3 | params' (the planted one)
//   node js-f1-runall.js [--prefix <out folder prefix>] [--exe <venetium.exe>] [--maglev na|proven] [--table-only] [--jobs 4]
//   default prefix F:\cr\device-1\runs\fix-js-f1-  ->  <prefix><config>[-armed]\js-f1.json, js-f1.png (runner output)
'use strict';
const fs = require('fs'), path = require('path'), vm = require('vm'), { spawn } = require('child_process');
const argv = process.argv.slice(2);
const opt = (n, d) => { const i = argv.indexOf('--' + n); return i >= 0 ? argv[i + 1] : d; };
const TESTS = path.resolve(__dirname, '..', '..');
const RUN = path.join(TESTS, 'runner', 'run.js');
const prefix = opt('prefix', 'F:\\cr\\device-1\\runs\\fix-js-f1-');
const exe = opt('exe', null), maglev = opt('maglev', 'na'), jobs = Number(opt('jobs', 4));
const ctx = { window: {} }; vm.runInNewContext(fs.readFileSync(path.join(TESTS, 'kit', 'lib', 'configs.js'), 'utf8'), ctx);
const CONFIGS = ctx.window.KIT_CONFIGS;
const PLANTED = 'sloppy plain 2of3 | params';

function runOne(config, armed) {
  const out = prefix + config + (armed ? '-armed' : '');
  const args = [RUN, '--config', config, '--pages', 'js-f1', '--out', out, ...(armed ? ['--armed'] : []), ...(exe ? ['--exe', exe] : [])];
  return new Promise(res => { const p = spawn(process.execPath, args, { stdio: 'ignore' }); p.on('close', code => res(code)); });
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
    const f = path.join(prefix + c + (a ? '-armed' : ''), 'js-f1.json');
    let rec = null; try { rec = JSON.parse(fs.readFileSync(f, 'utf8')); } catch (e) {}
    const label = (c + (a ? ' ARMED' : '')).padEnd(22);
    if (!rec || !rec.result) { line(false, label + ' NO RESULT (' + (rec ? (rec.crashes.length ? 'CRASH' : 'TIMEOUT') : f + ' missing') + ')'); continue; }
    const r = rec.result, badRows = r.checks.filter(k => !k.ok), real = r.checks.filter(k => !k.info && !k.na), na = r.checks.filter(k => k.na);
    const clean = rec.crashes.length === 0 && rec.consoleErrors.length === 0 && rec.exceptions.length === 0;
    const tier = r.tier || '';
    const wantTier = CONFIGS[c].tier;
    const tierOk = !wantTier ? /^n\/a \(configuration selects no JS tier\)$/.test(tier)
      : (wantTier === 'maglev' && maglev === 'na') ? /^n\/a \(no Maglev in this build/.test(tier)
      : new RegExp('^proven ' + wantTier + ' \\(' + CONFIGS[c].who + 's, ' + (CONFIGS[c].who === 'caller' ? 24 : 6) + ' functions\\)$').test(tier);
    const ok = a ? (r.verdict === 'FAIL' && r.planted === true && badRows.length === 1 && badRows[0].name === PLANTED && clean && tierOk)
                 : (r.verdict === 'PASS' && r.planted === false && badRows.length === 0 && clean && tierOk);
    line(ok, label + ' ' + r.verdict + ' checks ' + r.checks.length + ' (real ' + real.length + ', n/a ' + na.length + ') bad ' + badRows.length + ' planted ' + r.planted +
      ' crashes ' + rec.crashes.length + ' console ' + rec.consoleErrors.length + ' ' + rec.ms + ' ms | tier ' + tier.slice(0, 60) + (badRows.length && !(a && ok) ? ' | bad: ' + badRows.map(k => k.name).join('; ') : ''));
  }
  console.log(bad ? '\n' + bad + ' of ' + todo.length + ' runs FAILED the gate' : '\nall ' + todo.length + ' runs meet the gate');
  process.exit(bad ? 1 : 0);
})();
