// Preamble (needs --allow-natives-syntax): report which tier a function's code is in after the run.
// Bits from v8/src/runtime/runtime.h:1124-1148 (OptimizationStatus): Maglevved 1<<4, TurboFanned 1<<5, Interpreted 1<<6,
// Baseline (Sparkplug) 1<<14, MaybeDeopted 1<<2.
globalThis.__status = function (f) {
  var s = %GetOptimizationStatus(f);
  var t = (s & 32) ? 'turbofan' : (s & 16) ? 'maglev' : (s & 16384) ? 'sparkplug' : (s & 64) ? 'interpreter' : 'other';
  return t + ((s & 4) ? '+maybe-deopted' : '') + '(' + s + ')';
};
