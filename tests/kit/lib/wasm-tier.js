// Venetium test kit - wasm.html helper: V8 natives for the WebAssembly tier.
// USES %-NATIVES: wasm.html loads this file ONLY when Kit.natives is true (the configuration passes --allow-natives-syntax).
// Without that flag this file is a SyntaxError, so it must never be a <script src> of the page.
// The natives (v8/src/runtime/runtime-test-wasm.cc) crash the renderer unless given a function exported by a wasm instance, so every
// argument here comes from instance.exports of a module this page built.
(function (g) {
  'use strict';
  function status(f) {
    if (%IsTurboFanFunction(f)) return 'turbofan';
    if (%IsLiftoffFunction(f)) return 'liftoff';
    if (%IsUncompiledWasmFunction(f)) return 'uncompiled';
    return 'other';
  }
  g.KitWasmNatives = {
    status: status,
    tierUp: function (f) { %WasmTierUpFunction(f); },                     // compile f with TurboFan now, synchronously
    trapHandler: function () { return !!%IsWasmTrapHandlerEnabled(); },   // true: out-of-bounds traps via the signal handler; false: explicit bounds checks
    jit: function () { return { turbofan: !!%IsTurbofanEnabled(), sparkplug: !!%IsSparkplugEnabled(), maglev: !!%IsMaglevEnabled() }; },
  };
})(window);
