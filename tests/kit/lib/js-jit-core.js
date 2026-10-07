// BATCH-V8SIM-2 §3.6 — JIT answer comparison, core JavaScript. Every function runs REPS times (so the default tiers warm
// up and tier up); the printed value is the last result, and 'stable' says whether every repetition agreed.
// Integer results are recomputed independently on the host by s10/jit/expected.py; every tier must match it and each other.
var REPS = 4;
function run(name, f) {
  var first, last, stable = true;
  // preamble hooks (s10/jit/status.js, force.js; need --allow-natives-syntax): force the tier, then report the tier reached
  if (typeof globalThis.__force === 'function') globalThis.__force(f);
  for (var r = 0; r < REPS; r++) {
    var v = String(f());
    if (r === 0) first = v; else if (v !== first) stable = false;
    last = v;
  }
  print(name + '=' + last + (stable ? '' : ' UNSTABLE(first=' + first + ')'));
  if (typeof globalThis.__status === 'function') print('tier.' + name + '=' + globalThis.__status(f));
}
// the SpiderMonkey multiply-overflow case, verbatim
run('overflow', function () { var t = 0; for (var i = 0; i < 20000; i++) t = (t + i * i) | 0; return t; });
// integer division and modulo, negative dividends included (JS truncates toward zero)
run('divmod', function () {
  var s = 0;
  for (var i = -5000; i < 5000; i++) { var b = (i % 7) + 11; s = (s + ((i / b) | 0) * 3 + (i % b)) | 0; }
  return s;
});
// bit operations and shifts around 2^31, incl. >>>
run('bits', function () {
  var s = 0;
  for (var i = 0; i < 4000; i++) {
    var x = (i * 2654435761) | 0;
    s = (s ^ (x << 3) ^ (x >> 5) ^ (x >>> 7)) | 0;
    s = (s + (x >>> 31) + (x >> 31)) | 0;
  }
  return s;
});
run('bitconst', function () {
  var one = 1, m1 = -1, big = 0x80000000, mn = -2147483648, neg1 = -1;
  return [one << 31, (one << 31) >>> 0, m1 >>> 0, big | 0, big >> 1, big >>> 1, (mn / neg1) | 0, (0xFFFFFFFF + 2) | 0].join(',');
});
run('imul', function () {
  var s = 0;
  for (var i = 0; i < 5000; i++) s = (s + Math.imul((i * 123457) | 0, 0x7fffffff - i)) | 0;
  return s;
});
// a string hash h*31|0
run('strhash', function () {
  var str = '';
  for (var i = 0; i < 300; i++) str += String.fromCharCode(32 + (i * 7) % 90);
  var h = 0;
  for (var r = 0; r < 20; r++) for (var j = 0; j < str.length; j++) h = (h * 31 + str.charCodeAt(j)) | 0;
  return h;
});
// array and object loops
run('arrobj', function () {
  var a = []; for (var i = 0; i < 2000; i++) a.push(i * 3 - 1000);
  var s = 0; for (var r = 0; r < 5; r++) for (var i = 0; i < a.length; i++) s = (s + a[i] * (r + 1)) | 0;
  var o = {}; for (var i = 0; i < 500; i++) o['k' + i] = i * i;
  var t = 0; for (var k in o) t = (t + o[k]) | 0;
  var objs = []; for (var i = 0; i < 1000; i++) objs.push({ x: i, y: -i * 2 });
  var u = 0; for (var i = 0; i < objs.length; i++) u = (u + objs[i].x * objs[i].y) | 0;
  return [s, t, u].join(',');
});
// typed arrays: Int32 wrap, Uint32 wrap, Uint8Clamped clamp
run('typed', function () {
  var i32 = new Int32Array(1000), u32 = new Uint32Array(1000), u8c = new Uint8ClampedArray(1000);
  for (var i = 0; i < 1000; i++) { i32[i] = i * 3000007; u32[i] = -i * 7; u8c[i] = i - 500; }
  var a = 0, b = 0, c = 0;
  for (var i = 0; i < 1000; i++) { a = (a + i32[i]) | 0; b = (b + u32[i]) % 4294967296; c += u8c[i]; }
  return [a, b, c].join(',');
});
// BigInt.asUintN(64, ...) arithmetic
run('bigint', function () {
  var x = 0n;
  for (var i = 0n; i < 300n; i++) x = BigInt.asUintN(64, x * 6364136223846793005n + 1442695040888963407n + i);
  var y = BigInt.asIntN(64, x);
  return [x, y, x >> 13n, x % 1000003n].join(',');
});
// a regular expression: match count and a hash of a replace
run('regex', function () {
  var s = '';
  for (var i = 0; i < 400; i++) s += (i % 3 == 0 ? 'ab' : 'c') + i + (i % 5 == 0 ? 'X' : 'y');
  var m = s.match(/a?b\d+[Xy]/g);
  var r = s.replace(/(\d)(\d)/g, '$2$1');
  var h = 0; for (var j = 0; j < r.length; j++) h = (h * 33 + r.charCodeAt(j)) | 0;
  return m.length + ',' + h;
});
// try / catch / throw in a loop
run('trycatch', function () {
  var c = 0, s = 0;
  for (var i = 0; i < 3000; i++) {
    try { if (i % 7 == 3) throw new Error('e' + i); if (i % 11 == 5) throw i; s = (s + i) | 0; }
    catch (e) { c++; if (typeof e === 'number') s = (s - e) | 0; }
  }
  return c + ',' + s;
});
// doubles: Math.sqrt and division are correctly rounded IEEE-754, so the host can reproduce them; toFixed(9)
run('doubles', function () {
  var s = 0; for (var i = 1; i < 3000; i++) s += Math.sqrt(i) / i;
  return [s.toFixed(9), (1 / 3).toFixed(9), Math.sqrt(2).toFixed(9), (0.1 + 0.2).toFixed(9), (2.5).toFixed(0), (1e21 / 7).toString()].join(',');
});
// cross-tier only (libm-dependent, not recomputed on the host)
run('sin_crosstier', function () { var t = 0; for (var i = 0; i < 2000; i++) t += Math.sin(i) * 0.5; return t.toFixed(12); });
print('done=core');
