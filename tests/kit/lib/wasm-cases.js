// Venetium test kit - wasm.html helper: the cases. Plain JS (no %-natives), written so that the same file also runs in Node on the
// host (venetium/tests/kit-tools/wasm/node_check.js) - the host's second opinion (V8 on x64) for the extra cases.
//   s10 cases    caseI32 / caseI64: the bodies of the two run() calls of s10/jit/wasm.js, unchanged
//   ext cases    digestOps / spinProbe / memProbe / jsMemory / memScript: extra coverage for what a 32-bit target does differently
//                (64-bit operators incl. i64 shifts and rotates by variable and by constant amounts around 31/32/33, i64 <-> f32/f64
//                conversions, bounds-checked memory: narrow / extending / unaligned / float / static-offset / constant-address loads
//                and stores, a hot loop)
//   shape        memShape / opsShape: what was EXECUTED (phases, step counts, functions, operator groups) against the host's record of what
//                the generator wrote (data ext.mem.expect, ext.expect), so that a data file that lost a phase or a group cannot pass by omission
// An ext "digest" is  <fnv1a-32 hex of "<result>," for every input tuple, a trap counting as "T">/<tuples>/<traps>.
(function (g) {
  'use strict';
  const REPS = 4;        // s10/jit/wasm.js: every case runs 4 times and must give one answer
  function bytes(b64) { const bin = atob(b64); const u = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i); return u; }

  // ---- s10/jit/wasm.js: the two cases (the loop bodies are verbatim; only the var e is a parameter)
  function caseI32(e) {
    var s = 0, traps = 0;
    for (var i = -3000; i < 3000; i++) {
      var a = (i * 715827883) | 0, b = (i % 13) - 6;
      s = (s + e.add32(a, 0x7fffffff) + e.mul32(a, 0x10001)) | 0;
      try { s = (s + e.div32(a, b)) | 0; } catch (x) { traps++; }
    }
    try { e.div32(-2147483648, -1); } catch (x) { traps += 1000; }
    return s + ',' + traps;
  }
  function caseI64(e) {
    var s = 0n, traps = 0;
    for (var i = -500n; i < 500n; i++) {
      var a = BigInt.asIntN(64, i * 1234567890123456789n);
      s = BigInt.asIntN(64, s + e.add64(a, 0x7fffffffffffffffn) + e.mul64(a, 3n));
      var b = (i % 9n) - 4n;
      try { s = BigInt.asIntN(64, s + e.div64(a, b)); } catch (x) { traps++; }
    }
    try { e.div64(-9223372036854775808n, -1n); } catch (x) { traps += 1000; }
    return s + ',' + traps;
  }
  // run(name, f) of s10: REPS runs; the answer is the last, UNSTABLE when one differs from the first
  function reps(f) { const vals = []; for (let r = 0; r < REPS; r++) vals.push(String(f())); return vals; }

  // ---- ext: operator digests
  function fnv(h, s) { for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619) >>> 0; } return h; }
  function sets(D) {
    return { V64: D.ext.sets.V64.map(BigInt), VF: D.ext.sets.VF.map(BigInt), VSH: D.ext.sets.VSH.map(BigInt), V32: D.ext.sets.V32.map(Number) };
  }
  function digestOp(e, o, S) {
    const f = e[o.fn], inp = S[o.inputs], two = o.sig.split('>')[0].length === 2;
    let h = 2166136261, traps = 0, n = 0;
    const one = (args) => {
      n++;
      let tok;
      try { tok = String(f.apply(null, args)); }
      catch (x) { if (!(x instanceof WebAssembly.RuntimeError)) throw x; traps++; tok = 'T'; }
      h = fnv(h, tok + ',');
    };
    if (two) { for (let i = 0; i < inp.length; i++) for (let j = 0; j < inp.length; j++) one([inp[i], inp[j]]); }
    else for (let i = 0; i < inp.length; i++) one([inp[i]]);
    return h.toString(16).padStart(8, '0') + '/' + n + '/' + traps;
  }
  function digestOps(e, D) { const S = sets(D), out = {}; for (const o of D.ext.ops) out[o.fn] = digestOp(e, o, S); return out; }

  // ---- ext: the hot loop. spin(n) runs max(n,1) iterations of x = x*1664525 + 1013904223 (i32), x0 = 0.
  function spinProbe(e, D) { const out = {}; for (const n of D.ext.spin.n) out[n] = e.spin(n); return out; }
  // memloop(n): the same shape with word loads and stores in the first page (bounds-checked). It writes the memory, so every call gets a
  // fresh instance of the module (the code - and its tier - belongs to the module, so a fresh instance does not restart the tiering).
  function memloopProbe(mod, D) { const out = {}; for (const n of D.ext.memloop.n) out[n] = new WebAssembly.Instance(mod).exports.memloop(n); return out; }

  // ---- ext: memory through wasm loads and stores. Returns [name, want, got] triples; wants are literals.
  function trap(f) {
    try { f(); return 'no trap'; }
    catch (x) {
      const n = String(x && x.name);
      return n === 'RuntimeError' && !(x instanceof WebAssembly.RuntimeError) ? 'RuntimeError (not a WebAssembly.RuntimeError)' : n;
    }
  }
  function memProbe(e) {
    const out = [], t = (name, want, got) => out.push([name, String(want), String(got)]);
    const mem = e.mem;
    t('memory starts at 1 page: size()', 1, e.size());
    t('mem.buffer.byteLength', 65536, mem.buffer.byteLength);
    e.st32(100, 0x12345678);
    t('st32(100, 0x12345678) then ld32(100)', 0x12345678, e.ld32(100));
    const old = mem.buffer, u8 = new Uint8Array(old);
    t('little-endian bytes at 100..103', '120,86,52,18', [u8[100], u8[101], u8[102], u8[103]].join());
    e.st64(8, -2n);
    t('st64(8, -2n) then ld64(8)', '-2', e.ld64(8));
    t('...the same word through DataView.getBigInt64(8, true)', '-2', new DataView(old).getBigInt64(8, true));
    t('ld32(65532): the last word is in bounds', 0, e.ld32(65532));
    t('ld32(65533): straddles the end', 'RuntimeError', trap(() => e.ld32(65533)));
    t('ld32(65536): first byte past the end', 'RuntimeError', trap(() => e.ld32(65536)));
    t('ld32(-1): address 0xFFFFFFFF', 'RuntimeError', trap(() => e.ld32(-1)));
    t('ld64(65528): the last doubleword is in bounds', 0, e.ld64(65528));
    t('ld64(65529): straddles the end', 'RuntimeError', trap(() => e.ld64(65529)));
    t('st32(65533, 1): straddles the end', 'RuntimeError', trap(() => e.st32(65533, 1)));
    t('...and the trapped store wrote nothing: ld32(65532)', 0, e.ld32(65532));
    t('grow(1) returns the old size', 1, e.grow(1));
    t('size() after growing', 2, e.size());
    t('the old mem.buffer is detached by growth: byteLength', 0, old.byteLength);
    t('the new mem.buffer.byteLength', 131072, mem.buffer.byteLength);
    t('ld32(65536): in bounds since the growth', 0, e.ld32(65536));
    e.st32(131068, 0x7eadbeef);
    t('last word of page 2: st32 then ld32', 0x7eadbeef, e.ld32(131068));
    t('ld32(131069)', 'RuntimeError', trap(() => e.ld32(131069)));
    t('grow(1) up to the 3-page maximum returns 2', 2, e.grow(1));
    t('grow(1) beyond the maximum returns -1', -1, e.grow(1));
    t('size() is still 3', 3, e.size());
    t('data written before growth survives: ld32(100)', 0x12345678, e.ld32(100));
    t('ld32(196604): last word of page 3', 0, e.ld32(196604));
    t('ld32(196605)', 'RuntimeError', trap(() => e.ld32(196605)));
    return out;
  }
  // ---- the JS API's own Memory: 64 MiB in a 32-bit address space
  function jsMemory() {
    const out = [], t = (name, want, got) => out.push([name, String(want), String(got)]);
    const m = new WebAssembly.Memory({ initial: 1, maximum: 1024 });
    t('Memory({initial: 1, maximum: 1024}).buffer.byteLength', 65536, m.buffer.byteLength);
    t('grow(1023) returns the old size', 1, m.grow(1023));
    t('byteLength after growing to 1024 pages (64 MiB)', 67108864, m.buffer.byteLength);
    const v = new Uint32Array(m.buffer);
    v[0] = 0xcafebabe; v[v.length - 1] = 0xdeadbeef;
    t('first and last word of the 64 MiB buffer read back', '3405691582,3735928559', v[0] + ',' + v[v.length - 1]);
    t('grow(1) beyond the maximum throws', 'RangeError', trap(() => m.grow(1)));
    return out;
  }
  // ---- ext: the memory script (data/wasm-data.js ext.mem, generated by the host's python MemModel): [phase, fn, args, want] steps on ONE fresh instance.
  // want: a decimal result, 'ok' (no result) or 'T' (a WebAssembly.RuntimeError). '@bytes' [start, n] reads the memory through the JS API instead (hex).
  // Returns [phase, step name, want, got, function] per step executed.
  function memScript(e, M) {
    const out = [], hex = u8 => Array.prototype.map.call(u8, b => (b < 16 ? '0' : '') + b.toString(16)).join('');
    for (const st of M.script) {
      const phase = st[0], fn = st[1], args = st[2], want = st[3];
      let got, name;
      if (fn === '@bytes') { name = '@bytes[' + args[0] + ',+' + args[1] + ']'; got = hex(new Uint8Array(e.mem.buffer, Number(args[0]), Number(args[1]))); }
      else {
        const ps = M.fns[fn].split('>')[0], a = args.map((s, i) => ps[i] === 'L' ? BigInt(s) : Number(s));
        name = fn + '(' + args.join(',') + ')';
        try { const r = e[fn].apply(null, a); got = r === undefined ? 'ok' : String(r); }
        catch (x) { got = x instanceof WebAssembly.RuntimeError ? 'T' : 'EXC ' + (x && x.name); }
      }
      out.push([phase, name, want, got, fn]);
    }
    return out;
  }
  // ---- shape: what was EXECUTED against the host's record of what was generated (data ext.mem.expect and ext.expect, written by gen_wasm_data.py).
  // The page emits one row per phase / per group by iterating the data, so a data file that lost a phase or a group would make those rows
  // disappear instead of failing. One real row per list compares the executed runs, in order, with the generator's list, name by name and
  // count by count. Pure functions of (results, data): node_check.js runs the same code on the host. Each returns { ok, want, got } for Kit.check.
  function runs(labels) { const out = []; for (const l of labels) { const t = out[out.length - 1]; if (t && t[0] === l) t[1]++; else out.push([l, 1]); } return out; }
  const total = r => r.reduce((a, p) => a + p[1], 0);
  const brief = s => String(s).split(/[:,]/)[0];                    // 'D float loads and stores: bit patterns ...' -> 'D float loads and stores'
  const runsText = (r, unit) => r.length + ' ' + (unit === 'steps' ? 'phases' : 'groups') + ': ' + r.map(p => brief(p[0]) + ' ' + p[1]).join(', ') + ' = ' + total(r) + ' ' + unit;
  function runsDiff(want, got) {
    const out = [], w = new Map(want.map(p => [p[0], p[1]])), g = new Map(got.map(p => [p[0], p[1]]));
    for (const [n, c] of want) { if (!g.has(n)) out.push(brief(n) + ' MISSING (host ' + c + ')'); else if (g.get(n) !== c) out.push(brief(n) + ': ' + g.get(n) + ' executed, host ' + c); }
    for (const [n, c] of got) if (!w.has(n)) out.push('UNEXPECTED ' + brief(n) + ' (' + c + ')');
    if (!out.length && JSON.stringify(want) !== JSON.stringify(got)) out.push('the same names and counts in a different order or split');
    return out;
  }
  function memShape(res, M) {
    const E = M.expect;
    if (!E) return { ok: false, want: "the host's record of the phases (data ext.mem.expect)", got: 'absent from the data file' };
    const got = runs(res.map(r => r[0])), table = Object.keys(M.fns), called = new Set(res.map(r => r[4])), never = table.filter(f => !called.has(f));
    const bad = runsDiff(E.phases, got);
    if (res.length !== E.steps) bad.push(res.length + ' steps executed, host ' + E.steps);
    if (table.length !== E.fns) bad.push(table.length + ' memory functions in the table, host ' + E.fns);
    if (never.length) bad.push('never called: ' + never.slice(0, 4).join(', ') + (never.length > 4 ? ', ...' : ''));
    return { ok: bad.length === 0,
             want: runsText(E.phases, 'steps') + '; ' + E.fns + ' of ' + E.fns + ' memory functions called',
             got: runsText(got, 'steps') + '; ' + (table.length - never.length) + ' of ' + table.length + ' memory functions called' + (bad.length ? ' [' + bad.join('; ') + ']' : '') };
  }
  // got: the digests digestOps produced (by function name); the host holds one answer per operator (D.ext.host_python)
  function opsShape(got, D) {
    const E = D.ext.expect;
    if (!E) return { ok: false, want: "the host's record of the operator groups (data ext.expect)", got: 'absent from the data file' };
    const ran = D.ext.ops.filter(o => Object.prototype.hasOwnProperty.call(got, o.fn)), r = runs(ran.map(o => o.group));
    const hostFns = Object.keys(D.ext.host_python), unanswered = hostFns.filter(k => !Object.prototype.hasOwnProperty.call(got, k));
    const bad = runsDiff(E.groups, r);
    if (ran.length !== E.ops) bad.push(ran.length + ' operators digested, host ' + E.ops);
    if (hostFns.length !== E.ops) bad.push(hostFns.length + ' host answers, host record ' + E.ops);
    if (unanswered.length) bad.push('host answers never compared: ' + unanswered.slice(0, 4).join(', ') + (unanswered.length > 4 ? ', ...' : ''));
    return { ok: bad.length === 0,
             want: runsText(E.groups, 'operators') + ', ' + E.ops + ' of ' + E.ops + ' host digests compared',
             got: runsText(r, 'operators') + ', ' + (hostFns.length - unanswered.length) + ' of ' + hostFns.length + ' host digests compared' + (bad.length ? ' [' + bad.join('; ') + ']' : '') };
  }
  g.KitWasmCases = { REPS, bytes, caseI32, caseI64, reps, digestOps, spinProbe, memloopProbe, memProbe, jsMemory, memScript, memShape, opsShape, trap };
})(typeof window !== 'undefined' ? window : globalThis);
