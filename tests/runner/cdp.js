// Venetium test runner — a small DevTools-protocol driver shared by the server-side X86-2 scripts (§3–§5). Node 24, built-in WebSocket.
//   const { launch } = require('./cdp');
//   const b = await launch({ exe, profile, flags: [...], headless: true, log: 'file' });   // fresh profile unless keepProfile
//   const t = await b.newTab(url);            // { targetId, sessionId, send(method, params), eval(expr), screenshot(file), close(), events }
//   await b.close();
'use strict';
const fs = require('fs'), path = require('path'), { spawn } = require('child_process');
const sleep = ms => new Promise(r => setTimeout(r, ms));

async function launch({ exe = 'F:\\cr\\src\\out\\x86-rt21\\venetium.exe', profile, flags = [], headless = true, keepProfile = false, log, url = 'about:blank', windowSize = '1280,1000' }) {
  if (!keepProfile) fs.rmSync(profile, { recursive: true, force: true });
  fs.mkdirSync(profile, { recursive: true });
  const portFile = path.join(profile, 'DevToolsActivePort');
  fs.rmSync(portFile, { force: true });
  const argv = ['--user-data-dir=' + profile, '--remote-debugging-port=0', '--window-size=' + windowSize, ...(headless ? ['--headless=new'] : []), ...flags, url];
  const proc = spawn(exe, argv, { cwd: path.dirname(exe), stdio: ['ignore', 'pipe', 'pipe'] });
  const logStream = log ? fs.createWriteStream(log) : null;
  if (logStream) { proc.stdout.pipe(logStream); proc.stderr.pipe(logStream); } else { proc.stdout.resume(); proc.stderr.resume(); }
  const b = { proc, argv, profile, exits: [], events: [], listeners: [], pending: new Map(), id: 0, exited: false };
  proc.on('exit', (code, sig) => { b.exits.push({ code, sig, t: Date.now() }); b.exited = true; });
  for (let i = 0; i < 400 && !fs.existsSync(portFile); i++) { if (b.exited) throw new Error('browser exited before DevTools came up: ' + JSON.stringify(b.exits)); await sleep(100); }
  await sleep(200);
  const [port, wsPath] = fs.readFileSync(portFile, 'utf8').split(/\r?\n/);
  b.port = port;
  const ws = new WebSocket(`ws://127.0.0.1:${port}${wsPath}`);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error('ws error')); });
  ws.onmessage = m => {
    const msg = JSON.parse(typeof m.data === 'string' ? m.data : m.data.toString());
    if (msg.id && b.pending.has(msg.id)) { const { res, rej } = b.pending.get(msg.id); b.pending.delete(msg.id); msg.error ? rej(new Error(msg.error.message + ' ' + (msg.error.data || ''))) : res(msg.result); }
    else if (msg.method) { b.events.push({ t: Date.now(), method: msg.method, sessionId: msg.sessionId, params: msg.params }); for (const l of b.listeners) l(msg); }
  };
  b.send = (method, params, sessionId, timeoutMs = 60000) => {
    const id = ++b.id; const m = { id, method, params: params || {} }; if (sessionId) m.sessionId = sessionId;
    ws.send(JSON.stringify(m));
    return new Promise((res, rej) => { const tm = setTimeout(() => { if (b.pending.has(id)) { b.pending.delete(id); rej(new Error('timeout ' + method)); } }, timeoutMs);
      b.pending.set(id, { res: v => { clearTimeout(tm); res(v); }, rej: e => { clearTimeout(tm); rej(e); } }); });
  };
  await b.send('Target.setDiscoverTargets', { discover: true });
  b.newTab = async (url, { enable = ['Page', 'Runtime', 'Log', 'Inspector', 'Media'] } = {}) => {
    const { targetId } = await b.send('Target.createTarget', { url: 'about:blank' });
    const { sessionId } = await b.send('Target.attachToTarget', { targetId, flatten: true });
    const t = { targetId, sessionId, crashed: false, consoleErrors: [], exceptions: [], media: {} };
    const on = msg => {
      if (msg.sessionId === sessionId && msg.method === 'Inspector.targetCrashed') t.crashed = true;
      if (msg.method === 'Target.targetCrashed' && msg.params.targetId === targetId) t.crashed = true;
      if (msg.sessionId === sessionId && msg.method === 'Runtime.exceptionThrown') t.exceptions.push((msg.params.exceptionDetails.exception || {}).description || msg.params.exceptionDetails.text);
      if (msg.sessionId === sessionId && msg.method === 'Log.entryAdded' && msg.params.entry.level === 'error') t.consoleErrors.push(msg.params.entry.text.slice(0, 300));
      if (msg.sessionId === sessionId && msg.method === 'Runtime.consoleAPICalled' && msg.params.type === 'error') t.consoleErrors.push(msg.params.args.map(a => a.value || a.description).join(' ').slice(0, 300));
      if (msg.sessionId === sessionId && msg.method === 'Media.playerPropertiesChanged') { const p = t.media[msg.params.playerId] = t.media[msg.params.playerId] || {}; for (const x of msg.params.properties) p[x.name] = x.value; }
    };
    b.listeners.push(on);
    t.send = (m, p, to) => b.send(m, p, sessionId, to);
    for (const d of enable) { try { await t.send(d + '.enable', {}); } catch (e) {} }
    t.eval = async (expr, awaitPromise = true) => {
      const r = await t.send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise }, 120000);
      if (r.exceptionDetails) throw new Error((r.exceptionDetails.exception || {}).description || r.exceptionDetails.text);
      return r.result.value;
    };
    t.navigate = async (u, waitMs = 0) => {
      const loaded = new Promise(res => { const h = m => { if (m.sessionId === sessionId && m.method === 'Page.loadEventFired') { b.listeners.splice(b.listeners.indexOf(h), 1); res(); } }; b.listeners.push(h); });
      const t0 = Date.now(); const nav = await t.send('Page.navigate', { url: u });
      await Promise.race([loaded, sleep(60000)]); t.loadMs = Date.now() - t0; if (waitMs) await sleep(waitMs); return nav;
    };
    t.screenshot = async (file, full = true) => { const s = await t.send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: full }); fs.writeFileSync(file, Buffer.from(s.data, 'base64')); return file; };
    // text of the whole page including open shadow roots (chrome://settings is built from them)
    t.deepText = () => t.eval(`(() => { const out = []; const walk = n => { if (n.shadowRoot) walk(n.shadowRoot);
      for (const c of n.childNodes) { if (c.nodeType === 3) { const s = c.textContent.trim(); if (s) out.push(s); } else if (c.nodeType === 1 && !['SCRIPT','STYLE'].includes(c.tagName)) walk(c); } };
      walk(document.documentElement); return out.join('\\n'); })()`);
    t.close = async () => { b.listeners.splice(b.listeners.indexOf(on), 1); try { await b.send('Target.closeTarget', { targetId }); } catch (e) {} };
    if (url) await t.navigate(url);
    return t;
  };
  b.close = async () => {
    try { await b.send('Browser.close', {}, undefined, 10000); } catch (e) {}
    for (let i = 0; i < 150 && !b.exited; i++) await sleep(100);
    if (!b.exited) { try { process.kill(proc.pid); } catch (e) {} }
    try { ws.close(); } catch (e) {}
    if (logStream) logStream.end();
  };
  return b;
}
module.exports = { launch, sleep };
