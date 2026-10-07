// Venetium test kit — the shared reporter (BATCH-X86-2 §1). Every page:
//   <script src="lib/configs.js"></script><script src="lib/kit.js"></script>
//   Kit.check(name, want, got, ok)   one check; Kit.info(name, got) / Kit.na(name, why) never fail
//   Kit.want(rightValue, wrongValue) the expected value; when the page runs armed (?armed=1) the FIRST call returns wrongValue,
//                                    so exactly one real check must fail — every page must route one check through it
//   await Kit.done({extra})          publishes {page, config, armed, planted, checks, verdict, ...extra}
//   await Kit.arch()                 the CPU the browser reports + .ia32 (only ia32 may lack Maglev); never rejects, 3 s timeout
//   Kit.build                        window.KIT_BUILD (lib/build.js, written by the packaging step) or null when unknown
// Page URL parameters: ?config=<name from lib/configs.js>&armed=1. Results go to the parent (index.html) by postMessage only —
// every file:// document is its own opaque origin, so nothing is fetched; data lives in .js files loaded with <script src>.
// Standalone (no parent), the result is left in window.__kitResult for the runner (DevTools protocol) to read.
(function () {
  'use strict';
  const q = new URLSearchParams(location.search);
  const config = q.get('config') || 'default';
  const armed = q.get('armed') === '1';
  const known = !!(window.KIT_CONFIGS || {})[config];
  const cfg = (window.KIT_CONFIGS || {})[config] || { flags: [], natives: false };
  const page = decodeURIComponent(location.pathname.split('/').pop()).replace(/\.html$/, '');
  const checks = [];
  let planted = false, finished = false, table = null;
  const queued = [];
  const bodyReady = new Promise(r => { if (document.body) r(); else document.addEventListener('DOMContentLoaded', () => r()); });
  bodyReady.then(() => { for (const c of queued.splice(0)) row(c); });
  function row(c) {
    if (!document.body) { queued.push(c); return; }   // page scripts run in <head>; rows wait for <body>
    if (!table) {
      const style = document.createElement('style');
      style.textContent = 'body{font:14px/1.35 "Segoe UI",Arial,sans-serif;margin:12px}#kit-head{font-size:18px;font-weight:bold;margin:4px 0 8px}' +
        '#kit-table{border-collapse:collapse}#kit-table td,#kit-table th{border:1px solid #999;padding:2px 6px;font:12px Consolas,monospace;text-align:left;vertical-align:top;max-width:520px;overflow-wrap:break-word}' +
        '.kit-ok{background:#d7f5d7}.kit-bad{background:#f8d0d0}.kit-na{background:#eee}#kit-verdict{font-size:22px;font-weight:bold;margin:8px 0}';
      document.head.appendChild(style);
      const h = document.createElement('div'); h.id = 'kit-head';
      h.textContent = 'Venetium kit — ' + page + ' — config ' + config + (armed ? ' — ARMED' : '');
      const v = document.createElement('div'); v.id = 'kit-verdict'; v.textContent = 'running…';
      table = document.createElement('table'); table.id = 'kit-table';
      table.innerHTML = '<tr><th>check</th><th>want</th><th>got</th><th>ok</th></tr>';
      document.body.prepend(h, v, table);
    }
    const tr = document.createElement('tr');
    tr.className = c.na ? 'kit-na' : (c.ok ? 'kit-ok' : 'kit-bad');
    for (const x of [c.name, c.want, c.got, c.na ? 'n/a' : (c.ok ? 'yes' : 'NO')]) {
      const td = document.createElement('td'); td.textContent = String(x); tr.appendChild(td);
    }
    table.appendChild(tr);
  }
  function str(v) { try { return typeof v === 'string' ? v : JSON.stringify(v); } catch (e) { return String(v); } }
  const Kit = {
    page, config, armed, cfg, natives: !!cfg.natives, build: window.KIT_BUILD || null,
    want(v, wrong) { if (armed && !planted) { planted = true; return wrong; } return v; },
    check(name, want, got, ok) { const c = { name, want: str(want), got: str(got), ok: !!ok }; checks.push(c); row(c); return !!ok; },
    eq(name, want, got) { return Kit.check(name, want, got, str(want) === str(got)); },
    info(name, got) { const c = { name, want: '(info)', got: str(got), ok: true, info: true }; checks.push(c); row(c); },
    na(name, why) { const c = { name, want: 'n/a', got: str(why), ok: true, na: true }; checks.push(c); row(c); },
    loadScript(src) {
      return new Promise((res, rej) => { const s = document.createElement('script'); s.src = src; s.onload = res;
        s.onerror = () => rej(new Error('cannot load ' + src)); document.head.appendChild(s); });
    },
    bytes(b64) { const bin = atob(b64); const u = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i); return u; },
    blobUrl(b64, type) { return URL.createObjectURL(new Blob([Kit.bytes(b64)], { type })); },
    sleep(ms) { return new Promise(r => setTimeout(r, ms)); },
    // Chromium reports architecture 'x86' for both ia32 and x64; a 32-bit browser on 64-bit Windows reports bitness '64' and
    // wow64 true; Windows ARM32 reports architecture '' (no PROCESSOR_ARCHITECTURE_ARM case in base/win), bitness '64', wow64
    // false. So ia32 = 'x86' and (bitness '32' or wow64); anything else (x64, ARM32, ARM64, unknown, no answer) is not ia32.
    async arch() {
      const r = { architecture: null, bitness: null, wow64: null, ia32: false, note: '' };
      try {
        if (!navigator.userAgentData || !navigator.userAgentData.getHighEntropyValues) { r.note = 'no navigator.userAgentData'; return r; }
        const v = await Promise.race([navigator.userAgentData.getHighEntropyValues(['architecture', 'bitness', 'wow64']),
          new Promise((_, rej) => setTimeout(() => rej(new Error('no answer within 3 s')), 3000))]);
        r.architecture = v.architecture; r.bitness = v.bitness; r.wow64 = v.wow64;
        r.ia32 = v.architecture === 'x86' && (v.bitness === '32' || v.wow64 === true);
        if (v.architecture === '') r.note = 'the browser reported no architecture (Windows ARM32 reports none)';
      } catch (e) { r.note = 'getHighEntropyValues failed: ' + (e && e.message || e); }
      return r;
    },
    async done(extra) {
      if (finished) return window.__kitResult;
      finished = true;
      await bodyReady; await new Promise(r => setTimeout(r, 0));   // the queued rows are drawn first
      const real = checks.filter(c => !c.info && !c.na);
      if (armed && !planted) Kit.check('ARMED RUN: this page planted no wrong expectation (page bug)', 'planted', 'none', false);
      const verdict = real.length > 0 && checks.every(c => c.ok) ? 'PASS' : 'FAIL';
      const r = Object.assign({ page, config, armed, planted, checks, verdict, userAgent: navigator.userAgent }, extra || {});
      window.__kitResult = r;
      document.title = 'KIT ' + page + ' ' + verdict;
      if (!table) row({ name: '(no checks)', want: '', got: '', ok: false });
      document.getElementById('kit-verdict').textContent = 'verdict: ' + verdict + ' (' + checks.filter(c => c.ok).length + '/' + checks.length + ' ok)';
      if (window.parent !== window) window.parent.postMessage({ kit: r }, '*');
      return r;
    },
  };
  // an unknown configuration name must not silently run as 'default'
  if (!known) Kit.check('configuration "' + config + '" is defined in lib/configs.js', 'a known configuration', config, false);
  // a page that throws before Kit.done still reports: the failed check at once, and the result (the checks so far) 5 s later
  // if the page has not finished by then — a prompt FAIL instead of index.html's per-page timeout
  let abortTimer = null;
  function abortSoon(why) { if (!finished && !abortTimer) abortTimer = setTimeout(() => { if (!finished) Kit.done({ aborted: why }); }, 5000); }
  window.addEventListener('error', e => { if (!finished) { Kit.check('uncaught error', 'none', (e.message || e) + ' @' + (e.filename || '').split('/').pop() + ':' + e.lineno, false); abortSoon('uncaught error'); } });
  window.addEventListener('unhandledrejection', e => { if (!finished) { Kit.check('unhandled rejection', 'none', String(e.reason && (e.reason.stack || e.reason)), false); abortSoon('unhandled rejection'); } });
  window.Kit = Kit;
})();
