// js-jit.html helper, NATIVES ONLY (needs --allow-natives-syntax; without it this file is a SyntaxError, so the page loads it only when
// Kit.natives is true). Reports which tiers this V8 build / flag set has switched on, so the page can tell "Maglev is not built" (ia32)
// from "Maglev is built but the flags turned it off". Not part of s10\jit: those three files are copied verbatim (js-jit-core/status/force.js).
globalThis.__jitProbe = {
  sparkplug: %IsSparkplugEnabled(),
  maglev: %IsMaglevEnabled(),     // false when V8 is built without Maglev (ia32: v8_build_config.json "has_maglev": false) or --no-maglev
  turbofan: %IsTurbofanEnabled(),
};
