// Venetium test kit — the V8 configurations (BATCH-X86-2 §1). The launchers (launchers\*.cmd) and the runner (runner\run.js) use
// exactly these flags, passed as --js-flags. The JS configurations are f1probe.py's CONFIGS; the two wasm ones are the JIT
// comparison's wasm tiers. `tier` / `who`: which tier the F1 probe must prove, and on which functions (callee or caller).
// `ia32: 'n/a'`: V8 builds no Maglev for ia32 (X86-1 Finding 7) — those configurations are not applicable on x86 and apply on ARM32.
window.KIT_CONFIGS = {
  'default':         { flags: [],                                                                       natives: false, tier: null },
  'jitless':         { flags: ['--jitless', '--allow-natives-syntax'],                                   natives: true,  tier: 'interpreter', who: 'callee', nowasm: true },
  'ignition':        { flags: ['--no-sparkplug', '--no-maglev', '--no-turbofan', '--allow-natives-syntax'], natives: true, tier: 'interpreter', who: 'callee' },
  'sparkplug':       { flags: ['--always-sparkplug', '--no-maglev', '--no-turbofan', '--allow-natives-syntax'], natives: true, tier: 'sparkplug', who: 'callee' },
  'maglev':          { flags: ['--allow-natives-syntax'],                                                natives: true,  tier: 'maglev', who: 'callee', ia32: 'n/a' },
  'turbofan':        { flags: ['--allow-natives-syntax'],                                                natives: true,  tier: 'turbofan', who: 'callee' },
  'maglev-caller':   { flags: ['--allow-natives-syntax', '--no-maglev-inlining'],                        natives: true,  tier: 'maglev', who: 'caller', ia32: 'n/a' },
  'turbofan-caller': { flags: ['--allow-natives-syntax', '--no-turbo-inlining', '--no-maglev'],          natives: true,  tier: 'turbofan', who: 'caller' },
  // the two wasm configurations carry natives syntax only so that wasm.html can prove the wasm tier (%IsLiftoffFunction /
  // %IsTurboFanFunction); they select no JS tier (tier: null), so js-f1 and js-jit report their JS tier proof as not applicable
  'liftoff-only':    { flags: ['--liftoff-only', '--allow-natives-syntax'],                             natives: true,  tier: null, wasmTier: 'liftoff' },
  'no-liftoff':      { flags: ['--no-liftoff', '--allow-natives-syntax'],                               natives: true,  tier: null, wasmTier: 'turbofan' },
};
window.KIT_PAGES = ['js-f1', 'js-jit', 'wasm', 'images', 'media', 'canvas-webgl', 'webrtc', 'storage', 'fonts'];
