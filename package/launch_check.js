// BATCH-DEVICE-1 §2 step 11 (RUNTIME): launch an unzipped portable Venetium (headless, fresh profile under F:\cr\device-1\profiles,
// --allow-chrome-scheme-url), read chrome://version, load a web page and a local page, screenshot each, and report.
//   node launch_check.js <venetium.exe> <out-dir> <profile-dir>
// No sign-in, no forms, no consent clicks; only reads. Writes <out>\launch.json, version.png, page.png.
'use strict';
const fs = require('fs'), path = require('path');
const { launch, sleep } = require('D:/repo/supermium-rt/venetium/tests/runner/cdp.js');
const [exe, out, profile] = process.argv.slice(2);
fs.mkdirSync(out, { recursive: true });
(async () => {
  const r = { exe, started: new Date().toISOString(), checks: [] };
  const check = (name, ok, got) => { r.checks.push({ name, ok: !!ok, got }); console.log((ok ? 'ok   ' : 'FAIL ') + name + ' :: ' + got); };
  const b = await launch({ exe, profile, flags: ['--allow-chrome-scheme-url', '--no-first-run', '--no-default-browser-check'], log: path.join(out, 'browser.log') });
  try {
    const t = await b.newTab('chrome://version');
    await sleep(1500);
    const ver = await t.eval('document.body.innerText');
    fs.writeFileSync(path.join(out, 'version.txt'), ver);
    await t.screenshot(path.join(out, 'version.png'));
    const head = (ver.split('\n').find(l => /Venetium/.test(l)) || '').trim();
    check('chrome://version names "Venetium 150.0.7871.226"', /Venetium\s+150\.0\.7871\.226/.test(ver), head);
    check('chrome://version says (32-bit)', /\(32-bit\)/.test(ver), (ver.match(/\(\d\d-bit\)/) || ['none'])[0]);
    const exeLine = (ver.split('\n').find(l => /venetium\.exe/i.test(l)) || '').trim();
    check('the running exe is the unzipped one', exeLine.toLowerCase().includes(path.dirname(exe).toLowerCase().replace(/\//g, '\\')), exeLine);
    await t.close();
    const p = await b.newTab('https://example.com/');
    let title = '';
    for (let i = 0; i < 60 && !/Example Domain/.test(title); i++) { await sleep(500); title = await p.eval('document.title').catch(() => ''); }
    await p.screenshot(path.join(out, 'page.png'));
    check('a web page loads (https://example.com/)', /Example Domain/.test(title), title);
    await p.close();
    const d = await b.newTab('data:text/html,<title>local ok</title><p>local');
    await sleep(500);
    const dt = await d.eval('document.title');
    check('a local page renders (data: URL)', dt === 'local ok', dt);
    await d.close();
  } catch (e) { check('no exception in the launch check', false, String(e && e.stack || e)); }
  finally {
    r.exits = b.exits; await b.close().catch(() => {});
    r.verdict = r.checks.every(c => c.ok) ? 'PASS' : 'FAIL';
    fs.writeFileSync(path.join(out, 'launch.json'), JSON.stringify(r, null, 1));
    console.log('VERDICT ' + r.verdict);
  }
})();
