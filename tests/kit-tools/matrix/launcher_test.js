// BATCH-DEVICE-1 §1 step 8 (RUNTIME): run a kit launcher (.cmd) from a folder whose path has spaces, the device layout (kit\ next to
// venetium.exe), passing extra browser arguments through the launcher's %*; attach over the DevTools protocol, wait for index.html's
// verdict, screenshot it, close the browser.
//   node launcher_test.js <launcher.cmd> <out-dir>
// Extra arguments given to the launcher: --headless=new --remote-debugging-port=0 --no-first-run --no-default-browser-check
// --use-fake-device-for-media-stream --use-fake-ui-for-media-stream (the fake camera/microphone answer webrtc's prompt on the server;
// on the device he clicks Allow). The launcher itself picks the profile (kit-profile-<config> next to the kit) and the js-flags.
'use strict';
const fs = require('fs'), path = require('path'), { spawn } = require('child_process');
const sleep = ms => new Promise(r => setTimeout(r, ms));
const [launcher, out] = process.argv.slice(2);
fs.mkdirSync(out, { recursive: true });
const cfgName = path.basename(launcher, '.cmd');
const kit = path.resolve(path.dirname(launcher), '..');
const profile = path.resolve(kit, '..', 'kit-profile-' + cfgName);
const extra = '--headless=new --remote-debugging-port=0 --no-first-run --no-default-browser-check --use-fake-device-for-media-stream --use-fake-ui-for-media-stream';
(async () => {
  const rep = { launcher, kit, profile, extra, started: new Date().toISOString() };
  fs.rmSync(profile, { recursive: true, force: true });
  const cmdline = `""${launcher}" ${extra}"`;
  const proc = spawn('cmd.exe', ['/d', '/s', '/c', cmdline], { windowsVerbatimArguments: true, stdio: ['ignore', 'pipe', 'pipe'] });
  let launcherOut = '';
  proc.stdout.on('data', d => launcherOut += d); proc.stderr.on('data', d => launcherOut += d);
  let exited = null; proc.on('exit', c => exited = c);
  const portFile = path.join(profile, 'DevToolsActivePort');
  for (let i = 0; i < 300 && !fs.existsSync(portFile); i++) { if (exited !== null) break; await sleep(100); }
  if (!fs.existsSync(portFile)) { rep.verdict = 'LAUNCH-FAILED'; rep.launcherOut = launcherOut; rep.exit = exited; finish(rep); return; }
  await sleep(300);
  const [port, wsPath] = fs.readFileSync(portFile, 'utf8').split(/\r?\n/);
  const ws = new WebSocket(`ws://127.0.0.1:${port}${wsPath}`);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error('ws error')); });
  let id = 0; const pending = new Map();
  ws.onmessage = m => { const msg = JSON.parse(String(m.data)); if (msg.id && pending.has(msg.id)) { const p = pending.get(msg.id); pending.delete(msg.id); msg.error ? p.rej(new Error(JSON.stringify(msg.error))) : p.res(msg.result); } };
  const send = (method, params = {}, sessionId) => new Promise((res, rej) => { const i = ++id; pending.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method, params, ...(sessionId ? { sessionId } : {}) })); });
  let target = null;
  for (let i = 0; i < 100 && !target; i++) { const t = await send('Target.getTargets'); target = t.targetInfos.find(x => x.type === 'page' && /index\.html/.test(x.url)); if (!target) await sleep(200); }
  rep.url = target && target.url;
  const { sessionId } = await send('Target.attachToTarget', { targetId: target.targetId, flatten: true });
  let res = null; const t0 = Date.now();
  while (Date.now() - t0 < 900000) {
    const r = await send('Runtime.evaluate', { expression: 'window.__kitIndexResult ? JSON.stringify({verdict: __kitIndexResult.verdict, config: __kitIndexResult.config, pages: __kitIndexResult.pages.map(p => p.page + ":" + p.verdict + "/" + p.checks)}) : ""', returnByValue: true }, sessionId);
    if (r.result && r.result.value) { res = JSON.parse(r.result.value); break; }
    await sleep(2000);
  }
  rep.ms = Date.now() - t0; rep.index = res;
  await send('Runtime.evaluate', { expression: 'window.scrollTo(0,0)' }, sessionId);
  const shot = await send('Page.captureScreenshot', { format: 'png' }, sessionId);
  fs.writeFileSync(path.join(out, cfgName + '.png'), Buffer.from(shot.data, 'base64'));
  const cl = await send('Runtime.evaluate', { expression: 'document.getElementById("cfg").textContent', returnByValue: true }, sessionId);
  rep.cfgLine = cl.result && cl.result.value;
  try { await send('Browser.close'); } catch (e) {}
  for (let i = 0; i < 100 && exited === null; i++) await sleep(100);
  rep.launcherExit = exited; rep.launcherOut = launcherOut.trim();
  rep.verdict = res ? res.verdict : 'NO-RESULT';
  finish(rep);
})().catch(e => finish({ launcher, verdict: 'ERROR', error: String(e && e.stack || e) }));
function finish(rep) { fs.writeFileSync(path.join(out, cfgName + '.json'), JSON.stringify(rep, null, 1)); console.log(cfgName, rep.verdict, rep.index ? rep.index.pages.join(' ') : '', rep.cfgLine || ''); }
