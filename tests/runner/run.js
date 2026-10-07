// Venetium test kit runner (BATCH-X86-2 §1.2) — server only. Drives venetium.exe over the DevTools protocol with Node's built-in
// WebSocket (Node 24; nothing to install).
//   node run.js --config <name> [--armed] [--pages js-f1,media|all] [--index] --out <dir>
//               [--exe <venetium.exe>] [--extra "<more flags>"] [--page-timeout <s>] [--keep-profile] [--test-autoplay]
// For each run it starts venetium.exe with a fresh profile (F:\cr\device-1\profiles\runner\<out name>-<group>, or under
// %KIT_PROFILES%; deleted first and
// after), --headless=new --remote-debugging-port=0
// (the port comes from the profile's DevToolsActivePort) and the configuration's --js-flags (kit\lib\configs.js); the autoplay
// policy is the browser's default, as on the device (--test-autoplay adds --autoplay-policy=no-user-gesture-required, which
// the X86-2 runs used); webrtc.html gets its own browser with --use-fake-device-for-media-stream
// --use-fake-ui-for-media-stream. Each page opens in a new tab as file://…/<page>.html?config=…&armed=…; the runner waits for
// window.__kitResult (or the page timeout), screenshots it, and records: Inspector.targetCrashed / Target.targetCrashed events,
// process exits, console errors and exceptions, the Media domain's player properties (decoder and codec names), downloads.
// --index runs index.html (every page in iframes) and waits for window.__kitIndexResult.
// Writes <out>\<page>.json, <out>\<page>.png, <out>\summary.json; prints one line per page. Exit code 0 unless the runner itself failed.
'use strict';
const fs = require('fs'), path = require('path'), vm = require('vm'), { spawn } = require('child_process'), { pathToFileURL } = require('url');
const KIT = path.resolve(__dirname, '..', 'kit');
const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf('--' + n); return i >= 0 ? args[i + 1] : d; };
const has = n => args.includes('--' + n);
const ctx = { window: {} }; vm.runInNewContext(fs.readFileSync(path.join(KIT, 'lib', 'configs.js'), 'utf8'), ctx);
const CONFIGS = ctx.window.KIT_CONFIGS, ALL_PAGES = ctx.window.KIT_PAGES;
const config = opt('config', 'default'); const armed = has('armed');
if (!CONFIGS[config]) { console.error('unknown config ' + config); process.exit(2); }
const out = path.resolve(opt('out', path.join('F:\\cr\\device-1\\runs', config + (armed ? '-armed' : ''))));
const exe = path.resolve(opt('exe', 'F:\\cr\\src\\out\\x86-rt21\\venetium.exe'));
const pageTimeout = Number(opt('page-timeout', 180)) * 1000;
let pages = opt('pages', has('index') ? '' : 'all');
pages = pages === 'all' ? ALL_PAGES.slice() : pages ? pages.split(',').filter(Boolean) : [];
const extra = (opt('extra', '') || '').split(' ').filter(Boolean);
fs.mkdirSync(out, { recursive: true });
const sleep = ms => new Promise(r => setTimeout(r, ms));
const PROFILES = process.env.KIT_PROFILES || 'F:/cr/device-1/profiles/runner';
const log = (...a) => { const s = a.join(' '); console.log(s); fs.appendFileSync(path.join(out, 'runner.log'), s + '\n'); };

class Browser {
  constructor(name, flags) { this.name = name; this.flags = flags; this.id = 0; this.pending = new Map(); this.listeners = []; this.events = []; this.exits = []; }
  async start() {
    // test profiles live under F:\cr\device-1\profiles (BATCH-DEVICE-1 rules), one per run folder and browser group; deleted after the run
    this.profile = path.join(PROFILES, path.basename(path.resolve(out)) + '-' + this.name);
    fs.rmSync(this.profile, { recursive: true, force: true }); fs.mkdirSync(this.profile, { recursive: true });
    const cfg = CONFIGS[config];
    const argv = ['--user-data-dir=' + this.profile, '--headless=new', '--remote-debugging-port=0', '--window-size=1280,1000',
      ...(has('test-autoplay') ? ['--autoplay-policy=no-user-gesture-required'] : []), '--no-first-run', '--no-default-browser-check'];
    if (cfg.flags.length) argv.push('--js-flags=' + cfg.flags.join(' '));
    argv.push(...this.flags, ...extra, 'about:blank');
    this.argv = argv;
    this.proc = spawn(exe, argv, { cwd: path.dirname(exe), stdio: ['ignore', 'pipe', 'pipe'] });
    this.stderr = fs.createWriteStream(path.join(out, 'browser-' + this.name + '.log'));
    this.proc.stdout.pipe(this.stderr); this.proc.stderr.pipe(this.stderr);
    this.proc.on('exit', (code, sig) => { this.exits.push({ code, sig, t: Date.now() }); this.exited = true; });
    const portFile = path.join(this.profile, 'DevToolsActivePort');
    for (let i = 0; i < 300 && !fs.existsSync(portFile); i++) { if (this.exited) throw new Error('browser exited before DevTools came up'); await sleep(100); }
    await sleep(200);
    const [port, wsPath] = fs.readFileSync(portFile, 'utf8').split(/\r?\n/);
    this.ws = new WebSocket(`ws://127.0.0.1:${port}${wsPath}`);
    await new Promise((res, rej) => { this.ws.onopen = res; this.ws.onerror = e => rej(new Error('ws error')); });
    this.ws.onmessage = m => {
      const msg = JSON.parse(typeof m.data === 'string' ? m.data : m.data.toString());
      if (msg.id && this.pending.has(msg.id)) { const { res, rej } = this.pending.get(msg.id); this.pending.delete(msg.id); msg.error ? rej(new Error(JSON.stringify(msg.error))) : res(msg.result); }
      else if (msg.method) { this.events.push({ t: Date.now(), method: msg.method, sessionId: msg.sessionId, params: msg.params }); for (const l of this.listeners) l(msg); }
    };
    await this.send('Target.setDiscoverTargets', { discover: true });
    this.downloads = path.join(out, 'downloads'); fs.mkdirSync(this.downloads, { recursive: true });
    await this.send('Browser.setDownloadBehavior', { behavior: 'allowAndName', downloadPath: this.downloads, eventsEnabled: true });
    log(`[${this.name}] started pid ${this.proc.pid} port ${port} flags ${argv.slice(1, -1).join(' ')}`);
  }
  send(method, params, sessionId) {
    const id = ++this.id;
    const msg = { id, method, params: params || {} }; if (sessionId) msg.sessionId = sessionId;
    this.ws.send(JSON.stringify(msg));
    return new Promise((res, rej) => { const tm = setTimeout(() => { if (this.pending.has(id)) { this.pending.delete(id); rej(new Error('timeout ' + method)); } }, 60000);
      this.pending.set(id, { res: v => { clearTimeout(tm); res(v); }, rej: e => { clearTimeout(tm); rej(e); } }); });
  }
  async stop() {
    try { await this.send('Browser.close'); } catch (e) {}
    for (let i = 0; i < 100 && !this.exited; i++) await sleep(100);
    if (!this.exited) { try { process.kill(this.proc.pid); } catch (e) {} }
    try { this.ws.close(); } catch (e) {}
  }
}

async function runPage(b, url, name, waitExpr, timeoutMs) {
  const t0 = Date.now();
  const { targetId } = await b.send('Target.createTarget', { url: 'about:blank' });
  const { sessionId } = await b.send('Target.attachToTarget', { targetId, flatten: true });
  const rec = { page: name, url, crashes: [], consoleErrors: [], exceptions: [], media: {}, downloads: [] };
  const on = msg => {
    if (msg.method === 'Inspector.targetCrashed' && msg.sessionId === sessionId) rec.crashes.push({ event: msg.method, t: Date.now() - t0 });
    if (msg.method === 'Target.targetCrashed' && msg.params.targetId === targetId) rec.crashes.push({ event: msg.method, status: msg.params.status, errorCode: msg.params.errorCode, t: Date.now() - t0 });
    if (msg.method === 'Runtime.exceptionThrown' && msg.sessionId === sessionId) rec.exceptions.push((msg.params.exceptionDetails.exception || {}).description || msg.params.exceptionDetails.text);
    if (msg.method === 'Log.entryAdded' && msg.sessionId === sessionId && msg.params.entry.level === 'error') rec.consoleErrors.push(msg.params.entry.text.slice(0, 300));
    if (msg.method === 'Runtime.consoleAPICalled' && msg.sessionId === sessionId && msg.params.type === 'error') rec.consoleErrors.push(msg.params.args.map(a => a.value || a.description).join(' ').slice(0, 300));
    if (msg.method === 'Media.playerPropertiesChanged' && msg.sessionId === sessionId) { const p = rec.media[msg.params.playerId] = rec.media[msg.params.playerId] || {}; for (const x of msg.params.properties) p[x.name] = x.value; }
    if (msg.method === 'Media.playerErrorsRaised' && msg.sessionId === sessionId) { const p = rec.media[msg.params.playerId] = rec.media[msg.params.playerId] || {}; (p.errors = p.errors || []).push(...msg.params.errors.map(e => e.errorType + ':' + e.code)); }
    if (msg.method === 'Browser.downloadWillBegin') rec.downloads.push({ guid: msg.params.guid, suggested: msg.params.suggestedFilename });
    if (msg.method === 'Browser.downloadProgress') { const d = rec.downloads.find(x => x.guid === msg.params.guid); if (d) d.state = msg.params.state; }
  };
  b.listeners.push(on);
  for (const d of ['Page', 'Runtime', 'Inspector', 'Log', 'Media']) { try { await b.send(d + '.enable', {}, sessionId); } catch (e) { rec['enable_' + d] = String(e.message); } }
  await b.send('Page.navigate', { url }, sessionId);
  let result = null;
  while (Date.now() - t0 < timeoutMs) {
    if (rec.crashes.length) break;
    try {
      const r = await b.send('Runtime.evaluate', { expression: `(() => { const r = ${waitExpr}; return r ? JSON.stringify(r) : '' })()`, returnByValue: true }, sessionId);
      if (r.result && r.result.value) { result = JSON.parse(r.result.value); break; }
    } catch (e) { if (b.exited) break; }
    await sleep(500);
  }
  rec.ms = Date.now() - t0; rec.timedOut = !result && !rec.crashes.length;
  await sleep(300);
  try {
    const shot = await b.send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: true }, sessionId);
    fs.writeFileSync(path.join(out, name + '.png'), Buffer.from(shot.data, 'base64')); rec.screenshot = name + '.png';
  } catch (e) { rec.screenshotError = String(e.message); }
  b.listeners.splice(b.listeners.indexOf(on), 1);
  try { await b.send('Target.closeTarget', { targetId }); } catch (e) {}
  rec.result = result;
  return rec;
}

(async () => {
  const qs = `config=${encodeURIComponent(config)}${armed ? '&armed=1' : ''}`;
  const summary = { config, armed, exe, flags: CONFIGS[config].flags, started: new Date().toISOString(), pages: {}, browsers: [] };
  const groups = [];
  const normal = pages.filter(p => p !== 'webrtc'); const rtc = pages.includes('webrtc');
  const FAKE_AV = ['--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream'];
  if (normal.length) groups.push({ name: 'main', flags: [], pages: normal });
  if (rtc) groups.push({ name: 'webrtc', flags: FAKE_AV, pages: ['webrtc'] });
  // index.html runs webrtc.html in an iframe too, so its browser gets the fake camera / microphone as well
  if (has('index')) groups.push({ name: 'index', flags: FAKE_AV, pages: [], index: true });
  for (const g of groups) {
    const b = new Browser(g.name, g.flags);
    await b.start();
    for (const p of g.pages) {
      const url = pathToFileURL(path.join(KIT, p + '.html')).href + '?' + qs;
      const rec = await runPage(b, url, p, 'window.__kitResult', pageTimeout);
      summary.pages[p] = rec;
      const r = rec.result;
      log(`${p.padEnd(13)} ${r ? r.verdict.padEnd(4) : (rec.crashes.length ? 'CRASH' : 'TIMEOUT')} checks ${r ? r.checks.length : 0} bad ${r ? r.checks.filter(c => !c.ok).length : '-'}` +
          ` planted ${r ? r.planted : '-'} ${rec.ms}ms crashes ${rec.crashes.length} console-errors ${rec.consoleErrors.length}` + (r && r.tier ? ` tier ${r.tier}` : ''));
      fs.writeFileSync(path.join(out, p + '.json'), JSON.stringify(rec, null, 1));
      if (b.exited) { log(`[${g.name}] browser process exited: ${JSON.stringify(b.exits)}`); break; }
    }
    if (g.index && !b.exited) {
      // the webrtc page inside index.html needs the fake-device flags: the index run gets them too
      const url = pathToFileURL(path.join(KIT, 'index.html')).href + '?' + qs;
      const rec = await runPage(b, url, 'index', 'window.__kitIndexResult', pageTimeout * 6);
      summary.index = rec;
      log(`index         ${rec.result ? rec.result.verdict : (rec.crashes.length ? 'CRASH' : 'TIMEOUT')} ` + (rec.result ? rec.result.pages.map(x => x.page + ':' + x.verdict).join(' ') : '') + ` ${rec.ms}ms`);
      fs.writeFileSync(path.join(out, 'index.json'), JSON.stringify(rec, null, 1));
    }
    summary.browsers.push({ name: g.name, argv: b.argv, exits: b.exits, crashEvents: b.events.filter(e => /Crashed/.test(e.method)).length });
    await b.stop();
    summary.browsers[summary.browsers.length - 1].exitsAfterStop = b.exits;
    if (!has('keep-profile')) fs.rmSync(b.profile, { recursive: true, force: true });
  }
  summary.finished = new Date().toISOString();
  fs.writeFileSync(path.join(out, 'summary.json'), JSON.stringify(summary, null, 1));
})().catch(e => { log('RUNNER ERROR ' + (e.stack || e)); process.exit(1); });
