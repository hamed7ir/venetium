# the arm32 pgo profile

the profile-guided-optimization data for the arm32 builds: what the browser really runs on a tegra, counted function
by function. the arm32 and thumb2 release builds (`args/out-arm-pgo.gn`) are compiled with it and with thinlto.

| file | release | sha256 |
|---|---|---|
| `venetium-arm32-20261122.profdata.xz` (48 mb, xz) | v1.4 | `200ef6b9c6a72559856d23d83c8e101ae8cd2f896907e88f2cd251c2d8c947c7` |
| `venetium-arm32-20261122.profdata` (463 mb, unpacked) | | `16dcf18894d6f8dc2439d2bd4e08c2f52548fc62e6d3dc447e9cc396203ebd21` |
| `venetium-arm32-20261026.profdata.xz` (42 mb, xz) | v1.3 | `56c7f8ec377037edccff4d876939a17c8a4486cf6853ea4c71590ca71ffa54fa` |
| `venetium-arm32-20261026.profdata` (454 mb, unpacked) | | `f67c2bf5880cdab5899c325c5f8ded563e30a39e4b5a2146372c1c323f25fe43` |

v1.3's profile took speedometer on my surface 2 from about 1.20 (v1.2) to about 1.79.

## use it

unpack it (`xz -dk venetium-arm32-20261122.profdata.xz`, or 7-zip), put `venetium-arm32-20261122.profdata` into
the chromium tree's `chrome\build\pgo_profiles\`, and build with `args/out-arm-pgo.gn`, which points
`pgo_data_path` at it.

`pgo_data_path` has to be set: chromium's `build/config/compiler/pgo/BUILD.gn` maps every windows cpu except arm64
and x64 to "win32", so without it an arm32 build would quietly take google's x86 profile, which matches nothing.

the v1.4 profile was gathered on chromium 150.0.7871.226 with patches 0001-0074 and llvm-rt2.3, and it fits rt2.4 too
(the same middle end: of 33,485 functions compiled with rt2.4, none had a different hash). functions a later patch
changes just lose their profile (clang skips a function whose hash does not match), so small patches are fine. after
a new chromium version, or a toolchain change in llvm's middle end, gather a new one.

## how i made the v1.4 profile

1. **two instrumented builds**, `chrome_pgo_phase = 1` without thinlto: arm32 from `args/out-arm-instr.gn`
   (= `out-arm-rt21.gn` + `chrome_pgo_phase = 1`), and the thumb2 build the same way (0064 and the v8 port on top).
   rt2.3 ships the arm32 and i386 profile runtimes and has zlib, so nothing had to be built by hand this time, and the
   instrumented `chrome.dll` is 662 mb instead of about 1 gb.
2. **profiling kits** (the `pgo-profiling-20261111` pre-release): `run-profile.cmd` sets
   `LLVM_PROFILE_FILE=C:\prof\ven-a32-%m.profraw` (thumb2 kit: `ven-t2-`) and starts venetium with `--no-sandbox`
   (every process has to write its counts) and a profile folder of its own. chromium writes the child processes'
   counts itself (`child_pool-0` to `-3`). both kits run on windows 10 15035 only: windows rt 8.1 cannot load the
   662 mb thumb2 image.
3. **the sessions:** **jwa4** did 36 on his surface 2 (tegra 4), 17 with each kit (speedometer 3.0 and 3.1,
   jetstream 2, motionmark 1.3, webgl aquarium, google maps, telegram, discord, twitter, reddit, dailymail, bbc
   persian, downloads + a pdf, an extension install, youtube (vp9 and h.264) and twitch), then jetstream 2 once more
   with each kit. i added photopea, squoosh, software video decoding (h.264 with
   `--disable-accelerated-video-decode`, vp9 from wikimedia webm files), jetstream 3 on the thumb2 kit, and more
   browsing with both kits.
4. **merge:** every file was checked first (the same modules and function hashes as these builds, plausible counts;
   one truncated file was left out), then ten sets went in with equal weight:
   `llvm-profdata merge -o venetium-arm32-20261122.profdata <sets>` with rt2.3's llvm-profdata. the result has
   1,267,442 functions, 304,237 of them with counts (v1.3's profile: 79,335), 2.95e11 counts in all. the video
   decoders have real counts now. angle (libGLESv2 / libEGL) never writes a profile, its gpu process ends without
   one, so 0076 builds angle without the profile.

## how i made the v1.3 profile

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
