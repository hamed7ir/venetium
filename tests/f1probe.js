// Venetium F1 probe (BATCH-REDO-1 §5 step 22). Run by f1probe.py; needs --allow-natives-syntax.
//
// F1: on Windows ARM32, MacroAssembler::InvokePrologue (v8/src/codegen/arm/macro-assembler-arm.cc) enters its
// argument-copy loop through a branch that reads the condition flags, and the Windows-only AllocateStackSpace()
// before it ends in `cmp bytes, #4096`, which clobbers them: a call that passes FEWER arguments than the function
// declares never copies its arguments. `f3(1, 2)` then sees `[global], 1, 2` instead of `1, 2, undefined`.
// Venetium 0011 sets the flags from the argument count before that branch.
//
// What this checks, for every call: each declared parameter's value (missing ones must be undefined), the callee's
// arguments.length, and that the receiver never shows up among the parameters. Calls: 0 of 2, 1 of 3, 2 of 3
// (F1's own pattern), 2 of 5 (under-application) and 4 of 2, 6 of 3 (over-application); each from a sloppy and a
// strict callee, each as a plain call and as a method call on a marker receiver R. 6 x 2 x 2 = 24 calls, 3 checks
// each = 72 checks per configuration.
//
// The configuration is chosen by the runner (flags) and by F1_MODE (set with -e before this file):
//   jitless, ignition, sparkplug        callees run in that tier (flags pick it)
//   maglev, turbofan                    callees are forced into that tier before the measured calls
//   maglev-caller, turbofan-caller      the measured calls come from callers forced into that tier (inlining off)
// Output (stdout): CHECK lines, TIER lines (%GetOptimizationStatus, decoded), one SUMMARY line.

var MODE = globalThis.F1_MODE || 'default';
var R = { tag: 'R' };

function S2(a, b) { return [this, arguments.length, a, b]; }
function S3(a, b, c) { return [this, arguments.length, a, b, c]; }
function S5(a, b, c, d, e) { return [this, arguments.length, a, b, c, d, e]; }
function T2(a, b) { 'use strict'; return [this, arguments.length, a, b]; }
function T3(a, b, c) { 'use strict'; return [this, arguments.length, a, b, c]; }
function T5(a, b, c, d, e) { 'use strict'; return [this, arguments.length, a, b, c, d, e]; }
R.S2 = S2; R.S3 = S3; R.S5 = S5; R.T2 = T2; R.T3 = T3; R.T5 = T5;
var CALLEES = [S2, S3, S5, T2, T3, T5];

// Every call site has a literal argument count (no spread, no apply): [name, declared, passed args, target, caller].
// The caller receives its target (a function, or a property name of R for method calls), so the call site's target is not a
// compile-time constant. In the *-caller modes each call site is first warmed up with all six targets (megamorphic), so the
// optimized caller calls through V8's generic Call builtin — the path that runs InvokePrologue. (With a single constant
// target, Maglev and TurboFan pad missing arguments in the caller and never reach InvokePrologue.)
var KEYS = ['S2', 'S3', 'S5', 'T2', 'T3', 'T5'];
var CALLS = [
  ['sloppy plain 0of2',  2, [],                 S2,   function (f) { return f(); }],
  ['sloppy plain 1of3',  3, [1],                S3,   function (f) { return f(1); }],
  ['sloppy plain 2of3',  3, [1, 2],             S3,   function (f) { return f(1, 2); }],
  ['sloppy plain 2of5',  5, [1, 2],             S5,   function (f) { return f(1, 2); }],
  ['sloppy plain 4of2',  2, [1, 2, 3, 4],       S2,   function (f) { return f(1, 2, 3, 4); }],
  ['sloppy plain 6of3',  3, [1, 2, 3, 4, 5, 6], S3,   function (f) { return f(1, 2, 3, 4, 5, 6); }],
  ['sloppy method 0of2', 2, [],                 'S2', function (k) { return R[k](); }],
  ['sloppy method 1of3', 3, [1],                'S3', function (k) { return R[k](1); }],
  ['sloppy method 2of3', 3, [1, 2],             'S3', function (k) { return R[k](1, 2); }],
  ['sloppy method 2of5', 5, [1, 2],             'S5', function (k) { return R[k](1, 2); }],
  ['sloppy method 4of2', 2, [1, 2, 3, 4],       'S2', function (k) { return R[k](1, 2, 3, 4); }],
  ['sloppy method 6of3', 3, [1, 2, 3, 4, 5, 6], 'S3', function (k) { return R[k](1, 2, 3, 4, 5, 6); }],
  ['strict plain 0of2',  2, [],                 T2,   function (f) { return f(); }],
  ['strict plain 1of3',  3, [1],                T3,   function (f) { return f(1); }],
  ['strict plain 2of3',  3, [1, 2],             T3,   function (f) { return f(1, 2); }],
  ['strict plain 2of5',  5, [1, 2],             T5,   function (f) { return f(1, 2); }],
  ['strict plain 4of2',  2, [1, 2, 3, 4],       T2,   function (f) { return f(1, 2, 3, 4); }],
  ['strict plain 6of3',  3, [1, 2, 3, 4, 5, 6], T3,   function (f) { return f(1, 2, 3, 4, 5, 6); }],
  ['strict method 0of2', 2, [],                 'T2', function (k) { return R[k](); }],
  ['strict method 1of3', 3, [1],                'T3', function (k) { return R[k](1); }],
  ['strict method 2of3', 3, [1, 2],             'T3', function (k) { return R[k](1, 2); }],
  ['strict method 2of5', 5, [1, 2],             'T5', function (k) { return R[k](1, 2); }],
  ['strict method 4of2', 2, [1, 2, 3, 4],       'T2', function (k) { return R[k](1, 2, 3, 4); }],
  ['strict method 6of3', 3, [1, 2, 3, 4, 5, 6], 'T3', function (k) { return R[k](1, 2, 3, 4, 5, 6); }],
];

function show(v) {
  if (v === globalThis) return '[global]';
  if (v === R) return '[R]';
  if (v === undefined) return 'undefined';
  return String(v);
}

function tier(f) {
  var s = %GetOptimizationStatus(f);
  var t = (s & 32) ? 'turbofan' : (s & 16) ? 'maglev' : (s & 16384) ? 'sparkplug' : (s & 64) ? 'interpreter' : 'other';
  return t + ((s & 4) ? '+maybe-deopted' : '') + '(' + s + ')';
}

// Bring the right functions into the right tier before the measured calls (warm-up results are not checked).
if (MODE === 'maglev' || MODE === 'turbofan') {
  CALLEES.forEach(function (f) { %PrepareFunctionForOptimization(f); });
  CALLS.forEach(function (c) { c[4](c[3]); c[4](c[3]); });
  CALLEES.forEach(function (f) {
    if (MODE === 'maglev') { %OptimizeMaglevOnNextCall(f); } else { %OptimizeFunctionOnNextCall(f); }
  });
} else if (MODE === 'maglev-caller' || MODE === 'turbofan-caller') {
  CALLS.forEach(function (c) {
    var targets = (typeof c[3] === 'string') ? KEYS : CALLEES;   // all six targets: a megamorphic call site
    %PrepareFunctionForOptimization(c[4]);
    for (var r = 0; r < 2; r++) targets.forEach(function (t) { c[4](t); });
    if (MODE === 'maglev-caller') { %OptimizeMaglevOnNextCall(c[4]); } else { %OptimizeFunctionOnNextCall(c[4]); }
  });
}

var checks = 0, bad = 0;
function check(name, what, ok, got, want) {
  checks++;
  if (!ok) bad++;
  print('CHECK ' + MODE + ' | ' + name + ' | ' + what + ' | ' + (ok ? 'OK' : 'BAD') + ' | got ' + got + ' | want ' + want);
}

CALLS.forEach(function (c) {
  var name = c[0], declared = c[1], passed = c[2];
  var res = c[4](c[3]);                   // the measured call
  var argc = res[1], params = res.slice(2);
  var want = [];
  for (var i = 0; i < declared; i++) want.push(i < passed.length ? passed[i] : undefined);
  var got = params.map(show).join(','), exp = want.map(show).join(',');
  check(name, 'params', got === exp, got, exp);
  check(name, 'arguments.length', argc === passed.length, argc, passed.length);
  var leaked = params.some(function (p) { return p === R || p === globalThis; });
  check(name, 'receiver not among params', !leaked, leaked ? 'leaked' : 'none', 'none');
});

if (MODE === 'maglev-caller' || MODE === 'turbofan-caller') {
  CALLS.forEach(function (c) { print('TIER caller ' + c[0] + ' ' + tier(c[4])); });
} else {
  CALLEES.forEach(function (f) { print('TIER callee ' + f.name + ' ' + tier(f)); });
}
print('SUMMARY ' + MODE + ' checks=' + checks + ' bad=' + bad);
