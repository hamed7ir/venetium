# lld fixes for windows arm32 thinlto

nobody had built windows arm32 with thinlto before, and lld broke on it in three places (all three are still in llvm
main). the v1.3 arm32 build links with llvm-rt2.1 plus these patches; everything else in the toolchain is unchanged.

| patch | file | what breaks without it |
|---|---|---|
| `0021-lld-coff-lto-armnt-static-reloc.patch` | `lld/COFF/LTO.cpp` | lld's lto backend used the pic relocation model for every coff machine except x86. thumb-2 pic cannot be expressed in coff: the /GS stack-cookie load becomes a pc-relative movw/movt and every stack-protected function fails with "Cannot represent this expression". the fix uses the static model on armnt too, the one clang compiles windows arm code with. |
| `0022-lld-coff-arm-delayload-helper-absolute.patch` | `lld/COFF/DLL.cpp` | the arm delay-load stub called `__delayLoadHelper2` with a `bl`, which reaches only 16 mb, and lld makes no range thunks for its own stubs. with lto the helper (a native crt object) ends up hundreds of mb away from the stubs: "relocation out of range" for chrome.dll. the fix calls it through `movw/movt r2, helper` + `blx r2`, with a base relocation. |
| `0024-cgprofile-skip-intrinsic-callees.patch` | `llvm/lib/Transforms/Instrumentation/CGProfile.cpp` | with pgo counts, arm's cost model says math intrinsics (exp2, pow, sin, log...) become calls, so the call-graph profile named `llvm.exp2.f64` and the link failed with an undefined symbol (v8's gc-tracer). only arm32 + pgo + coff hits it. it lives in llvm proper, so build clang with it too if you use pgo without lto. |

**0023, not a patch here:** `llvm-lib` (`lld-link /lib`) cannot archive windows arm32 bitcode either -
`getBitcodeFileMachine()` in `llvm/lib/ToolDrivers/llvm-lib/LibDriver.cpp` maps `Triple::arm` but not
`Triple::thumb`, and windows arm triples are always `thumbv7-*`. the fix is one line, `case Triple::thumb:` next to
`case Triple::arm:`. venetium 0062 works around it in chromium's toolchain instead (`llvm-ar` for arm32 lto archives),
so you do not need it.

## building it

apply the three patches to the llvm-project tree you build llvm-rt2.1 from and build lld (0024 is in llvm's
instrumentation library, which lld links). i only rebuilt `LTO.cpp`, `DLL.cpp`, `CGProfile.cpp` and lld's main
against the prebuilt rt2.1 libraries with llvm's own msvc release flags; with the fixes left out, that rebuild gave
byte-identical outputs to the shipped lld-link on native and delay-load links, so nothing else changed. point
`clang_base_path` at the toolchain with the fixed lld (see `args/out-arm-pgo.gn`).

## upstream arm32 fixes worth taking in the next toolchain

these are arm32 wrong-code fixes that are missing from llvm 23.1.1 (they are in v1.2 too):

- ARMLoadStoreOptimizer forms vldmia while the base register is still live - llvm-project #223630 / #223754,
  fixed by #226937 (main only).
- in 23.1.4 but not 23.1.1: arm mac miscompile for 64-bit values (#231702), dagcombiner usubo_carry borrow chain
  (#223396, #224759), selectiondag undef bitcast lanes, instcombine sinkNotIntoOtherHandOfLogicalOp (#226783),
  globalsaa (#213631), simplifylibcalls.

backend-only fixes (the first four) keep the pgo profile valid. middle-end ones (instcombine, globalsaa,
simplifylibcalls) run before the profile is applied and can change function hashes: after taking those, gather a new
profile (see `pgo/README.md`).
