# the arm32 pgo profile

`venetium-arm32-20261026.profdata.xz` is the profile-guided-optimization data for the arm32 build: what the browser
really runs on a tegra, counted function by function. the v1.3 arm32 build (`args/out-arm-pgo.gn`) is compiled with
it and with thinlto. on my surface 2 that took speedometer from about 1.20 (v1.2) to about 1.79.

| file | sha256 |
|---|---|
| `venetium-arm32-20261026.profdata.xz` (42 mb, xz) | `56c7f8ec377037edccff4d876939a17c8a4486cf6853ea4c71590ca71ffa54fa` |
| `venetium-arm32-20261026.profdata` (454 mb, unpacked) | `f67c2bf5880cdab5899c325c5f8ded563e30a39e4b5a2146372c1c323f25fe43` |

## use it

unpack it (`xz -dk venetium-arm32-20261026.profdata.xz`, or 7-zip), put `venetium-arm32-20261026.profdata` into
the chromium tree's `chrome\build\pgo_profiles\`, and build with `args/out-arm-pgo.gn`, which points
`pgo_data_path` at it.

`pgo_data_path` has to be set: chromium's `build/config/compiler/pgo/BUILD.gn` maps every windows cpu except arm64
and x64 to "win32", so without it an arm32 build would quietly take google's x86 profile, which matches nothing.

the profile fits chromium 150.0.7871.226 with patches 0001-0062 and llvm-rt2.1. functions a later patch changes just
lose their profile (clang skips a function whose hash does not match), so small patches are fine. after a new
chromium version, or a toolchain change in llvm's middle end, gather a new one.

## how i made it

1. **an instrumented build:** `args/out-arm-rt21.gn` with `chrome_pgo_phase = 1` (and `use_thin_lto = false`).
   llvm-rt2.1 ships no profile runtime for arm32, so i built `clang_rt.profile-arm.lib` from the windows part of
   `compiler-rt/lib/profile` (`InstrProfiling*.c`, `InstrProfilingPlatformWindows.c`, `WindowsMMap.c`,
   `InstrProfilingRuntime.cpp`; `--target=armv7-pc-windows-msvc`, `/MT`, `-I compiler-rt/lib/profile -I
   compiler-rt/include`, then `llvm-lib`) and put it into the clang runtime folder (`lib/clang/23/lib/windows`).
   phase 1 also instruments v8's x86 host tools (mksnapshot, torque), and their link asks for the plain
   `clang_rt.profile.lib`: build the same sources with `--target=i386-pc-windows-msvc -m32` and install that as
   `clang_rt.profile.lib` and `clang_rt.profile-i386.lib`.
2. **run it on the device.** the instrumented `chrome.dll` is about 1 gb: rt2.1's clang is built without zlib, so it
   cannot compress the profile's function names. it still ran on the 2 gb tegra. i ran it with
   `LLVM_PROFILE_FILE=C:\prof\ven-%m.profraw` (`%m` merges into one file per binary; `%p` would write one per
   process and fill the disk) and `--no-sandbox` so the renderers can write their counts, then did speedometer,
   ordinary browsing and video for 10-15 minutes and closed the browser normally (a crash loses the counts).
3. **merge:** `llvm-profdata merge -o venetium-arm32.profdata *.profraw` with rt2.1's llvm-profdata. six raw files
   went in; the result has 1,266,964 functions. the hottest code is blink's css selector matching, v8's scanner and gc,
   partitionalloc and mojo/ipcz.
