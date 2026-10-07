// js-jit.html helper, NATIVES ONLY, MAGLEV configurations only (maglev, maglev-caller), and only where Maglev is built (not ia32).
// s10\jit\force.js ends in %OptimizeFunctionOnNextCall, which compiles with TurboFan unless --optimize-on-next-call-optimizes-to-maglev is
// set (s10\jit\tiers.json passed it, with --max-opt=2). The kit's maglev configurations pass only --allow-natives-syntax (f1probe.py's
// CONFIGS), so this variant asks for Maglev directly, as f1probe.js does. Same shape as force.js: core.js's run() calls __force(f) first.
globalThis.__force = function (f) {
  %PrepareFunctionForOptimization(f);
  f(); f();
  %OptimizeMaglevOnNextCall(f);
};
