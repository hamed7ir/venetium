# RECIPE (live copy, `venetium\RECIPE.md`) — rebuild `F:\cr\src` exactly as the laptop has it (Venetium state-0010 + V8's test files + three output directories)

Written by BATCH-MOVE-1 §1.1 (2026-09-27, on the laptop). Every step is what SUPERMIUM-3 §1 and VENETIUM-1…4 actually
did, taken from the scripts and logs named in each step (all of them travel in the `supermium-rt` repo bundle, under
`D:\repo\supermium-rt\`). Replay it **in Git Bash**, in this order, on the same paths. The end state is proven by
`tree-manifest.tsv` (every file) and by two `git diff` hashes.

**Updated by BATCH-MOVE-1b (2026-09-29, on the laptop):** steps 13–17 are new — Venetium 0009–0010 (V8SIM-1), the
state-0010 check, V8's test files (V8SIM-2, group `v8-testdata`), the output directories `out\x86-arm-sim` and
`out\x86-rt21`, and the three build-graph fingerprints; step 11 now covers only the directories restored from `extras`.
The order in which the server compares things is at the end ("Comparison order"). Steps 1–10 are unchanged; step 11 is
narrowed and step 12 marked "state 0008 — before step 13".
The repo bundle is at **`219f0383…`** (2026-09-29), which carries `venetium\args\out-x86-rt21.gn` and the fingerprint tool
`s11\tools\fingerprint.sh`; `F:\move\args\out-x86-rt21.gn` and `F:\move\tools\fingerprint.sh` are byte-identical copies, for
convenience. **`F:\move\RECIPE.md` (this file) is newer than the bundle's `RECIPE.md`** (`955a7be4…`): it updates this
paragraph, two rows of the inputs table, step 15's description of the test files and step 16's source path for
`out-x86-rt21.gn`, and adds the `gn args --list` note in step 16 — use `F:\move\RECIPE.md`. Check each file by the sha256
given below.

**Moved into the repo by BATCH-REDO-1 §7 (2026-10-01, build server 2): this file, `venetium\RECIPE.md`, is the live recipe.**
It is MOVE-2's copy (`C:\move\RECIPE.md`, `2b72ec0e…`, now frozen — `C:\move\FROZEN.txt`) plus: the Git setting step 5
depends on; the fixes from MOVE-2 (`MOVE-2-RESULTS.md` Findings 1–4) and the first server's three; the ATL (G7) and Visual C++
runtime prerequisites; **Venetium 0011** and `state-0011` (new step 18); the fingerprints after ATL. The repo-root `RECIPE.md`
(`955a7be4…`) is older — do not use it. On the laptop and server the paths are the same.

**Updated by BATCH-DEVICE-1 (2026-10-04): step 31** — the portable packages (comparison item 18).
**Updated by BATCH-ARM-1 (2026-10-04, build server 2): steps 26–30** — the first Windows ARM32 build (G1 builtins, Venetium
0016–0027, the 69 generated ARM inputs, `out\arm-rt21`, the build) and comparison items 13–17.

| anchor | value |
|---|---|
| Supermium-only state (after step 9) — `git -C F:\cr\src diff \| sha256sum` | `adada54fafb2462435cfa66c74457adc1e3475fd3aabbf6eddcf8a23ead61912` |
| **Final state S3 (after step 10)** — same command | **`407cf0872923c741c241389f3365b5b96513352b49e58c5219ec7e813321ddcd`** |
| full-tree manifest (`F:\move\tree-manifest.tsv`, state 0008, step 12) | `09cd1a108c6cf61ce24fbc42f521d0531e28d3d981eb6db52ae2066a1956bb4b` (also in `tree-manifest.tsv.sha256` and `MOVE-1-RESULTS.md`) |
| **state-0010 (after step 14)** — `s9\state-0010.sha256` | 225 files match + 1 absent by design; `git diff` **stays `407cf087…`** (0009/0010 touch only `build/`, which the default index does not track) |
| **V8's test files (after step 15)** — `venetium\testdata\v8-17700469.tsv` | 14,095 files match; manifest sha256 `385917d96f0965ad4d1148c1dda14609af368708daab7ede7ba3a461f6a6717b` |
| **build-graph fingerprints (step 17)** | three normalized sha256 values, in step 17 |
| **state-0011 (after step 18)** — `s9\state-0011.sha256` | 226 files match + 1 absent by design; = state-0010 + `v8/src/codegen/arm/macro-assembler-arm.cc` (`b6f15964…`); `git diff` stays `407cf087…` |
| **fingerprints with ATL (G7) installed** | x86-arm-sim `bea57559…`, x86-rt21 `61a0de05…` (step 17); `out\x64-rt2` retired |

## Machine prerequisites (the laptop had all of these; BATCH-MOVE-2 §0.4 covers most)
- **Git for Windows 2.53.0.windows.3** — its Git Bash supplies `xz` 5.8.2 (`C:\Program Files\Git\mingw64\bin\xz.exe`).
- **Windows' own bsdtar** `C:\Windows\System32\tar.exe` — laptop: `bsdtar 3.8.8 - libarchive 3.8.8`. It is used because it
  creates **real symlinks** (the tree has 7,621 file + 26 directory symlinks); MSYS GNU tar would make copies.
  → the user running the extraction must be allowed to create symlinks (Developer Mode on, or an elevated shell).
- `git config --global core.symlinks true` (the laptop's global setting, s3\PROGRESS.md:67) and `LongPathsEnabled=1`.
- **Never pipe the tar stream through Windows PowerShell 5.1** — it re-encodes native output and corrupts it
  (s3\step_extract_mt.sh:9-10). Use Git Bash (or cmd).
- **Git `core.autocrlf`:** Git for Windows' installer writes `core.autocrlf=true` into the *system* gitconfig. Step 5 runs
  `git apply` before step 7's `git init`, outside any repository, where that setting makes `git apply` write the patched files
  with CRLF (33 of the 34 delta files; `DEPS` then hashes `f43a9535…`, not `cddd21a6…`). Step 5 therefore passes
  `-c core.autocrlf=false`, exactly as the laptop ran it (MOVE-2 Finding 1); build machines also set
  `git config --global core.autocrlf false` (REDO-1 §0).
- **Visual C++ 2015–2022 Redistributable, x64 and x86** (MOVE-2 Finding 3): `gn gen` copies its DLLs from
  `C:\Windows\System32` / `SysWOW64` into every output directory (`build/vs_toolchain.py`, `DEPOT_TOOLS_WIN_TOOLCHAIN=0`), and
  `rustc-rt2`/`rustc-rt21` (`rustc.exe`, `rustc_driver-*.dll`) and `bindgen-rt` import `VCRUNTIME140`/`MSVCP140` without carrying
  them. Install from `D:\Program Files\vs22buildtools\VC\Redist\MSVC\14.44.35112\vc_redist.x64.exe` and `vc_redist.x86.exe`
  (14.44.35211.0; sha256 `cc0ff0eb…` / `0c09f261…`, as in the vs22buildtools manifest).
- **ATL in our MSVC copy (G7, `TOOLCHAIN-ISSUES.md`)** — `base/win/atl_throw.cc` (every Windows build) needs `atldef.h`. With the
  VS 2022 Build Tools bootstrapper (catalog 17.14.41), install **`Microsoft.VisualStudio.Component.VC.14.44.17.14.ATL`** and
  **`Microsoft.VisualStudio.Component.VC.14.44.17.14.ATL.ARM`** into a scratch folder (`D:\vs-atl-scratch`, `--nocache`), copy only
  `VC\Tools\MSVC\14.44.35207\atlmfc\` into `D:\Program Files\vs22buildtools`, then uninstall the scratch instance. Result: MSVC
  14.44.35207 1,980 → 2,063 files; `atlmfc\` manifest (`tools\treehash.py --noskip`)
  **`d5835a3e68ac52e7e8bd48a61240039009cb92edbdeebac28b17a33292999a61`** (83 files); `atldef.h` `3a344f2b…`.
- **Python's `python3.exe` in a folder without spaces** (the build server: `C:\Python313\python3.exe`, a copy of
  `python.exe`; the laptop: the Store's `…\WindowsApps\python3.exe`) — GN resolves `script_executable = "python3"` from `PATH`,
  and step 17 normalizes that one path.

## Inputs
| file | size | sha256 | where from |
|---|---|---|---|
| `F:\dl\chromium-150.0.7871.222.tar.xz` | 5,829,998,600 | `5ea2862124f8602dfb444acb833fa6781eda3183a63e8e68071b8aed45ae6659` | Google's official source tarball, bucket `chromium-browser-official`: **`https://commondatastorage.googleapis.com/chromium-browser-official/chromium-150.0.7871.222.tar.xz`** (Google also publishes `…/chromium-150.0.7871.222.tar.xz.hashes`; the copy the laptop saved is `s3\chromium-150.0.7871.222.tar.xz.hashes`). **Not packed** — download it on the server. The laptop's copy is at `F:\dl\` if the download fails (see note). |
| `F:\dl\depot_tools.zip` | 30,954,870 | `bb75c674b71280ca9a6c1f5af5d0b89b2f8a0d5406c06b501792f1ff27dd0ac3` | packed (group `build-tools`) |
| `F:\dl\gn-windows-amd64.zip` | 1,153,192 | `77e77e2f0d7bea1992769343c68ab4312b8151c5a433f30301b365dd8e0f8687` | packed; CIPD gn `git_revision:3357c4f51b1a…` (s3\PROGRESS.md:126,134) |
| `F:\dl\ninja-windows-amd64.zip` | 318,551 | `0cf1cb1b9d2b8d2d14a9fd38b984f17dd853115db682880b21ceee45e91deb50` | packed; CIPD ninja `version:3@1.12.1.chromium.4` (s3\PROGRESS.md:126,135) |
| `s3\delta-222-226\src.diff` / `v8.diff` / `skia.diff` | 53,109 / 6,183 / 3,672 | `64ce9737…` / `3f64d226…` / `ab9e3019…` | repo bundle; fetched by `s3\step_fetch_delta.sh` from GitHub compare URLs (in that script and `fetch.log`) |
| `s3\supermium-touched.txt` (132 paths) | 5,935 | `449be3c192abec67f8063b2d27664abeb15e10a09c00d5af7a5f8bfa7fd16774` | repo bundle |
| `s3\patches-lf\*.patch` (Supermium's 7, LF) | | see **`s3\patches-lf\HASHES.txt`** ("blob(LF)"; `s3\patch-hashes.txt` holds the CRLF working copies of the Supermium clone — not these files, MOVE-2 Finding 2); combined result `s3\applied-supermium-150.diff` = `adada54f…` | repo bundle |
| `venetium\patches\0001…0008` | | `1019703a…` `f44bdd45…` `9405b0df…` `305e2e9a…` `1b4276d4…` `d395a275…` `1a419d54…` `1134d2b1…` | repo bundle (full hashes in MOVE-1-RESULTS.md §0) |
| `venetium\patches\0009-v1-warning.patch` | | `1a8a3a2261f484f55ece82937157ec6be46c79990c93682b56dc7612c8d77823` | repo bundle (V8SIM-1) |
| `venetium\patches\0010-win-clang-x86-v8-arm.patch` | | `c8eb09a296d863a6bf7a7edc5743c64b06602d4c1d048bb5eef256bf2e51587d` | repo bundle (V8SIM-1) |
| `s9\state-0009.sha256`, `s9\state-0010.sha256` | | 225 files + 1 `MISSING` line each | repo bundle (V8SIM-1, made by `s9\state.py`) |
| V8's test files (14,095) | | per file in `venetium\testdata\v8-17700469.tsv` (`385917d9…`); checker `s10\check_testdata.py` | packed (group **`v8-testdata`**, restore folder `F:\cr\src`); manifest and checker in the repo bundle |
| `venetium\env\build-env.sh` | | `8c1feade36e63f662644864f1e4509e66dafd394cfe5b1578cd90cb237d5e062` | repo bundle (V8SIM-2) |
| `venetium\args\out-x86-arm-sim.gn` | 2,739 | `1dae326b8664ce3224dcb328ed66815a10c484c76bb6e60ab31926129e0a3113` | repo bundle (V8SIM-2, PCH off) |
| `venetium\args\out-x86-rt21.gn` | 3,585 | `65a99204900a3659a807586250e8906a6b13b187d869c513e9c95079447e56ac` | repo bundle (MOVE-1b, commit `219f0383…`); copy in `F:\move\args\` |
| `s11\tools\fingerprint.sh` | 2,676 | `fe491085af486c137f1a88a2db38feecb4e168216b553ca15e9a929e71689833` | repo bundle (MOVE-1b); copy in `F:\move\tools\` |
| `venetium\patches\0011-v8-arm-invokeprologue-flags.patch` | 2,098 | `3131f43870c48c7ab179428c4a429af0e4cc625033241dd1bef34f2a4ee69c80` | repo (REDO-1 §4; F1 header + `git diff`) |
| `s9\state-0011.sha256` | | 226 files + 1 `MISSING` line | repo (REDO-1 §4) |
| `venetium\tests\f1probe.js`, `f1probe.py` | | | repo (REDO-1 §5) — the F1 probe |
| `venetium\deps\x86-inputs.tsv` | 72,685 | `c295cfbbc890339c7ed2df81830ba76cf660f7e6407dcc9b2054f6726e9afff1` | repo (X86-1 §1.3–§1.4, + `microsoft_webauthn` from §2) — the Windows-host build inputs: path · sha256 · source · pin, 295 files |
| `s9\state-0011-inputs.sha256` | 64,296 | `55ecaab0c6edd9b59da4850e44d50f44f9dc4875c6ed8a607281fa7fd975864d` | repo (X86-1) — state-0011 + those 287 files; 513 files + 2 `MISSING` lines |
| `venetium\patches\0012-x86-1-build-fixes.patch` | 20,700 | `697575c45691b84d49fb906de414b1ea09cb2752eb16194e524d1eba7e598b1b` | repo (X86-1, after §1.8 — his decision: fix here) |
| `s9\state-0012.sha256` | | 229 files + 1 `MISSING` line | repo (X86-1) |
| `s9\state-0012-inputs.sha256` | | 524 files + 2 `MISSING` lines | repo (X86-1) — state-0012 + the 295 inputs |
| `venetium\patches\0013-x86-1-toolchain-drift.patch` | 3,638 | `8cb97645a542193b004d7eb6f431236c62afad17412df0d7af5c9ad4a94fd15d` | repo (X86-1 §2 fix round — his decision) |
| `venetium\args\venetium.gn` | 1,039 | `d3e29c52f71137d8d58b2e4af2c7ceab9aeda2084baf7b1d7329105c405a265a` | repo — Venetium's GN args incl. D13 (codecs) and D15 (`use_dummy_lastchange = false`), merged into each output dir's `args.gn` (step 22) |
| `venetium\deps\release-inputs.tsv` | 865 | `73dc6db669881bcc7a5952543ecb302b48f1aa321fb02cec63797de5af52e5cb` | repo (X86-1b §2) — `build/util/LASTCHANGE` + `.committime` for .226 (step 23) |
| `s9\state-0013-release.sha256` | | 528 files + 2 `MISSING` lines | repo (X86-1b) — state-0013-inputs + the 2 release files |
| `venetium\patches\0014-process-bound-string-decrypt.patch` | 1,572 | `7602ea00646584433483a45e3555b65a0b1c0b090df3629a0f1c1ed5b66266b0` | repo (X86-1b — his decision) |
| `s9\state-0014.sha256` | | 232 files + 1 `MISSING` line | repo (X86-1b) |
| `s9\state-0014-release.sha256` | | 529 files + 2 `MISSING` lines | repo (X86-1b) — state-0014 + the 295 inputs + the 2 release files; **the whole record** |
| `venetium\patches\0015-ua-brand-venetium.patch` | 1,910 | `f2985979453e38ba4ff3be8314df8a6b23a9cc0a6fc21a043de364b30ef1252a` | repo (X86-2 — his decision) |
| `s9\state-0015.sha256` | | 233 files + 1 `MISSING` line | repo (X86-2) — the union grows by `user_agent_utils.cc` |
| `s9\state-0015-release.sha256` | | 530 files + 2 `MISSING` lines | repo (X86-2) — state-0014-release + `user_agent_utils.cc`; **the whole record** |
| `s9\state-0013.sha256` | | 231 files + 1 `MISSING` line | repo (X86-1) |
| `s9\state-0013-inputs.sha256` | | 526 files + 2 `MISSING` lines | repo (X86-1) — state-0013 + the 295 inputs; **the whole record** |

Note on the tarball: on 2026-09-27 the laptop could no longer reach the bucket ("service is not available in your
location", HTTP 403/401 — it downloaded fine on 2026-09-25). If the server cannot download it either, upload
`F:\dl\chromium-150.0.7871.222.tar.xz` from the laptop separately (5.8 GB) and check the sha256 above.
**Google's bucket does serve the build servers:** the first server downloaded it, and build server 2 got HTTP 200 with the
right size on 2026-10-01 (MOVE-2 used an uploaded copy; same sha256).

## Steps (Git Bash; `sha256sum` every input first)
1. **Extract** (s3\step_extract_mt.sh:20; laptop wall 2,702 s):
   `xz -dc -T8 /f/dl/chromium-150.0.7871.222.tar.xz | /c/Windows/System32/tar.exe -xf - -C 'F:/cr'`
   Expected: `xz rc=0  tar rc=1` with exactly one message `Archive entry has empty or unreadable filename ... skipping`
   (F:\cr\extract-150-mt.log). The skipped entry is
   `third_party/vulkan-loader/src/tests/framework/icd/export_definitions/🌋.def` (96 B, a Vulkan-loader test fixture whose
   emoji name bsdtar could not map; s3\archive-vs-disk.txt:64). It is **absent on the laptop** and therefore absent from
   `tree-manifest.tsv`. If the server's bsdtar does create it, delete it — the laptop tree does not have it. **Windows Server 2022's bsdtar 3.8.4
   does** (MOVE-2: `xz rc=0  tar rc=0`, no message, `🌋.def` 96 B created) — delete it there.
2. **Rename** `F:\cr\chromium-150.0.7871.222` → `F:\cr\src` (s3\step_post_extract.ps1:17).
3. **depot_tools** (never bootstrapped, not used by the build — gn and ninja run directly):
   `mkdir -p /f/depot_tools && /c/Windows/System32/tar.exe -xf F:/dl/depot_tools.zip -C F:/depot_tools`
   (s3\step_post_extract.ps1:35; 288 top-level entries). The package also carries `F:\depot_tools` itself with a manifest.
4. **gn and ninja** (s3\step_post_extract.ps1:38-41):
   `tar.exe -xf F:/dl/gn-windows-amd64.zip -C F:/cr/src/buildtools/win` → `gn.exe` `154197d2da4217a7…` (gn 2407 (3357c4f51b1a))
   `tar.exe -xf F:/dl/ninja-windows-amd64.zip -C F:/cr/src/third_party/ninja` → `ninja.exe` `48f6f1b2653e37a8…` (1.12.1)
   (F:\cr\post-extract-150.log:10-16.)
5. **.222 → .226 deltas**, from `F:\cr\src`, each `--check` first (s3\PROGRESS.md:344-349; outcome s3\delta-222-226\apply.log —
   run inline, no saved script; the exact lines are in the laptop's session transcript, 2026-09-26T03:06:56Z / 03:07:06Z):
   `git -c core.autocrlf=false apply --exclude=v8 --exclude=third_party/skia D:/repo/supermium-rt/s3/delta-222-226/src.diff` (25 files;
   the two excluded entries are gitlinks), `git -c core.autocrlf=false apply --directory=v8 …/v8.diff` (6),
   `git -c core.autocrlf=false apply --directory=third_party/skia …/skia.diff` (3) — `--check` the same way. **The `-c` matters**
   (see Machine prerequisites): without it `DEPS` hashes `f43a9535…`.
   Check: `chrome/VERSION` → PATCH=226; `v8/include/v8-version.h` → 15.0.245.28; `DEPS` sha256 `cddd21a62d852243…`
   (apply.log:15-18).
6. **Marker file:** create the empty `F:\cr\src\third_party\rust-toolchain\bin\rustc.exe` (0 B). Chromium's Windows Rust build
   lists it as an unconditional GN input even with a custom sysroot; the tarball ships only a Linux rustc
   (SUPERMIUM-3-RESULTS.md:250-252, s3\PROGRESS.md:551-552).
7. **Git baseline — index only, no commit** (s3\PROGRESS.md "§1.5 route"; the capture scripts in s4/s6/s7 rely on it):
   `cd /f/cr/src && git init -q && git config --local core.autocrlf false && git config --local core.symlinks true && git config --local core.longpaths true`
   `tr -d '\r' < D:/repo/supermium-rt/s3/supermium-touched.txt | xargs -d '\n' git add -f --`   (132 files)
   The default index's own sha256 will differ from the laptop's (`953bec9c…`, it holds file times) — the diff hashes are
   the identity.
8. **Supermium's 7 patches**, in this order, each `git apply --check -v` first (s3\step15_patches.sh):
   `supermium_150_core, ui_part_1, ui_part_2, general_features, 150_about_flags_and_finishing_touches, ptr_device_fix, xp_tts_fix`
   → `git apply D:/repo/supermium-rt/s3/patches-lf/<name>.patch`
9. **Check:** `git diff | sha256sum` = **`adada54f…`**.
10. **Venetium 0001 → 0008**, each `git apply --binary --check` then `git apply --binary` (as `s7\roundtrip.sh` does). After
    patch L the files of all patches must equal `s7\state-000L.sha256` (224 files; `sha256sum -c`, `MISSING` lines = absent).
    Final: `git diff | sha256sum` = **`407cf087…`**.
11. **Output directories from `extras`:** restore `out\x64-rt2\args.gn` (1,873 B, `3223e0d27f01e790…`) and the other three
    `args.gn` (`arm-rt2`, `x64-lean`, `x64-v4`) from the `extras` group; everything else under `out\` is regenerated by
    `gn gen`. (`out\x86-arm-sim` and `out\x86-rt21` come from `venetium\args\` in step 16. Leave `gn gen` until step 16.)
12. **Prove it:** run `tools\treehash.py F:/cr/src <new.tsv>` (the same script that made `tree-manifest.tsv`; skips top-level
    `out\` and `.git\` and every `__pycache__\`, records links as links) and compare with `tree-manifest.tsv` line by line. Any
    difference that is in `extras` (`extras.tsv` names every such file and why) is restored from `extras`; anything else is
    a stop (BATCH-MOVE-2 §2.3). **This compares state 0008** — do it before step 13.
13. **Venetium 0009 → 0010** (V8SIM-1), from `F:\cr\src`, exactly as step 10: `git apply --binary --check` first, then
    `git apply --binary`:
    `git apply --binary --check D:/repo/supermium-rt/venetium/patches/0009-v1-warning.patch && git apply --binary D:/repo/supermium-rt/venetium/patches/0009-v1-warning.patch`
    → check `s9\state-0009.sha256` the way step 14 checks state-0010; then the same two commands for
    `0010-win-clang-x86-v8-arm.patch`. 0009 touches `build/config/compiler/BUILD.gn` (`-Wno-deprecated-attributes` for
    non-default clangs), 0010 `build/toolchain/win/BUILD.gn` (the `clang_x86_v8_arm` toolchain). `git diff | sha256sum`
    **stays `407cf087…`** — the default index tracks only Supermium's 132 files, none under `build/`.
14. **State check (state-0010):**
    `cd /f/cr/src && grep -v '^MISSING' D:/repo/supermium-rt/s9/state-0010.sha256 | sha256sum -c --quiet - && echo 225 match`
    and the one `MISSING` line's file, `chrome/app/visual_elements_resources/chrome.VisualElementsManifest.xml`, must be
    **absent** (by design: 0002 renames it to `venetium.VisualElementsManifest.xml`). `bash D:/repo/supermium-rt/s9/roundtrip.sh 10` is the full proof (removes all
    ten patches newest first down to `adada54f…`, then re-applies them, checking the state after each).
15. **V8's test files** (V8SIM-2 §2; group `v8-testdata`): 14,095 files at V8 revision `17700469f857…` that the Chromium
    tarball lacks — 14,088 under `v8/test/` (mjsunit 9,216, message tests, the unittests' ARM sources, …) and 7 self-test
    fixtures under `v8/tools/testrunner/testdata/testroot*/out/build/` (the tarball drops every `out/` path; the test runner
    itself is already in the tree). **Never overwrite a tree file:**
    - Before: `python D:/repo/supermium-rt/s10/check_testdata.py` must end `0 OK, 14095 bad` (every file `MISSING`).
    - Extract: `"/c/Program Files/7-Zip/7z.exe" x -snl -aos -oF:/cr/src C:/move/parts/v8-testdata.7z.001`
      (`-aos` skips any file that already exists).
    - After: `python D:/repo/supermium-rt/s10/check_testdata.py` → `14095 OK, 0 bad | manifest sha256 385917d9…`.
      Two names are non-ASCII (`v8/test/message/unicode-filename-🎅🎄.js` and `.out`); 7-Zip stores Unicode names, and the
      check proves both arrived.
      (`s10\check_testdata.py` used to crash with `UnicodeEncodeError` when it printed one of those two names to a
      non-UTF-8 Windows console or redirect — i.e. in the "before" run, where every file is `MISSING`; fixed by REDO-1 §7: it now
      writes UTF-8 with backslash escapes.)
16. **Output directories for the server's builds.** Copy byte for byte (then `cmp`):
    `venetium\args\out-x86-arm-sim.gn` → `F:\cr\src\out\x86-arm-sim\args.gn` (`1dae326b…`: V8's ARM simulator — d8,
    v8_unittests, mjsunit; PCH off) and `venetium\args\out-x86-rt21.gn` → `F:\cr\src\out\x86-rt21\args.gn` (`65a99204…`:
    the x86 Venetium browser — rt2.1 + rustc-rt2.1, release, DCHECKs off, `symbol_level = 1`, PCH off, Venetium's four
    args). `out\x64-rt2\args.gn` came from `extras` (step 11). **Every `gn` and `ninja` run sources the environment file**:
    `source D:/repo/supermium-rt/venetium/env/build-env.sh && cd /f/cr/src && $GN gen out/<dir>` (it also puts the SDK's
    `mt.exe` on `PATH`, G6). Laptop results: `x64-rt2` 30,629 targets (plus one warning: its `enable_nacl = false` has no
    effect in 150 — harmless, left as the laptop has it), `x86-arm-sim` 31,839, `x86-rt21` 31,837 (no warning).
    Note: on Windows **`gn args <dir> --list` is not read-only** — loading the build runs `setup_toolchain.py`, which rewrites
    `<dir>\environment.x64` / `environment.x86` (`build/toolchain/win/BUILD.gn:12-13`), exactly as `gn gen` does. Harmless
    (GN output files), but query only a directory you are allowed to change.
17. **Build-graph fingerprints.** For each row: `bash C:/move/tools/fingerprint.sh <dir> <tag> <targets>` (runs
    `ninja -t commands`, which builds nothing; outputs in `F:\cr\move1b\`). The **normalized** sha256 must equal:

    | dir | targets | tag | normalized sha256 | lines | clang-cl / rustc / lld-link |
    |---|---|---|---|---|---|
    | `x64-rt2` | `chrome` | `x64-rt2-chrome` | `3c3cfae40e075c6ddb4bf3b513a900805c32720cf090564f197b53edc43039a2` | 57,219 | 46,743 / 191 / 2,175 |
    | `x86-arm-sim` | `d8 v8_unittests` | `x86-arm-sim-d8-unittests` | `1eec72f583aa626096c1d9fa8f396b6c7542d51d8f1b7471ccd6fffdea1add3d` | 5,183 | 4,671 / 91 / 154 |
    | `x86-rt21` | `chrome` | `x86-rt21-chrome` | `6a343be292da0d0484ad26f09f6fd825ee212075d624389c99ce87218c51583f` | 55,936 | 45,605 / 166 / 2,126 |

    Normalized = the Python interpreter GN resolved on that machine (`//.gn` `script_executable = "python3"`) replaced by
    `<PYTHON3>`, byte for byte (CRLF kept). On the laptop it is `C:/Users/hamed/AppData/Local/Microsoft/WindowsApps/python3.exe`,
    the **only machine-specific path** in the three lists (MOVE-1b §2: every other absolute path is under `D:\Windows Kits\10`,
    `D:\Program Files\vs22buildtools` or `C:\Program Files (x86)\Windows Kits\NETFXSDK`, plus Chromium's placeholder
    `/PDBSourcePath:o:\fake\prefix`). The raw sha256 matches too if the server's Python path is identical (laptop raw:
    `efbb1316…`, `16f28a3e…`, `d43cee71…`). MOVE-1's `fingerprint.txt` (`7c0bbecf…` / `3402e193…`) was state 0008; the
    `x64-rt2` row replaces it.

    **With ATL (G7) installed** every compile gains `-imsvc…\ATLMFC\include` and every link `-libpath:…\ATLMFC\lib\<cpu>`;
    the normalized fingerprints become **x86-arm-sim `bea57559d14162cfb605ae00b901e64636aee201c0b633df15a4d6ee27da4e81`** and
    **x86-rt21 `61a0de05e9428a3931c935ca4e1ae71c6b68133c68e1e7409096f7d4760502d6`** (REDO-1 §1; = the first server's); removing
    exactly those flags gives the values above byte for byte. **`out\x64-rt2` is retired** (REDO-1 §7): its fingerprint is not
    refreshed. Run `fingerprint.sh` with `FP_DIR=<scratch>` to keep its outputs out of `F:\cr\move1b\` on new machines.
18. **Venetium 0011 — F1** (BATCH-REDO-1 §4): `v8/src/codegen/arm/macro-assembler-arm.cc` (`7cd2367e…` → `b6f15964…`), six lines
    in `MacroAssembler::InvokePrologue` that set the flags before the copy loop on Windows. Like step 13:
    `git apply --binary --check D:/repo/supermium-rt/venetium/patches/0011-v8-arm-invokeprologue-flags.patch && git apply --binary D:/repo/supermium-rt/venetium/patches/0011-v8-arm-invokeprologue-flags.patch`
    (the patch begins with a prose header before `diff --git`; `git apply` skips it). Then
    `grep -v '^MISSING' D:/repo/supermium-rt/s9/state-0011.sha256 | sha256sum -c --quiet -` → 226 match, the `MISSING` file absent;
    `git diff | sha256sum` stays `407cf087…` (the default index does not track V8). Full proof: `s9\roundtrip.sh 11` run with the
    full-union states (REDO-1 kept the tracked `state-0000…0010` unchanged; `state.py 11` would add the 0011 file's baseline line
    to each). It changes no command line (fingerprints unchanged); it changes the generated ARM code: `snapshot_blob.bin` of
    `out\x86-arm-sim` becomes `806070fc…` (was `966a5ab3…`). Proof by running: `python venetium/tests/f1probe.py <d8.exe>`.
19. **Windows-host build inputs** (BATCH-X86-1 §1.3–§1.4, allowed change 1). DEPS pins these for a Windows checkout; the
    tarball, made on Linux, lacks them, and `chrome` cannot build without them (`node`, `esbuild`, `gperf`, `rc`, MIDL's
    preprocessor, `dxguids.cpp`, devtools' rollup). Fetch each at the pin of this tree's DEPS (Chromium 150.0.7871.226), check
    the hash DEPS gives, and put it where DEPS would, **never overwriting a file**. The manifest is `venetium\deps\x86-inputs.tsv`.
    - `third_party/node/win/node.exe`: `curl -fLo node.exe https://storage.googleapis.com/chromium-nodejs/2f710ced2db2beb7c3debf6097196c35ee5adb74`
      → sha256 `2ffe3acc…` (DEPS `src/third_party/node/win`), 89,935,872 B.
    - `build/toolchain/win/rc/win/rc.exe`: `https://storage.googleapis.com/chromium-browser-clang/rc/dea7da0d9bfcb8dad6cf53ca608dddac3459faaa`
      → sha1 = `rc.exe.sha1` (hook `rc_win`), 603,648 B.
    - `third_party/devtools-frontend/src/third_party/esbuild/esbuild.exe`: CIPD zip
      `https://chrome-infra-packages.appspot.com/dl/infra/3pp/tools/esbuild/windows-amd64/+/version:3@0.25.1.chromium.2`
      (sha256 of the zip = instance `279bd07f…`, 8,845,122 B); unzip `esbuild.exe` (devtools-frontend's DEPS, recursed).
    - `third_party/gperf/` and `third_party/microsoft_dxheaders/src/`: in an empty folder outside the tree,
      `git init -q && git -c core.autocrlf=false fetch --depth 1 <url> <commit> && git -c core.autocrlf=false checkout FETCH_HEAD`,
      check `FETCH_HEAD` = the commit and `git fsck` clean, copy the files without `.git`:
      `https://chromium.googlesource.com/chromium/deps/gperf.git` `e9eeea862a18e77b945d98eff7e1bf065d3daf8e` (186 files) and
      `https://github.com/microsoft/DirectX-Headers.git` `62c23d5ec700659453c6fe89d296554b2a5e7edc` (76 files; DEPS' mirror of it).
    - `third_party/llvm-build/Release+Asserts/`: `https://storage.googleapis.com/chromium-browser-clang/Win/clang-llvmorg-23-init-10931-g20b6ec66-11.tar.xz`
      (sha256 `855e4a23…`, 50,194,108 B). Only MIDL's preprocessor uses its `bin/clang-cl.exe` (`build/toolchain/win/midl.gni:174`
      hard-codes it); everything else compiles with rt2.1. 19 files are new; 310 are already there byte-identical (the Linux
      package's headers); `cr_build_revision` differs (the tarball's is 1 B, empty) and is **kept** — only `config("clang_revision")`
      reads it, and only with the default `clang_base_path`, which ours is not.
    - `third_party/microsoft_webauthn/src/` (found by X86-1 §2, not §1: it is a header-only checkout, and `#include`d headers are
      not ninja inputs until a first compile): in an empty folder outside the tree, as for gperf,
      `git init -q && git -c core.autocrlf=false fetch --depth 1 https://chromium.googlesource.com/external/github.com/microsoft/webauthn.git 273689d1d54232f0c316b31f596e7928acb1cd5a && git -c core.autocrlf=false checkout FETCH_HEAD`
      (DEPS `src/third_party/microsoft_webauthn/src`, `checkout_win`); 8 files, copied without `.git`, bytes as the git blobs
      (`webauthn.h` has CRLF upstream). Of DEPS' 33 `checkout_win` entries, 27 are missing from the tarball, but only this one is
      reached by `chrome`'s sources (`F:\cr\x86-1\scan_win_deps.py`).
    - devtools' rollup native binary: CIPD `https://chrome-infra-packages.appspot.com/dl/infra/3pp/tools/rollup_libs/windows-amd64/+/version:3@4.60.4`
      (instance `d13ee0b2…`) → `third_party/devtools-frontend/src/third_party/rollup_libs/rollup.win32-x64-msvc.node` (the tarball
      keeps that folder empty); then devtools' own hook, as gclient would run it:
      `cd third_party/devtools-frontend/src && python scripts/deps/sync_rollup_libs.py` → `node_modules/@rollup/rollup-win32-x64-msvc/`
      (`rollup-win32-x64-msvc.node`, `package.json`).
    - **Deviation, his decision (X86-1 §1.4): move the tarball's Linux ELF** `third_party/devtools-frontend/src/third_party/esbuild/esbuild`
      (15,670,606 B, `94c0d740…`) out of the tree (`F:\cr\x86-1\tree-moved\`). devtools' `esbuild_path()` has no extension, so on
      Windows that Linux file shadows `esbuild.exe` and every `ts_library` step fails with WinError 193. A Windows DEPS checkout
      never has it. The state file records it as a `MISSING` line.

    The scripts that did this on build server 2: `F:\cr\x86-1\place_inputs.py` (first six) and `devtools_fixes.py` (rollup, the
    move), with their logs. Check: `cd /f/cr/src && grep -v '^MISSING' D:/repo/supermium-rt/s9/state-0011-inputs.sha256 | sha256sum -c --quiet -`
    → 513 match, both `MISSING` files absent; `ninja -C out/x86-rt21 -n chrome` → no "missing and no known rule". None of it
    changes a command line (the x86-rt21 fingerprint stays `61a0de05…`).
20. **Venetium 0012 — X86-1 build fixes** (BATCH-X86-1, after §1.8; his decision "fix here"). Three patched sources did not
    compile in the first x86 build: (A) `components/url_formatter/url_fixer.cc` — Supermium's `general_features.patch` uses
    `base::CommandLine` without its header → `#include "base/command_line.h"`; (B) `components/translate/core/browser/translate_ranker_impl.cc`
    — 0007's bare early `return` leaves dead code (`-Werror,-Wunreachable-code`) → return only when the ranker is neither
    queried nor enforced (always, after 0007); (C) the two headers Supermium's about_flags patch includes but no Supermium patch
    creates, `chrome/browser/supermium_flag_choices.h` / `supermium_flag_entries.h` → new, from Supermium's branch `v144`
    (`82756ad4`, blobs `1452f912…` / `9a183cc7…`; main has neither) adapted to the 150 patches (65 → 51 entries; the patch
    header lists every change). Like step 18:
    `git apply --binary --check D:/repo/supermium-rt/venetium/patches/0012-x86-1-build-fixes.patch && git apply --binary D:/repo/supermium-rt/venetium/patches/0012-x86-1-build-fixes.patch`
    (prose header before `diff --git`; it creates the two headers). Then
    `grep -v '^MISSING' D:/repo/supermium-rt/s9/state-0012.sha256 | sha256sum -c --quiet -` → 229 match, the `MISSING` file absent;
    with step 19's inputs, `s9\state-0012-inputs.sha256` (524 + 2 absent; 516 + 2 before `microsoft_webauthn`) is the whole record
    at this step. **`git diff | sha256sum` becomes
    `0c9016cadc7226736abfd819245a34eb1297dd6226f49dffe8274119cca814e2`** (the default index tracks `url_fixer.cc`, a Supermium file;
    with 0001–0011 it was `407cf087…`). No command line changes. Proof (X86-1): the three objects compile; `state.py`-style chain
    0 mismatches; round trip of 0001–0012 PASS (bottom `adada54f…`). Made with `s9\prep_index.sh 0012` → edits → `git add -N` of
    the two new headers → `s9\capture.sh 0012` (COMPLETENESS ARM: PASS).
21. **Venetium 0013 — X86-1 toolchain drift** (BATCH-X86-1 §2 fix round; his decision "patch the source for both"). Two
    diagnostics that only rt2.1's newer compilers raise (Chromium 150 pins older ones), fixed at the narrowest source point:
    `third_party/webrtc/modules/audio_processing/agc2/rnn_vad/rnn_fc.cc` returns `&::rnnoise::TansigApproximated` /
    `&::rnnoise::SigmoidApproximated` (clang 23.1's `-Wpointer-bool-conversion` on a function reference in WebRTC's `FunctionView`,
    llvm PR #204944; the upstream header fix, WebRTC CL 488020, is left for the next roll because it would recompile 367 objects), and
    `third_party/rust/chromium_crates_io/vendor/qr_code-v2/src/structured.rs` uses `u8::MAX` (rustc 1.99+ deprecates `max_value()`,
    rust-lang/rust#146882, and the crate's own `#![deny(warnings)]` makes that an error). Like step 18:
    `git apply --binary --check D:/repo/supermium-rt/venetium/patches/0013-x86-1-toolchain-drift.patch && git apply --binary D:/repo/supermium-rt/venetium/patches/0013-x86-1-toolchain-drift.patch`;
    `grep -v '^MISSING' D:/repo/supermium-rt/s9/state-0013.sha256 | sha256sum -c --quiet -` → 231 match; with the inputs,
    `s9\state-0013-inputs.sha256` (526 + 2 absent). The default index does not track either file: `git diff` stays `0c9016ca…`.
    Proof (X86-1): both objects compile with rt2.1; capture COMPLETENESS PASS; round trip 0001–0013 PASS.
22. **Release args: codecs on (D13) and the real revision (D15)** (BATCH-X86-1b §1–§2). `venetium\args\venetium.gn` now also sets
    `proprietary_codecs = true`, `ffmpeg_branding = "Chrome"` (D13, "video and audio should be on") and `use_dummy_lastchange = false`
    (D15: stay non-official, carry Chromium's revision and year). **Merging** `venetium.gn` into an output dir's `args.gn` = replace
    everything after the line `# ---- venetium\args\venetium.gn (appended, as the build batches merge it) ----` with the current
    `venetium.gn`, verbatim (`F:\cr\x86-1b\apply_d13.py` / `apply_d15.py`); both `out\x86-rt21` and `out\x86-arm-sim` take it, then
    `gn gen`. `venetium\args\out-x86-rt21.gn` (the stored copy) still ends with the older block — the merge rule, not that copy, is
    the source. Derived: `enable_platform_hevc`, `enable_hevc_parser_and_hw_decoder`, `enable_mse_mpeg2ts_stream_parser`,
    `media_use_ffmpeg`, `media_use_openh264`, `rtc_use_h264` true; AC3/E-AC3, DTS, Widevine false. The x86-arm-sim `d8 v8_unittests`
    commands do not change. **`out\x86-rt21`'s normalized `chrome` fingerprint changes** (step 17's `61a0de05…` no longer applies):
    `6b9bf42f…` with D13 alone, **`35f16d40c629f12cbd60152fdd665dd0d0e4287c553fd77389a3f848cc4a4863`** with D13 + D15 (0014 and 0015 add none; X86-2 re-ran it after 0015: `35f16d40…`). **Owed for the ARM bring-up:** Chromium 150 ships no ffmpeg `Chrome\win\arm` configuration (only `ia32`,
    `x64`, `arm64` for `win`), and `build_ffmpeg.py` has no 32-bit Windows ARM target. It will take: an `arm` branch in
    `build_ffmpeg.py`, configuring Chromium's pinned ffmpeg (`ad41607c…`) for win/arm with ffmpeg-rt's settings (NEON, Thumb) as the
    reference, and `generate_gn.py` for the source lists. CR109's `win\arm` configs are an older FFmpeg (see `s3\FFMPEG-RECONCILE.md`).
23. **D15's identity inputs** (X86-1b §2): the .222 tarball's `build/util/LASTCHANGE` names the .222 tag commit (`93560ff1…@{#4255}`);
    the .222 → .226 delta does not touch it. **Replace** both files (move the old ones aside, they are not deleted) with .226's, in
    `build/util/lastchange.py`'s format: `LASTCHANGE=a432ac4385eeea1fffb88bb8785f778918a1a322-refs/branch-heads/7871@{#4266}` and
    `LASTCHANGE_YEAR=2026` (LF line ends), `LASTCHANGE.committime` = `1786028519` (2026-08-06T15:01:59Z, no newline) — read from
    `chromium.googlesource.com/chromium/src/+/refs/tags/150.0.7871.226` and GitHub `chromium/chromium` (same commit). Manifest
    `venetium\deps\release-inputs.tsv`; state `s9\state-0013-release.sha256`. The build-date stamp (`build/timestamp.gni`) follows
    the committime.
24. **Venetium 0014 — ProcessBoundString decrypt** (X86-1b; his decision "fix it, no matter it is broken or not"). Supermium's 150
    core patch makes `crypto/process_bound_string.cc` `MaybeDecryptBuffer()` call `::CryptProtectMemory` (encrypt): **cookies did not
    work at all** in the build before it (X86-1b cookie check: 0/3, then 3/3 with 0014). Supermium's 09-30 fix misspells the call, so
    0014 carries the correct `::CryptUnprotectMemory`. Like step 18:
    `git apply --binary --check D:/repo/supermium-rt/venetium/patches/0014-process-bound-string-decrypt.patch && git apply --binary D:/repo/supermium-rt/venetium/patches/0014-process-bound-string-decrypt.patch`;
    `s9\state-0014.sha256` → 232 match + 1 absent; with steps 19 and 23, `s9\state-0014-release.sha256` (529 + 2 absent) is the whole
    record. **`git diff | sha256sum` becomes `4f71b3c8c7536330133e68aa251752a6ca7bfb579fe5fbc103a178aab186a538`** (the default index
    tracks this Supermium-touched file). Round trip of 0001–0014 PASS.
25. **Venetium 0015 — client-hint brand "Venetium"** (X86-2; his decision "patch now: brand Venetium"). Supermium's
    `components/embedder_support/user_agent_utils.cc` `GetUserAgentBrandList()` gives a Chromium-branded build the brand
    "Google Chrome" (unless a `uao` file in the user data directory names one). Real Chrome sends that brand with its
    `X-Browser-Validation` headers, which only Chrome-branded builds have, so **Google sign-in refused the build before it**
    ("Couldn't sign you in — This browser or app may not be secure"; his test with a `uao` brand "Venetium" signed in, and so
    did the build with 0015). 0015 makes the default `version_info::GetProductName()` ("Venetium"); a `uao` brand still wins.
    Like step 18:
    `git apply --binary --check D:/repo/supermium-rt/venetium/patches/0015-ua-brand-venetium.patch && git apply --binary D:/repo/supermium-rt/venetium/patches/0015-ua-brand-venetium.patch`;
    `s9\state-0015.sha256` → 233 match + 1 absent; `s9\state-0015-release.sha256` (530 + 2 absent) is the whole record.
    **`git diff | sha256sum` becomes `abb3dace5a832b08534dc9e2e64985ecc15c111a41d1c26440e99a1bffde612f`** (the default index
    tracks this Supermium-touched file). Round trip of 0001–0015 PASS. The build-graph fingerprint does not change.

26. **ARM32 compiler-rt builtins (G1)** (BATCH-ARM-1 §2 step 8; `TOOLCHAIN-ISSUES.md` G1). Chromium links
    `clang_rt.builtins-arm.lib`, which rt2.1 does not ship. Built from rt2.1's own compiler-rt source (his `C:\rt21-src.7z`,
    extracted to `F:\cr\rt21-src`; llvmorg-23.1.1): `python s9/crt-arm/edit_cmakelists.py` (applies
    `s9\crt-arm\cmakelists-win-arm.diff` — a Windows-ARM32 `arm_SOURCES` branch without the `__aeabi_*`/`__chkstk` sources),
    `bash s9/crt-arm/build.sh` (rt2.1 `clang-cl --target=armv7-pc-windows-msvc`, MSVC 14.44.35207 + SDK 26100 headers,
    `COMPILER_RT_HAS_MIMPLICIT_IT=ON` pre-set), `bash s9/crt-arm/test/test.sh`, then `python s9/crt-arm/install.py` → 
    `F:\cr\rt21-cr\lib\clang\23\lib\windows\clang_rt.builtins-arm.lib` **167,018 B `6ca7e3e08ebc2336…`** (rt21-cr MANIFEST row 364;
    `rt21-cr.before/after.sha256` differ by exactly the .lib and MANIFEST.txt).
27. **Venetium 0016 → 0027** (BATCH-ARM-1 §2–§5), from `F:\cr\src` at state-0015-release, each exactly as step 18:
    `git apply --binary --check <patch> && git apply --binary <patch>`, then check `s9\state-00NN.sha256` (and `-release`):
    | patch | state (files + absent) | release | `git diff \| sha256sum` after it |
    |---|---|---|---|
    | 0016 `arm32-build-toolchain` | 243 + 1 | 540 + 2 | `abb3dace…` |
    | 0017 `arm32-platform-blockers` | 261 + 1 | 558 + 2 | `abb3dace…` |
    | 0018 `v8-windows-arm32` | 271 + 1 | 568 + 2 | `abb3dace…` |
    | 0019 `sandbox-arm32-interception` | 284 + 1 | 581 + 2 | **`42ee724a0abc9c39…`** (edits Supermium-tracked files) |
    | 0020 `crashpad-arm32` | 300 + 1 | 597 + 2 | `42ee724a…` |
    | 0021 `gpu-d3d9-arm32` | 303 + 1 | 600 + 2 | `42ee724a…` |
    | 0022 `arm32-link-misc` | 305 + 1 | 602 + 2 | `42ee724a…` |
    | 0023 `ffmpeg-win-arm-wiring` | 307 + 1 | 604 + 2 | `42ee724a…` |
    | 0024 `libcxx-no-exceptions-arm32` | 308 + 1 | 605 + 2 | `42ee724a…` |
    | 0025 `arm32-build-fix-round` | 322 + 1 | 619 + 2 | `42ee724a…` |
    | 0026 `arm32-link-fixes` | 330 + 1 | 627 + 2 | **`9ca5d60fe33d928b…`** (`process_metrics_win.cc` is Supermium-tracked) |
    | 0027 `chrome-dll-link-sdk19041` | 333 + 1 | 630 + 2 | `9ca5d60f…` |
    0024 is the Chromium-side workaround for `TOOLCHAIN-ISSUES` B4 (rt2.1 cannot emit Windows ARM32 C++ EH); drop it with the args
    line `dawn_use_built_dxc = false` when rt2 implements it. **0025 carries CRLF lines** for
    `third_party/cpuinfo/src/src/arm/windows/init.c` (CRLF upstream; `.gitattributes` says `*.c text eol=lf`): `git apply` keeps them
    with no special flag — but a RE-capture of 0025 must run `capture.sh` with
    `GIT_ATTR_SOURCE=4b825dc642cb6eb9a060e54bf8d69288fbee4904` (the empty tree), or git normalises the file to LF and the round trip
    fails. Round trip of 0001–0027 PASS (back to `adada54f…`, forward to `9ca5d60f…`).
28. **ARM32 generated inputs (69)** (BATCH-ARM-1 §3G; not patches, like step 19's x86 inputs): MIDL outputs for arm (45;
    `midl.exe` 26100 `/env arm32`, verified by `midl.py`'s own compare), codec configs (14: libvpx/libaom `win/arm-neon`, dav1d
    `win/arm`) and ffmpeg's `Chrome/win/arm-neon` config (10; `venetium\deps\ffmpeg-win-arm.sh` under MSYS2 `D:\MSYS2`, his order
    "Install msys2"). The files travel in `C:\handoff\ARM-1-record.zip` (`arm-1/gen-inputs/`); place them with
    `python F:\cr\arm-1\place_inputs.py` (in the zip): every file must match `venetium\deps\arm-inputs.tsv` (69 rows; MIDL also in
    `arm-midl.tsv`), its tree path must not exist yet, copied byte for byte. Check **`s9\state-0027-inputs.sha256`** (699 + 2 absent =
    state-0027-release + the 69; `4e1d261a…`). (During ARM-1 they were placed after 0023: `s9\state-0023-inputs.sha256`, 673 + 2.)
29. **`out\arm-rt21`**: copy `venetium\args\out-arm-rt21.gn` (3,918 B `3dafbb27…`; the x86 live args with `target_cpu = "arm"`,
    `enable_swiftshader = false`, `angle_enable_wgpu = false`, `dawn_use_swiftshader = false`, `dawn_use_built_dxc = false`, then
    `venetium.gn` appended) to `F:\cr\src\out\arm-rt21\args.gn` (`cmp`), source `venetium\env\build-env.sh`, `gn gen out/arm-rt21`
    → **38,183 targets**.
30. **Build**: `ninja -C out/arm-rt21 -j 8 chrome` (V8 builds its x86 snapshot toolchain `clang_x86_v8_arm` first; the full build was
    16 h on server 2). On the server under Claude Code, background shells are stopped at ~30 min, so it ran as
    `bash F:/cr/arm-1/run_s5c.sh <n> 1500` chunks (in the zip). Result: `venetium.exe` 2,404,352 B and `chrome.dll` ~217 MB,
    **ARMNT**; check with `python F:\cr\arm-1\s6\pe_facts.py F:/cr/src/out/arm-rt21 <report> venetium.exe chrome.dll chrome_elf.dll`.
    Running the browser is device-only (his Surface 2, Windows 10 build 15035).

31. **Portable packages (D16)** (BATCH-DEVICE-1 §2). Run-from-folder zips, files selected from Chromium's own ship list — never by
    guessing. `python venetium\package\portable.py set <out-dir> <x86|arm32> <report.tsv>` evaluates
    `chrome/installer/mini_installer/chrome.release` the way `chrome/tools/build/win/create_installer_archive.py` does for that build's
    GN args (GENERAL always; GOOGLE_CHROME only if branded; HIDPI if `enable_hidpi`; SNAPSHOTBLOB if `!use_v8_context_snapshot` or
    `include_both_v8_snapshots`; DXC if `dawn_use_built_dxc`; FFMPEG if `is_component_ffmpeg`), cross-checks FILES.cfg's `default`
    group, resolves every entry against the build folder (an entry with no file is reported, never skipped), keeps the build folder's
    FLAT layout (no `<version>\` folder; `chrome.exe` ships as `venetium.exe`). Both builds: sections GENERAL + HIDPI → x86 256 files,
    arm32 254 (the difference is exactly `vk_swiftshader.dll` + `vk_swiftshader_icd.json`, x86 only). Reported, not shipped: absent
    `chrome_child.dll`, `optimization_guide_internal.dll`, `Extensions\*.*`; built but off-section `elevation_service.exe`,
    `elevated_tracing_service.exe` (GOOGLE_CHROME), `snapshot_blob.bin` (SNAPSHOTBLOB).
    - x86: `portable.py zip F:/cr/src/out/x86-rt21 x86 venetium-150.0.7871.226-win-x86.zip` → 167,522,493 B **`f02dc37a…`**. RUNTIME
      control: unzip to a folder with spaces, `node venetium\package\launch_check.js <unzipped venetium.exe> <out> <profile>` →
      `chrome://version` "Venetium 150.0.7871.226 … (32-bit)", a web page and a local page load.
    - arm32: `portable.py zip F:/cr/src/out/arm-rt21 arm32 venetium-150.0.7871.226-win-arm32.zip --kit venetium\tests\kit
      --v8-config F:/cr/src/out/arm-rt21/clang_x86_v8_arm/v8_build_config.json` → 432 entries (254 build + 178 kit), 154,754,949 B
      **`145e7d94…`**. The kit goes in under `kit\`; every kit file must match `kit\MANIFEST.tsv`; `kit\lib\build.js` is written from
      the build's own V8 config (arm, `has_maglev true`, the `venetium.exe` sha256). Exit 3 if any SwiftShader file would be packaged for
      arm32. STRUCTURAL check: `python venetium\package\verify_zip.py <arm32.zip> <x86.zip> <set-arm32.tsv> <extract-dir>` (set
      present with matching sha256, every PE ARMNT, no SwiftShader, list = x86's minus SwiftShader, kit = MANIFEST, build.js = this build).
    - `python venetium\package\package_manifest.py <x86.zip> <arm32.zip>` → `PACKAGE-MANIFEST.tsv` (zip · path · sha256 · arch · size,
      688 rows; a copy is `venetium\package\PACKAGE-MANIFEST.tsv`), the `.sha256` files, and the copies in `C:\handoff\`.
    The zip's own sha256 also depends on the file times stored in it; the per-file hashes in PACKAGE-MANIFEST.tsv are the content proof.
    The kit tools that proved the kit (matrix, launchers from a path with spaces, manifest merge) are in `venetium\tests\kit-tools\matrix\`;
    what the kit checks and the x86 control verdict are in `venetium\tests\KIT-FINAL.md`.
32. **ARM32 `__rt_*` division shim** (DEVICE-1b; `TOOLCHAIN-ISSUES.md` **B5**; his decision 2026-10-04 "shim in our builtins lib").
    The device run crashed at startup: lld's ARM thunk margin (1.6 MB in the `chrome.dll` link) exceeds the ±1 MB `B<cond>.W` range,
    so the in-range early-out branch inside MSVC's `__rt_sdiv` (`libcmt.lib(divide.obj)`) goes through an r12-clobbering thunk and
    the remainder comes back negated. Fix: our own `__rt_udiv/__rt_sdiv/__rt_udiv64/__rt_sdiv64` in the G1 lib, so `divide.obj` is
    never linked. On top of step 26, all in `s9\crt-arm\rtdiv\`:
    - `python s9/crt-arm/rtdiv/edit_cmakelists_rtdiv.py` — checks `CMakeLists.txt` is G1's state, copies `rt_div_windows.S` to
      `compiler-rt\lib\builtins\arm\`, adds it to G1's Windows-ARM32 branch (`cmakelists-rtdiv.diff`; CMakeLists.txt → `41ed6836…`).
    - `bash s9/crt-arm/rtdiv/build.sh` (G1's flags, build dir `rtdiv\build`), then `python s9/crt-arm/rtdiv/compare_libs.py <G1 lib>
      <new lib> <standalone obj from asm_standalone.sh>` (157 G1 members unchanged + `rt_div_windows.S.obj`; no BRANCH20T added).
    - `python s9/crt-arm/rtdiv/install_rtdiv.py` → `clang_rt.builtins-arm.lib` **169,334 B `801fee5a3448a55f…`** (MANIFEST row +
      note; rt21-cr differs by exactly the lib and MANIFEST.txt). G1's lib stays in `s9\crt-arm\build\lib\windows\` (rollback).
    - **The lib is a ninja input** of every ARM `alink` edge (GN adds `libs` file paths as implicit deps), but `alink`'s rsp holds
      only `${in_newline}` (the objects), so no static lib contains it. Installing it with a new mtime makes ninja want 1,701
      re-archives plus a Rust/codegen cascade (8,303 dry-run steps) for byte-identical archives. So: `touch -r
      s9/crt-arm/build/lib/windows/clang_rt.builtins-arm.lib <installed lib>` (G1's time, 2026-10-02 14:15:07), then
      `python s9/crt-arm/rtdiv/relink_plan.py F:/cr/src/out/arm-rt21 relink-plan.tsv` (every link edge with the lib; 15 outputs
      present) and `python s9/crt-arm/rtdiv/relink_move.py F:/cr/src/out/arm-rt21 relink-plan.tsv` (moves them to
      `F:\cr\device-1\rtdiv\pre-relink\`, nothing deleted). `ninja -n` must then show exactly the 15 LINKs + `reorder_imports`.
    - `source venetium/env/build-env.sh` (G6: `mt.exe` on PATH — without it `chrome_elf.dll` fails "unable to find mt.exe"), then
      `$NINJA -C out/arm-rt21 -j 8 chrome <the 15 targets>` → `venetium.exe` 2,405,376 B **`ad8a696d…`**, `chrome.dll` 217,289,216 B
      **`2184001c…`**; `ninja -n` → no work to do.
    - Check every ARM build from now on: `python s9/crt-arm/rtdiv/find_rtdiv_code.py <divide.obj> <rt_div_windows.obj> <PEs…>` —
      every linked PE must show `msvc:*=0` and the shim (the 64-bit pair share their first 30 bytes, so they count 2 each); this
      catches a link order that would let `divide.obj` back in. `pe_facts.py` as step 30.
    - Package as step 31 under a new name: `portable.py zip F:/cr/src/out/arm-rt21 arm32 venetium-150.0.7871.226-win-arm32-rtdiv.zip
      --kit venetium\tests\kit --v8-config …` → 432 entries, 154,764,067 B **`a4ab5799…`**; `verify_zip.py` PASS; `package_manifest.py
      <x86.zip> <arm32-rtdiv.zip> --out … --label …` (688 rows, now `venetium\package\PACKAGE-MANIFEST.tsv`; the x86 zip is DEVICE-1's,
      unchanged). Against DEVICE-1's ARM zip exactly 11 entries differ: the 10 relinked shipped PEs and `kit\lib\build.js`.
33. **Venetium 0028 — no chrome_elf third-party-DLL hook on ARM32** (BATCH-ARM-FIX-1; his decision Q1; `TOOLCHAIN-ISSUES.md` N10). The
    device crashed ~2 s after start in chrome_elf's `NtMapViewOfSection` hook on Windows 10 loader-worker threads, which have no
    implicit TLS on 15035 ARM32 (MSVC emits the same TLS code — a platform fact). From `F:\cr\src` at state-0027-release + inputs:
    `git apply --binary --check venetium\patches\0028-chrome-elf-no-dll-hook-arm32.patch && git apply --binary …` (1 file,
    `chrome/chrome_elf/chrome_elf_main.cc`: `LeaveSetupBeacon()`/`Init()` under `#if !defined(_M_ARM)`), check `s9\state-0028.sha256`
    and `-release`; `git diff` anchor unchanged at `9ca5d60f…` (the file is not in the default index). Relink (`source
    venetium/env/build-env.sh`, `ninja -C out/arm-rt21 -j 8 chrome` → 5 steps): only `chrome_elf.dll` changes, 927,232 B
    **`12649432…`**; `chrome.dll` `2184001c…` and `venetium.exe` `ad8a696d…` relink byte-identical. Identity: ARMNT, ASLR/DEP/CFG,
    27 exports, version strings unchanged; imports 184 → 178 (the hook's 6 kernel32 functions, none added; all present on 15035).
    Package as step 31 under the standard name: `venetium-150.0.7871.226-win-arm32.zip` 432 entries, 154,750,260 B **`5650c910…`**,
    `verify_zip.py` PASS; vs the step-32 `-rtdiv` zip only `chrome_elf.dll` differs. `PACKAGE-MANIFEST.tsv` now lists this pair; the
    two earlier ARM zips are in `C:\handoff\superseded\`. Bringing the hook back on ARM32 would need a TLS-free hook path (not done).
34. **Venetium 0029 + 0030 — ARM32 sandboxed launches without the refused mitigation attribute** (BATCH-ARM-FIX-2; his
    decisions Q1 "both in one trip" and, after review `wf_8ca010d1-e4a`, "Add 0030 now"). The DEVICE-3 trip measured, with the
    sandbox probe, that Windows 10 15035 ARM32 refuses `PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY` with error 87 for every value in
    the 8- and 16-byte forms (every sandboxed child launch failed → "GPU process isn't usable"). From `F:\cr\src` at
    state-0028-release + inputs, each with `git apply --binary --check` first:
    - `venetium\patches\0029-sandbox-no-mitigation-attr-arm32.patch` (`sandbox/win/src/startup_information_helper.cc`:
      `SetMitigations()` clears the mitigation word and its size under `#if defined(_M_ARM)`) → `s9\state-0029` (335 + 1),
      `-release` (632 + 2), `-inputs` (701 + 2), anchor unchanged `9ca5d60f…`;
    - `venetium\patches\0030-sandbox-no-win32k-claim-arm32.patch` (`sandbox/policy/win/sandbox_win.cc`:
      `AddWin32kLockdownPolicy()` returns at once on `_M_ARM`, so no win32k flag and no fake-GDI interceptions) →
      `s9\state-0030` (336 + 1), `-release` (633 + 2), `-inputs` (702 + 2), anchor **`a7aae630…`** (the file is in the default
      index).
    Relink (`ninja -C out/arm-rt21 -j 8 chrome`, 5 steps after each patch): `chrome.dll` **`21e41be3…`** (217,289,728 B),
    `venetium.exe` **`fc85e06e…`**, `chrome_elf.dll` `12649432…` unchanged. ARMNT, ASLR/DEP/CFG, sections, exports, version
    strings and import sets are unchanged, and the `__rt_*` shim is present. What ARM32 loses is listed in the patch headers and
    `ARM-FIX-2-RESULTS.md`. The kit gains:
    - `kit\diag\`: sandbox probe v3.1 `sbxprobe.exe`, built from `venetium\tests\kit-tools\sbxprobe\` with its `build.sh`, plus
      `run-probe.cmd`, `log-run.cmd` and `README.txt`;
    - README section A2: Windows local crash dumps on, and off again at the end exactly as found (his step on the device).
    Package as step 31: `venetium-150.0.7871.226-win-arm32.zip`, 436 entries (254 + 182 kit), 154,934,845 B **`e6b5d260…`**,
    `verify_zip.py` PASS. Against step 33's zip: + 4 `kit/diag` files; 5 differ (`chrome.dll`, `venetium.exe`, `kit/README.md`,
    `kit/MANIFEST.tsv`, `kit/lib/build.js`). `PACKAGE-MANIFEST.tsv` lists this pair. In `C:\handoff\superseded\`:
    - step 33's zip, as `…-arm32-0028.zip`;
    - an intermediate 0029-only package, as `…-arm32-0029only.zip` (never announced).

35. **Venetium 0031 - link ARM32 without Control Flow Guard** (BATCH-ARM-FIX-2; his decision "ship CFG-off now, lld fix later"
    after DEVICE-5; `TOOLCHAIN-ISSUES.md` B6, handoff `LLD-ARM-CFG-1-HANDOFF.md`). DEVICE-5 (dbgrun) showed the post-0030 build
    fast-fails ~1.4 s in with 0xC0000409 subcode 10 (CFG indirect-call check): rt2.1 lld-link's ARM32 `/guard:cf` leaves the
    Guard Address-Taken IAT Entry Table empty (count 0 vs 11 on x86), so the first delay-load thunk call (GetIfTable2) is
    rejected. From `F:\cr\src` at state-0030-release + inputs: `git apply --binary --check
    venetium\patches\0031-arm32-no-cfg-link.patch && git apply --binary ...` (1 file, `build/config/win/BUILD.gn`:
    `config("cfi_linker")` guards the `/guard:cf` LINK flag with `current_cpu != "arm"`; the compile flag is unchanged).
    `gn gen out\arm-rt21` (the GN config changed); then `ninja -C out/arm-rt21 -j 8 chrome` relinks only (0 compiles). Check
    `s9\state-0031` (336 + 1), `-release` (633 + 2), `-inputs` (702 + 2); anchor unchanged `a7aae630...`. Binaries: `chrome.dll`
    **`1e9be14a...`**, `venetium.exe` **`4430de30...`**, `chrome_elf.dll` **`3e87c999...`**; all ARM32 PEs lose the GUARD_CF DLL
    characteristic (load config GuardCFFunctionCount 0, no CF_FUNCTION_TABLE_PRESENT) and keep ARMNT, ASLR/DEP, sections,
    exports, version strings and import sets; the `__rt_*` shim is present. Package as step 31:
    `venetium-150.0.7871.226-win-arm32.zip` 439 entries (254 + 185 kit, the kit now also carries `diag\dbgrun.exe`),
    153,355,582 B **`22c3d1a6...`**, `verify_zip.py` PASS; vs step 34's zip 11 ARM32 PEs differ (CFG off) + 3 new `kit/diag`
    (dbgrun). `PACKAGE-MANIFEST.tsv` lists this pair; step 34's zip is `C:\handoff\superseded\...-arm32-0030.zip`. Revert 0031
    when lld is fixed (LLD-ARM-CFG-1), which restores CFG.
36. **Venetium 0032–0037 — the GPU "hangs"/720p fix (BATCH-GPU-1), plus BATCH-SBX-1's `dllprobe` in the kit.** From `F:\cr\src` at
    state-0031-release + inputs, apply 0032–0037 the s9 way (own index, pre/post manifests, `--check` first, `gn gen` after 0034's
    `angle.gni`), each with its `s9\state-00NN` (+ `-release`, `-inputs`); the 37-patch round trip removes to `adada54f…` PURE
    SUPERMIUM and re-applies to `a7aae630…` with every state MATCH. Final patch shas: 0032 `39e608d4`, 0033 `5a5743f3`,
    0034 `2d3ee78b` (4 files — `angle.gni`, `Renderer11.cpp`, `gl_display.cc`, `gl_switches.cc`), 0035 `6f7ce4d4` (`context_group.cc`
    + `gl_switches.cc`), 0036 `1a3a329a`, 0037 `acbba0ab`. (0034/0035 each forward their own GPU-process switch in
    `gl_switches.cc` `kGLSwitchesCopiedFromGpuProcessHostArray`, or the switch never reaches the GPU process — proven by §5 arm (b).)
    Rebuild both control dirs at `-j 8`: `ninja -C out/x86-rt21 chrome` (I386; A/B `chrome.dll`/`libGLESv2.dll` changed) and
    `ninja -C out/arm-rt21 chrome` (ARMNT; `pe_facts.py`; `find_rtdiv_code.py` shim present / `msvc:*=0`; 0 imports missing vs 15035,
    `d3d11`/`dxgi` delay in `chrome.dll`, `dxgi!CreateDXGIFactory/1` the only new static in `libGLESv2.dll`). §5 WARP (x86,
    `--use-angle=d3d11-warp --venetium-angle-d3d11-max-fl=9_3`) shows FL9_3 → `MAX_VARYING_VECTORS=7`, 0 "too few varyings" with
    0035 vs 27 with `--venetium-strict-es2-limits`. Package as step 31 (standard name, supersedes the 0031 zip) with the combined
    kit (GPU-1: `diag\d3dprobe.exe`+`run-d3dprobe.cmd`, `video\`, `stall\`, `gpu\`; SBX-1: `diag\dllprobe.exe`+`run-dllprobe.cmd`;
    README A3/A4): `venetium-150.0.7871.226-win-arm32.zip` 448 entries (254 + 194 kit), 153,945,159 B **`15e2bf7e…`**,
    `verify_zip.py` PASS; x86 control `304d6f49…`. The 0031 ARM zip (`22c3d1a6`) and pre-0032 x86 zip (`f02dc37a`) move to
    `C:\handoff\superseded\`. NOT MEASURED (device): whether the Surface 2 reaches FL9_3 at all (he reports its max is 9_1, so 0034
    likely stays inert and 0033/D3D9 carries it — `d3dprobe` decides) and whether the D3D9 driver reports `ps_2_a`/`ps_2_b`.

## Comparison order for the server (MOVE-1b §3)
1. MOVE-1's full-tree manifest **at state 0008** (`tree-manifest.tsv`, `09cd1a10…`) — step 12.
2. Venetium **0009**, then **0010**, each `--check` first (state-0009 checked in between) — step 13.
3. **state-0010** (`s9\state-0010.sha256`, 225 + 1 absent; `git diff` still `407cf087…`) — step 14.
4. **V8's test files** (14,095 against `v8-17700469.tsv`) — step 15.
5. The **normalized fingerprints** of step 17 — after step 16's `gn gen` (the post-ATL values once G7 is installed).
6. **state-0011** (`s9\state-0011.sha256`, 226 + 1 absent; `git diff` still `407cf087…`) — step 18.
7. **state-0011-inputs** (`s9\state-0011-inputs.sha256`, 513 + 2 absent) — step 19, before building `chrome`.
8. **state-0012** (`s9\state-0012.sha256`, 229 + 1 absent; `git diff` `0c9016ca…`) — step 20.
9. **state-0013** (`s9\state-0013.sha256`, 231 + 1 absent) — step 21; then **state-0013-inputs** (526 + 2 absent) before building.
10. **state-0013-release** (`s9\state-0013-release.sha256`, 528 + 2 absent) — step 23.
11. **state-0014** (`s9\state-0014.sha256`, 232 + 1 absent; `git diff` `4f71b3c8…`) — step 24; then **state-0014-release** (529 + 2 absent) before building.
12. **state-0015** (`s9\state-0015.sha256`, 233 + 1 absent; `git diff` `abb3dace…`) — step 25; then **state-0015-release** (530 + 2 absent) before building.
13. The **ARM32 builtins** `clang_rt.builtins-arm.lib` (167,018 B `6ca7e3e0…`) in `F:\cr\rt21-cr` — step 26.
14. **state-0016 … state-0027** (`s9\state-00NN.sha256` + `-release`), one after each patch, with the `git diff` anchors of the step 27
    table (`abb3dace…` → `42ee724a…` at 0019 → `9ca5d60f…` at 0026) — step 27.
15. **state-0027-inputs** (`s9\state-0027-inputs.sha256`, 699 + 2 absent) — step 28, before `gn gen out\arm-rt21`.
16. `gn gen out\arm-rt21` → 38,183 targets — step 29.
17. The ARM32 link: `venetium.exe` / `chrome.dll` ARMNT (`pe_facts.py`) — step 30.
18. The portable packages: per-file sha256 against `venetium\package\PACKAGE-MANIFEST.tsv` (both zips; DEVICE-1's ARM zip rows are the file as committed in `986d904` — the current file lists the `-rtdiv` ARM zip of step 32), the ARM zip's `verify_zip.py` PASS, and the x86 zip's `launch_check.js` PASS — step 31.
19. The ARM32 builtins after the shim (`clang_rt.builtins-arm.lib` 169,334 B `801fee5a…`, `compare_libs.py` PASS), the relinked
    binaries (`find_rtdiv_code.py`: shim present, `msvc:*=0` in every PE), and the `-rtdiv` ARM zip against
    `venetium\package\PACKAGE-MANIFEST.tsv` + `verify_zip.py` PASS — step 32 (the `-rtdiv` rows are in the file as of DEVICE-1b; the
    current file lists step 33's zip).
20. **state-0028** (`s9\state-0028.sha256`) and **state-0028-release**, anchor `9ca5d60f…`; `chrome_elf.dll` `12649432…`; the ARM zip
    `5650c910…` against `venetium\package\PACKAGE-MANIFEST.tsv` + `verify_zip.py` PASS — step 33 (the current file lists step 34's
    zip; step 33's rows are in the file as committed after ARM-FIX-1).
21. **state-0029** (+ `-release`, `-inputs`, anchor `9ca5d60f…`), then **state-0030** (+ `-release`, `-inputs`, anchor
    `a7aae630…`); `chrome.dll` `21e41be3…`, `venetium.exe` `fc85e06e…`; the ARM zip `e6b5d260…` against
    `venetium\package\PACKAGE-MANIFEST.tsv` + `verify_zip.py` PASS — step 34.
22. **state-0030** then **state-0031** (+ `-release`, `-inputs`), anchor `a7aae630...`; after 0031 all ARM32 PEs have no GUARD_CF
    (`chrome.dll` `1e9be14a...`, `venetium.exe` `4430de30...`, `chrome_elf.dll` `3e87c999...`); the ARM zip `22c3d1a6...` against
    `venetium\package\PACKAGE-MANIFEST.tsv` + `verify_zip.py` PASS - step 35.
Any mismatch stops the replay at that point; the fix happens on the laptop, never on the server.

File times do not matter: the manifest and the diff hashes are content-only.
