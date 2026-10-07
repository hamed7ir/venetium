// scratch driver for images.html development (NOT part of the kit): opens an html file in venetium over the DevTools protocol,
// waits for window.__probe (or window.__kitResult), prints it as JSON, saves a screenshot.
//   node probe.js <file.html> <out-prefix> [--query "a=b"] [--flags "--js-flags=..."] [--wait <s>] [--gpu]
'use strict';
const fs = require('fs'), path = require('path'), { spawn } = require('child_process'), { pathToFileURL } = require('url');
const a = process.argv.slice(2);
const opt = (n, d) => { const i = a.indexOf('--' + n); return i >= 0 ? a[i + 1] : d; };
const file = path.resolve(a[0]); const outp = path.resolve(a[1]);
const exe = process.env.VENETIUM_EXE || 'F:\\cr\\src\\out\\x86-rt21\\venetium.exe';   // the browser under test (override with VENETIUM_EXE)
const extra = (opt('flags', '') || '').split(' ').filter(Boolean);
const waitMs = Number(opt('wait', 60)) * 1000;
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const profile = outp + '-profile'; fs.rmSync(profile, { recursive: true, force: true }); fs.mkdirSync(profile, { recursive: true });
  const argv = ['--user-data-dir=' + profile, '--headless=new', '--remote-debugging-port=0', '--window-size=1280,1000', '--no-first-run', '--no-default-browser-check', ...extra, 'about:blank'];
  const proc = spawn(exe, argv, { cwd: path.dirname(exe), stdio: ['ignore', 'pipe', 'pipe'] });
  const log = fs.createWriteStream(outp + '-browser.log'); proc.stdout.pipe(log); proc.stderr.pipe(log);
  let exited = false; proc.on('exit', (c, s) => { exited = true; console.log('browser exit', c, s); });
  const portFile = path.join(profile, 'DevToolsActivePort');
  for (let i = 0; i < 300 && !fs.existsSync(portFile); i++) await sleep(100);
  await sleep(200);
  const [port, wsPath] = fs.readFileSync(portFile, 'utf8').split(/\r?\n/);
  const ws = new WebSocket(`ws://127.0.0.1:${port}${wsPath}`);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error('ws')); });
  let id = 0; const pending = new Map(); const events = [];
  ws.onmessage = m => { const msg = JSON.parse(m.data.toString()); if (msg.id && pending.has(msg.id)) { const { res, rej } = pending.get(msg.id); pending.delete(msg.id); msg.error ? rej(new Error(JSON.stringify(msg.error))) : res(msg.result); } else if (msg.method) events.push(msg); };
  const send = (method, params, sessionId) => { const i = ++id; const m = { id: i, method, params: params || {} }; if (sessionId) m.sessionId = sessionId; ws.send(JSON.stringify(m)); return new Promise((res, rej) => { pending.set(i, { res, rej }); setTimeout(() => { if (pending.has(i)) { pending.delete(i); rej(new Error('timeout ' + method)); } }, 60000); }); };
  const { targetId } = await send('Target.createTarget', { url: 'about:blank' });
  const { sessionId } = await send('Target.attachToTarget', { targetId, flatten: true });
  for (const d of ['Page', 'Runtime', 'Log', 'Inspector']) await send(d + '.enable', {}, sessionId);
  const url = pathToFileURL(file).href + (opt('query', '') ? '?' + opt('query') : '');
  await send('Page.navigate', { url }, sessionId);
  const t0 = Date.now(); let res = null;
  while (Date.now() - t0 < waitMs) {
    if (events.some(e => /Crashed/.test(e.method))) { console.log('CRASH EVENT'); break; }
    try { const r = await send('Runtime.evaluate', { expression: '(()=>{const r=window.__probe||window.__kitResult;return r?JSON.stringify(r):""})()', returnByValue: true }, sessionId); if (r.result && r.result.value) { res = r.result.value; break; } } catch (e) { if (exited) break; }
    await sleep(300);
  }
  console.log('ms', Date.now() - t0);
  const errs = events.filter(e => (e.method === 'Runtime.exceptionThrown') || (e.method === 'Log.entryAdded' && e.params.entry.level === 'error') || (e.method === 'Runtime.consoleAPICalled' && ['error', 'warning'].includes(e.params.type))).map(e => JSON.stringify(e.params).slice(0, 400));
  if (errs.length) console.log('console/exceptions:', errs.slice(0, 20).join('\n'));
  try {
    const params = { format: 'png', captureBeyondViewport: true };
    if (opt('clip', '')) { const [x, y, w, h] = opt('clip').split(',').map(Number); params.clip = { x, y, width: w, height: h, scale: Number(opt('scale', 1)) }; }
    const shot = await send('Page.captureScreenshot', params, sessionId); fs.writeFileSync(outp + '.png', Buffer.from(shot.data, 'base64'));
  } catch (e) { console.log('shot err', e.message); }
  if (res) fs.writeFileSync(outp + '.json', res);
  console.log(res ? (res.length > 6000 ? res.slice(0, 6000) + '...[' + res.length + ']' : res) : 'NO RESULT');
  try { await send('Browser.close'); } catch (e) {}
  for (let i = 0; i < 50 && !exited; i++) await sleep(100);
  if (!exited) try { process.kill(proc.pid); } catch (e) {}
  process.exit(0);
})().catch(e => { console.error('ERR', e.stack || e); process.exit(1); });
