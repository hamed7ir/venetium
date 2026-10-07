// Preamble (needs --allow-natives-syntax): force each checked function into the optimizing tier before it is measured.
// V8 15 has no --always-turbofan / --always-maglev; this is how V8's own tests force a tier. With
// --optimize-on-next-call-optimizes-to-maglev the next call compiles with Maglev, otherwise with TurboFan.
globalThis.__force = function (f) {
  %PrepareFunctionForOptimization(f);
  f(); f();
  %OptimizeFunctionOnNextCall(f);
};
