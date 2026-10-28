# building venetium

this is how i build venetium from chromium 150 source. it is a general guide - it does not depend on my machine or
my own scripts, so you can reproduce it on your own windows x64 build box.

venetium is an arm32 (armnt) build for old windows arm tablets - surface rt (tegra 3) and surface 2 (tegra 4),
running windows 10 build 15035. the hard part is not chromium, it is the toolchain: stock llvm and stock rustc both
miscompile things for windows arm32, and the browser crashes on the device because of it. so you need my fixed
toolchain. everything else is normal chromium plus my patches.

## what you need

**a windows x64 machine** to build on - you cross-compile arm32 from x64, the way chromium is always built.

**the fixed toolchain (this is the important part):**

- **llvm-rt2.1** - my patched llvm / clang / lld. stock llvm has real arm32-windows codegen bugs: the control-flow
  guard tables, the c++ exception unwind tables, and the integer-division helpers all come out wrong, and chrome
  crashes on the device from them. llvm-rt2.1 fixes all three. point `clang_base_path` at it. do not use stock
  clang for the arm32 build - it will compile fine and then crash on the device.

- **rustc built with llvm-rt2.1.** chromium 150 has rust code in it, and that rust has to go through the same fixed
  llvm, or the rust objects get the same arm32 miscompiles. so build rustc from source against llvm-rt2.1, build a
  `thumbv7a-pc-windows-msvc` std for it, and point `rust_sysroot_absolute` at that sysroot. build bindgen the same
  way (it is an x64 host tool) and point `rust_bindgen_root` at it.
  
https://github.com/hamed7ir/llvm-rt1/releases/tag/rt2.1-23.1.1


**visual studio 2022 build tools** - the msvc headers and libs, the windows sdk, and `csc` (used by the installer).
you also need the **ATL** and **ARM ATL** components (`base/win/atl_throw.cc` needs `atldef.h`, and the arm build
needs the arm atl libs), and the arm32 sdk libs (i build with the 10.0.19041 arm libs and the 10.0.26100 headers).

**the visual c++ 2015-2022 redistributable (x64 + x86)** on the build box - gn and the rust tools need it to run.
this is for the build box only; the shipped browser is statically linked and needs no runtime on the device.

**python 3**, **git for windows** (turn symlinks and long paths on - the chromium tree has thousands of symlinks),
and **gn + ninja** (depot_tools has them; you do not need to bootstrap depot_tools).

## get the source

get chromium **150.0.7871.226** - either fetch it with depot_tools at that tag, or take google's official source
tarball and apply the .222 -> .226 delta. confirm you are on it: `chrome/VERSION` has `PATCH=226`.

## apply the patches

apply `patches/0001` through `patches/0063` in order (`0064` is for the thumb2 build, see the end). they are plain `git apply` patches. apply them with
`core.autocrlf=false` so the files stay byte-for-byte - arm32 is sensitive and i hash every file. the missing
numbers (0009, 0024, 0031, 0034, 0038) are patches i dropped or replaced along the way; the old versions are kept
under `patches/superseded/` for history, you do not apply those.

## set the gn args

make your output directory with `args/out-arm-rt21.gn` as its `args.gn` (that file already appends
`args/venetium.gn`). edit the toolchain paths in it to your own, then `gn gen out\arm-rt21`. the parts that matter:

- `target_cpu = "arm"`
- `clang_base_path` -> your llvm-rt2.1
- `rust_sysroot_absolute` -> your rustc-rt2.1 sysroot; `rust_bindgen_root` -> your bindgen
- arm32 renders through angle on d3d9 (the tegras, feature level 9_1) or on d3d11 at feature level 9_3 (adreno,
  0045), so: `enable_swiftshader=false`, `angle_enable_wgpu=false`, `dawn_use_swiftshader=false`,
  `dawn_use_built_dxc=false`. there is no d3d12/webgpu on these gpus, and the windows sdk ships no 32-bit-arm
  `dxil.dll`, so dxc has to stay off.
- codecs on: `proprietary_codecs=true`, `ffmpeg_branding="Chrome"` (so h.264 / aac work). both come from
  `args/venetium.gn`, so the x86 build gets them too.
- `is_official_build = true` (in `args/venetium.gn`): a real release build, the way google builds chrome -
  `chrome://version` says "official build". the out-dir files keep their own `use_thin_lto = false`,
  `chrome_pgo_phase` and `symbol_level = 1`, which win over what official turns on.
- arm32: `v8_enable_builtins_optimization = true` - v8's own builtins profile. google has no chrome pgo profile for
  arm32, so `out-arm-rt21.gn` has `chrome_pgo_phase = 0`.
- **arm32 release build (v1.3): `args/out-arm-pgo.gn`** = `out-arm-rt21.gn` with pgo + thinlto: `use_thin_lto =
  true`, `chrome_pgo_phase = 2` with my own arm32 profile (`pgo_data_path`, see `pgo/README.md` - unpack it into
  `chrome/build/pgo_profiles/` first), `win_thin_lto_jobs` (0062) and `toolchain_supports_rust_thin_lto = false`
  (rust stays native). its `clang_base_path` must be llvm-rt2.1 with the lld fixes in `toolchain/rt21-lto/` - the
  unfixed lld cannot link windows arm32 thinlto at all. on a surface 2 this build scores about 50% higher in
  speedometer than the same code without pgo/lto. `out-arm-rt21.gn` stays the quick build for testing patches.
- x86: `chrome_pgo_phase = 2` - google's profile-guided optimization data for chrome on 32-bit windows. the file
  name is in `chrome/build/win32.pgo.txt`; put that file into `chrome/build/pgo_profiles/` before `gn gen`
  (download `https://storage.googleapis.com/chromium-optimization-profiles/pgo_profiles/<that name>`, or let
  depot_tools' pgo hook fetch it). it is about 480 mb, and rt2.1's llvm-profdata reads it.

## build

put ninja and the toolchain on your PATH, then:

```
ninja -C out\arm-rt21 chrome
```

that gives you the whole browser: `venetium.exe`, `chrome.dll`, the angle dlls, and the resources.

the pgo + thinlto build is the same command on `out\arm-pgo`. with thinlto the compile step only writes bitcode and
all of chrome.dll's machine code is made in its final link, so that one link takes a long time and a lot of memory;
`win_thin_lto_jobs` sets its threads. on my build server the whole build took about 21 hours at -j8.

**one gotcha with the official build from the source tarball:** near the end, the build makes a resource allowlist
from `chrome.dll.pdb` with `third_party/llvm-build/Release+Asserts/bin/llvm-pdbutil`. in the tarball that file is a
linux binary sitting next to the windows `llvm-pdbutil.exe`, and windows picks the extensionless one, so the step fails
with `WinError 193 ... not a valid Win32 application`. rename the linux one (i call it `llvm-pdbutil.linux-elf`) and
run ninja again. don't turn the allowlist off instead (`enable_resource_allowlist_generation`): grit uses that arg too,
so changing it recompiles everything.

**the ad-blocker list (0059).** the blocker reads `venetium_filters\filters.dat` next to `venetium.exe`. it is made
from easylist + easyprivacy (https://easylist.to/easylist/easylist.txt and .../easyprivacy.txt) by chromium's own
`ruleset_converter`. build it once for x86 (it runs on the x64 build box) and convert:

```
ninja -C out\x86-rt21 components/subresource_filter/tools:ruleset_converter
out\x86-rt21\ruleset_converter.exe --input_format=filter-list --output_format=unindexed-ruleset --input_files=easylist.txt,easyprivacy.txt --output_file=filters.dat
```

then make a `venetium_filters` folder in each out dir with `filters.dat`, a one-line `version.txt` (i use
`easylist-<its ! Version>.easyprivacy-<its ! Version>`; a new version makes the browser re-index the list once) and
`package/venetium_filters/LICENSE.txt`. `chrome.release` lists the three files, so the zip and the installer pick
them up. no folder = no blocking, everything else works.

## package

(the commands use `out\arm-rt21`; for the v1.3 release build use `out\arm-pgo`.)

- **portable zip:** `package/portable.py zip out\arm-rt21 arm32 venetium-arm32.zip --kit tests/kit --v8-config out\arm-rt21\clang_x86_v8_arm\v8_build_config.json`
- **installer:** `installer/build-installer.ps1 -OutDir out\arm-rt21` builds `Venetium-Setup-<ver>-arm32.exe` - one
  self-contained exe that runs on arm32 windows / rt (see `installer/` for how it does that).

## the patches - what they do and why

i keep venetium as a patch series on chromium 150. here is what each group is for.

**branding and defaults (0001-0008)** - make it venetium instead of chromium: rename the exe to `venetium.exe`,
the product name, icons, and the permanent windows identity (see VENETIUM-IDS.md). turn off google sign-in (gaia)
and the google-api-key nag, make translation a switch, and set venetium's defaults.

**v8 on arm32 (0010-0011)** - v8 needs help to generate correct arm code on windows: build the x86 host snapshot
tool for an arm target, and fix the invoke-prologue flags. without these v8 miscompiles and chrome dies early.

**get it building on x86 first (0012-0015)** - before touching arm i got it clean on x86 with the fixed toolchain:
fix the compile errors the stricter clang catches, fix a double-encryption bug in process-bound strings, and set
the user-agent brand to venetium so sites that sniff the brand still work (google sign-in refuses an unknown one).

**the arm32 port (0016-0027)** - the real work. 0016 wires up the windows arm32 toolchain. then: get past
chromium's 32-bit-arm platform blockers, the v8 windows-arm32 build, the sandbox's instruction interception (it
rewrites cpu instructions and needs the arm32 encodings), crashpad on arm32, **the d3d9 / angle path (0021)**
because the tegra has no d3d11, ffmpeg wired for windows-arm, and the link fixes for the arm32 sdk libs.

**get it to launch on the device (0028-0030)** - windows 10 15035 on the tegra is a strange pre-release build.
0028 stops chrome_elf installing a dll-load hook that breaks there; 0029 and 0030 stop the sandbox from using a
process-mitigation attribute and a win32k claim that 15035's arm32 kernel refuses. these took it from "crashes
instantly" to "the window opens."

**gpu and smooth video (0032-0037, 0039)** - the tegra's d3d9 driver is shader-model 2 only, so: cache failed
shader compiles instead of retrying them, pick the strongest sm2 profile angle can use, accept 7 varyings (the
driver's limit) instead of demanding 8, keep the skia shader cache on, show "(32-bit)" in the version page, and
turn gpu rasterization on for tegra 4. this is what stops pages and video from crawling.

**hardware and software video (0040-0042)** - 0041 is hardware h.264 decode over d3d9 (dxva / media foundation):
the tegra decodes h.264 in its gpu, so 720p plays smooth. 0040 is an opt-in "prefer h.264" switch, off by default,
because most of youtube is vp9/av1 now and forcing h.264 would break those. 0042 moves the yuv->rgb step onto the
gpu for software-decoded vp9/av1, so software video is lighter on the cpu.

**the renderer sandbox (0043)** - on arm32 the strongest renderer token breaks windows 10's parallel dll loader
(its worker threads can't open the dlls under that token), so the renderer dies before it loads a page. 0043 gives
the arm32 renderer the same token the gpu process already runs with - still a real sandbox, but the loader works.
(0031 and 0038 were earlier attempts at this and at the cfg crash; they are dropped, under `patches/superseded/`.)

**the issues-1 batch (0044-0047)** - fixes for the first round of reports on the public repo. 0044 is the sandbox fix
again but for utility processes: the crx unpacker (data_decoder / unzip) was dying under the same lockdown token 0043
fixed for the renderer, so extensions wouldn't install - 0044 gives those utilities the user_limited token too, still a
real sandbox. 0046 adds a "renderer sandbox" row to chrome://version so you can actually see the sandbox is on (it reads
"on - arm32 user_limited"). 0045 and 0047 are for adreno gpus (lumia phones and the like) that expose d3d11 at feature
level 9_3: 0045 brings angle's d3d11 backend back on arm32 with a one-shot probe that only picks d3d11 at fl 9_3 (tegra
stays on d3d9, no change), and 0047 bridges the hardware h.264 decoder's output into that d3d11 device. tegra is
untouched by both.

**the second batch (0048-0051)** - the next round of device reports.

0048 is the big one: a data-alignment crash that only hit arm32. blink's garbage-collected collection backings
(HeapVectorBacking / HeapHashTableBacking) are empty tag types whose address *is* the element array, and cppgc was
aligning them to 4 bytes on 32-bit. an element with an 8-byte member (a double, an int64) then landed at a 4-byte
address, and arm32's ldrd / vldr fault on that (x86-32 just tolerates it). that is why recaptcha, telegram web, css
animations and some extensions crashed the tab. 0048 makes those backings carry alignof(element) on arm32 (8 bytes
when the element needs it), and keeps the now-8-byte-aligned ones out of the compaction space, which cppgc won't let
you align past the default. no-op off arm32. device-confirmed fixed.

0049 turns gpu rasterization on for every arm32 device that draws through angle's d3d9, not just the surface 2. 0039
had scoped it to the tegra 4's gpu id; 0049 drops the id check so the surface rt (tegra 3) and any other arm32 d3d9
device get it too. correctness is the same across these ps_2.0 d3d9 gpus. device-confirmed on the tegra 3
(chrome://gpu reads "hardware accelerated"). `--disable-gpu-rasterization` still turns it off.

0050 is the real fix for web store extensions. ungoogled had inverted a helper (`IsExtensionDownload` returned
`ShouldDownloadAsRegularFile()`, which is false unless you pass a flag), so the installer rejected every .crx with
"download was not a valid extension or user script". 0050 negates it to match the sibling download path. this, not
the utility-process sandbox (0044), turned out to be what actually blocked installs - with 0050 extensions install
and run. device-confirmed. (cross-arch: x86 gets this one too.)

0051 lets the adreno gpu (the lumias) rasterize on its d3d11 fl9_3 path - the one 0045 brings up - scoped to the
adreno 330 gpu id from a community d3dprobe report (vendor 0x4d4f4351, device 0x46363432). tegra is untouched; x86
has no such gpu.

**the third batch (0052-0053)** - the webcam, and the fixes from a review of the whole series.

0052 makes the webcam work on arm32. the desktop media foundation capture path opens these cameras (the tegra's
nvidia avstream camera, the lumias) but can never start streaming them - every format fails with
MF_E_HW_MFT_FAILED_START_STREAMING, even unsandboxed - so getUserMedia said "could not start video source". 0052 adds
a winrt MediaCapture capture backend for arm32 (wrl only, no exceptions): a MediaFrameReader on the camera's
VideoPreview source, frames copied off the gpu surface and handed to chromium as nv12 / yuy2, with photo capture as a
slow fallback. the probe measured 30 fps on both tegra cameras (photo capture was ~2 fps). camera enumeration stays on
media foundation, and x86 capture is unchanged. one gotcha: don't set MemoryPreference=Cpu on these cameras, it makes
the hardware mft refuse to start.

0053 is the review round: small, mostly arm32 fixes to earlier patches. the 0047 d3d11 decode bridge could never run
(its probe only looked at angle's d3d9 device) and had no sync between the two devices - fixed, but it stays behind
`--enable-features=VenetiumDxvaD3D11Bridge` until it is tested on an adreno. 0049's gpu-raster exception is scoped to
the arm32 gpu vendors (nvidia, qualcomm), so x86 with `use-angle=d3d9` no longer picks it up. on arm32 the
process mitigations that 0029 lost at process creation are applied by the child after startup instead, best-effort.
the sandbox's ntdll hooks flush the instruction cache, crashpad's arm32 CaptureContext records the right pc / cpsr and
the vfp registers, angle's failed-shader cache is keyed by signature (no hlsl rebuilt per draw), the d3d11 probe no
longer creates a device, cpuinfo reports fma/idiv on the cores that have them (tegra 4, krait), the dxva decoder keeps
6 picture buffers instead of 8, and three flag-text typos are fixed.

**the fourth batch (0054-0061)** - speed, mostly for 2 gb tablets. this is also the first full official build, so
build it from scratch (no incremental tricks: a patch that adds a string shifts every later string id, and only a
full build recompiles everything that uses them).

0054 stops treating every 2 gb machine as permanently short of memory: windows' pressure thresholds (moderate below
1000 mib free, critical below 400) now scale with the installed ram below 4 gib, renderers get v8's
`--max-semi-space-size=8` instead of `--optimize-for-size`, and on angle's d3d9 backend the swap interval is set
once instead of being toggled (each toggle rebuilt the whole window's swap chain). 0055 steers 720p+ video to the
hardware h.264 decoder on arm32 (vp9 level 3.1+ and av1 level 4+ report unsupported when the codec string has a
level, and vp9/av1 above 480p count as not smooth until real stats exist; feature VenetiumArm32CodecSteering), and
gives software h.264 / vp9 up to 4 decode threads on 32-bit. 0056 turns off the hidden tab-search preload, the
per-anchor navigation-predictor metrics and (arm32) tab hover-card images, and memory saver discards idle tabs after
an eighth of the usual time on low-end machines. 0057 serves a page's own preconnect / dns-prefetch hints while
"preload pages" is off. 0058 is the small stuff: no delayed dawn/dx12 gpu info collection, cache budgets for 1-2 gb,
always qpc on x86, the d3d9 fallback on x86, fewer component registrations and a 10-minute first update check, and
mksnapshot warns instead of failing on a builtins-profile mismatch. 0059 is the ad and tracker blocker: chromium's
subresource_filter with an all-sites preset and the list from `venetium_filters\` (see "the ad-blocker list" above).
0060 takes automatic stack-variable initialization out of the dsp-only codec targets (jpeg simd, dav1d, libvpx
intrinsics, libwebp dsp). 0061 replaces the arm32 crt's byte-at-a-time memchr / memcmp / strlen / strchr / strcmp
with word-at-a-time versions (`build/win/venetium_string_arm.c`, linked into every arm32 binary).

**0062 (v1.3)** - only build files, for the arm32 pgo + thinlto build; non-lto builds do not change. it adds the
`win_thin_lto_jobs` arg, gives every arm32 binary the full post-link optimization (lto level 2: the thinlto compile
step leaves vectorizing and unrolling to the link), archives arm32 bitcode with `llvm-ar` because `llvm-lib` cannot
(it does not know the `thumbv7` triple, see `toolchain/rt21-lto/README.md`), and keeps 0061's string routines out of
lto so they stay the device-tested native code. the profile is in `pgo/`, the linker fixes in `toolchain/rt21-lto/`.

**0063 (v1.3)** - `Math.pow` on windows arm32 uses v8's own pow. v8 normally calls the c runtime's `pow`, and the arm32
ucrt's `pow` returns 0 when the result is subnormal (`Math.pow(10, -310)` was 0 in every tier, jitless too). found by
the thumb-2 v8 test run on a surface rt; the same change is device-tested there. other platforms are unchanged.

## the thumb2 build (windows rt 8.0 / 8.1)

windows rt 8.x only runs thumb-2 code, so this build has v8 ported to thumb-2 and a few windows 8 fixes. on top of
0001-0063:

1. apply `patches/0064-windows-rt-8.patch` (the chromium side: pe subsystem 6.02, run-time lookups for the 8.1/10-only
   functions, version gates, delay-load stand-ins - details in the patch header).
2. apply the v8 port from [v8-thumb2-rt](https://github.com/hamed7ir/v8-thumb2-rt):
   `git -c core.autocrlf=false apply --directory=v8 v8-thumb2.patch`
3. same args as the arm32 pgo build (`args/out-arm-pgo.gn`), then build `chrome`.

the v1.3 thumb2 files were built incrementally from the arm32 pgo output dir, so they use the same pgo profile (v8's
changed code just gets less profile benefit).
