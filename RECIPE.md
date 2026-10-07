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

apply `patches/0001` through `patches/0043` in order. they are plain `git apply` patches. apply them with
`core.autocrlf=false` so the files stay byte-for-byte - arm32 is sensitive and i hash every file. the missing
numbers (0009, 0024, 0031, 0034, 0038) are patches i dropped or replaced along the way; the old versions are kept
under `patches/superseded/` for history, you do not apply those.

## set the gn args

make your output directory with `args/out-arm-rt21.gn` as its `args.gn` (that file already appends
`args/venetium.gn`). edit the toolchain paths in it to your own, then `gn gen out\arm-rt21`. the parts that matter:

- `target_cpu = "arm"`
- `clang_base_path` -> your llvm-rt2.1
- `rust_sysroot_absolute` -> your rustc-rt2.1 sysroot; `rust_bindgen_root` -> your bindgen
- arm32 is angle-on-d3d9 only, so: `enable_swiftshader=false`, `angle_enable_wgpu=false`,
  `dawn_use_swiftshader=false`, `dawn_use_built_dxc=false`. the tegra is d3d9 / feature level 9_1 - there is no
  d3d11/d3d12/webgpu, and the windows sdk ships no 32-bit-arm `dxil.dll`, so dxc has to stay off.
- codecs on: `proprietary_codecs=true`, `ffmpeg_branding="Chrome"` (so h.264 / aac work).

## build

put ninja and the toolchain on your PATH, then:

```
ninja -C out\arm-rt21 chrome
```

that gives you the whole browser: `venetium.exe`, `chrome.dll`, the angle d3d9 dlls, and the resources.

## package

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
