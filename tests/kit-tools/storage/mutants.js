// Mutation checks for kit\storage.html (server only; not part of the kit that goes to the device).
//   node mutants.js <scratch-dir> <mutation> [--run]      node = F:\cr\src\third_party\node\win\node.exe
// Copies the page, kit\lib and the runner to <scratch-dir>\<mutation>\kit and \runner (never touching the real ones), applies ONE
// mutation to the copy, and prints (or, with --run, executes) the runner command against the copy; --run writes the run to
// F:\cr\device-1\runs\mut-storage-<mutation>. Every replacement must match exactly once, or the tool stops, so a refactor of the page
// cannot silently turn a mutation into a no-op. What each mutation must do (EXPECT) is printed with it. Most mutations inject a
// <script> that breaks the BROWSER's behaviour (a corrupting storage, a lost database), which is what the page's checks are for.
'use strict';
const fs = require('fs'), path = require('path'), { spawnSync } = require('child_process');
const KIT = path.resolve(__dirname, '..', '..', 'kit'), RUNNER = path.resolve(__dirname, '..', '..', 'runner');
const HEAD = '<script src="lib/kit.js"></script>';
const inject = js => [[HEAD, HEAD + '\n<script>' + js + '</script>']];
const M = {
  // localStorage returns the big value with one character changed
  'ls-corrupt': { rep: inject("{ const g = Storage.prototype.getItem; Storage.prototype.getItem = function (k) { const v = g.call(this, k); return k === 'kit-storage-big' && v ? String.fromCharCode(v.charCodeAt(0) ^ 1) + v.slice(1) : v; }; }"),
    expect: 'FAIL: only "localStorage: a 98304-character value read back = the host CRC-32" is bad' },
  // IndexedDB stores the big values with one byte changed
  'idb-corrupt': { rep: inject("{ const p = IDBObjectStore.prototype.put; IDBObjectStore.prototype.put = function (v, k) { if (v instanceof Uint8Array && v.length > 100000) { v = v.slice(); v[1000] ^= 1; } return p.call(this, v, k); }; }"),
    expect: 'FAIL: only the 300000-byte IndexedDB read-back check is bad (the 40000-byte one stays ok)' },
  // every closed IndexedDB connection takes its database with it: the data is not in the database
  'idb-reopen-lost': { rep: inject("{ const c = IDBDatabase.prototype.close; IDBDatabase.prototype.close = function () { const n = this.name; c.call(this); indexedDB.deleteDatabase(n); }; }"),
    expect: 'FAIL: "IndexedDB: after close() and a new open() the value is still there" and both big-value read-backs are bad' },
  // a blob: URL that does not resolve
  'blob-url-broken': { rep: inject("{ const c = URL.createObjectURL; URL.createObjectURL = function (b) { return b.type === 'text/javascript' ? 'blob:null/00000000-0000-4000-8000-000000000000' : c.call(this, b); }; }"),
    expect: 'FAIL: only "a Blob read back through its blob: URL (loaded and run as a script)" is bad (the script behind the blob: URL did not load); the download is still clicked' },
  // the host value of the localStorage payload is wrong by one bit
  'host-wrong': { rep: [["crc: '9cb487ba'", "crc: '9cb487bb'"]],
    expect: 'FAIL: the generator check and the localStorage read-back check of the localStorage payload are bad' },
  // the JS engine computes Math.imul wrongly: the generated payloads are not the host's
  'engine-wrong': { rep: inject("Math.imul = function (a, b) { return ((a * b) | 0) ^ 1; };"),
    expect: 'FAIL: the generator checks (JS engine, no storage) are bad AND the read-backs against the host are bad (engine, not storage)' },
  // localStorage is denied (a SecurityError, as on a file:// document without storage)
  'ls-denied': { rep: inject("Object.defineProperty(window, 'localStorage', { get() { throw new DOMException('denied', 'SecurityError'); } });"),
    expect: 'FAIL: "localStorage available | SecurityError: denied"; the IndexedDB and Blob checks still run' },
};
const args = process.argv.slice(2);
const [outDir, name] = args;
if (!outDir || !M[name]) { console.error('usage: node mutants.js <scratch-dir> <' + Object.keys(M).join('|') + '> [--run]'); process.exit(2); }
let html = fs.readFileSync(path.join(KIT, 'storage.html'), 'utf8');
for (const [from, to] of M[name].rep) {
  const n = html.split(from).length - 1;
  if (n !== 1) { console.error('mutation anchor matches ' + n + ' times (need 1): ' + from); process.exit(1); }
  html = html.replace(from, () => to);
}
const root = path.resolve(outDir, name), mk = path.join(root, 'kit'), mr = path.join(root, 'runner');
fs.rmSync(root, { recursive: true, force: true });
fs.mkdirSync(mk, { recursive: true }); fs.cpSync(path.join(KIT, 'lib'), path.join(mk, 'lib'), { recursive: true }); fs.cpSync(RUNNER, mr, { recursive: true });
fs.writeFileSync(path.join(mk, 'storage.html'), html);
const out = 'F:\\cr\\device-1\\runs\\mut-storage-' + name;
const runArgs = [path.join(mr, 'run.js'), '--config', 'default', '--pages', 'storage', '--out', out];
console.log('mutation ' + name + ' written to ' + root + '\nEXPECT: ' + M[name].expect);
if (args.includes('--run')) process.exit(spawnSync(process.execPath, runArgs, { stdio: 'inherit' }).status || 0);
console.log('run: ' + process.execPath + ' ' + runArgs.join(' '));
