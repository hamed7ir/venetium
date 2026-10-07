# KIT-FINAL — the Venetium test kit as finished by BATCH-DEVICE-1 (2026-10-04)

The kit (`venetium\tests\kit\`, 178 files, `kit\MANIFEST.tsv` = 177 rows + itself) is the instrument the Surface 2 trip reads.
This file says what each page checks, what PASS means per configuration, and the **x86 control verdict** the device run is
compared against. His step-by-step guide for the trip is `kit\README.md`. Labels: **RUNTIME** (the browser ran) / **STRUCTURAL**.

## The x86 control verdict (RUNTIME) — the matrix
The x86 control build (`out\x86-rt21\venetium.exe` `858033a5…`, ia32, no Maglev), every configuration × {normal, armed}, each one
`index.html` run through the runner (all 9 pages in iframes; the browser's default autoplay policy, as on the device; fake camera and
microphone for webrtc), from a staged kit copy whose `lib\build.js` is the x86 record exactly as a package writes it (`v8_current_cpu
x86`, `has_maglev false`). Runs: `F:\cr\device-1\runs\matrix-<config>[-armed]\`; table made by `matrix_table.py`.

**Gate: PASS** — every normal run PASSes on all 9 pages; every armed run FAILs on all 9 pages, each with planted=true and exactly ONE
failed check (the planted one). 0 crashes. Each index run took 43–50 s here.

Cells: verdict checks/failed (P = planted). The device run must give the same verdicts; its check counts may differ only where the
page says so (section 3), e.g. js-f1 +1 row with an ARM record, and the Maglev configurations must read **proven maglev**.

| configuration | run | index | js-f1 | js-jit | wasm | images | media | canvas-webgl | webrtc | storage | fonts | tier proof (js-f1 / js-jit / wasm) | time |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| default | normal | PASS | PASS 79/0 | PASS 22/0 | PASS 56/0 | PASS 325/0 | PASS 87/0 | PASS 108/0 | PASS 58/0 | PASS 17/0 | PASS 61/0 | n/a (configuration selects no JS tier) / n/a (natives off) / wasm tier not provable without natives (default tiering; memloop first/best 2.5) | 43 s |
| default | armed | PASS | FAIL 79/1 P | FAIL 22/1 P | FAIL 56/1 P | FAIL 325/1 P | FAIL 87/1 P | FAIL 108/1 P | FAIL 58/1 P | FAIL 17/1 P | FAIL 61/1 P | n/a (configuration selects no JS tier) / n/a (natives off) / wasm tier not provable without natives (default tiering; memloop first/best 2.4) | 43 s |
| jitless | normal | PASS | PASS 95/0 | PASS 40/0 | PASS 17/0 | PASS 325/0 | PASS 87/0 | PASS 108/0 | PASS 58/0 | PASS 17/0 | PASS 61/0 | proven interpreter (callees, 6 functions) / interpreter 13/13 (lite-mode set) / n/a (no WebAssembly under --jitless) | 45 s |
| jitless | armed | PASS | FAIL 95/1 P | FAIL 40/1 P | FAIL 17/1 P | FAIL 325/1 P | FAIL 87/1 P | FAIL 108/1 P | FAIL 58/1 P | FAIL 17/1 P | FAIL 61/1 P | proven interpreter (callees, 6 functions) / interpreter 13/13 (lite-mode set) / n/a (no WebAssembly under --jitless) | 45 s |
| ignition | normal | PASS | PASS 95/0 | PASS 40/0 | PASS 83/0 | PASS 325/0 | PASS 87/0 | PASS 108/0 | PASS 58/0 | PASS 17/0 | PASS 61/0 | proven interpreter (callees, 6 functions) / interpreter 13/13 (lite-mode clear) / wasm natives (default tiering, tier reported): s10: liftoff,liftoff,liftoff,liftoff,liftoff,liftoff as called -> forced turbofan; ext: 124 operators liftoff as called -> forced turbofan; memory: 52 functions liftoff as called -> forced turbofan; hot loops after the runs: spin turbofan, memloop turbofan | 45 s |
| ignition | armed | PASS | FAIL 95/1 P | FAIL 40/1 P | FAIL 83/1 P | FAIL 325/1 P | FAIL 87/1 P | FAIL 108/1 P | FAIL 58/1 P | FAIL 17/1 P | FAIL 61/1 P | proven interpreter (callees, 6 functions) / interpreter 13/13 (lite-mode clear) / wasm natives (default tiering, tier reported): s10: liftoff,liftoff,liftoff,liftoff,liftoff,liftoff as called -> forced turbofan; ext: 124 operators liftoff as called -> forced turbofan; memory: 52 functions liftoff as called -> forced turbofan; hot loops after the runs: spin turbofan, memloop turbofan | 45 s |
| sparkplug | normal | PASS | PASS 89/0 | PASS 39/0 | PASS 83/0 | PASS 325/0 | PASS 87/0 | PASS 108/0 | PASS 58/0 | PASS 17/0 | PASS 61/0 | proven sparkplug (callees, 6 functions) / sparkplug 13/13 / wasm natives (default tiering, tier reported): s10: liftoff,liftoff,liftoff,liftoff,liftoff,liftoff as called -> forced turbofan; ext: 124 operators liftoff as called -> forced turbofan; memory: 52 functions liftoff as called -> forced turbofan; hot loops after the runs: spin turbofan, memloop turbofan | 44 s |
| sparkplug | armed | PASS | FAIL 89/1 P | FAIL 39/1 P | FAIL 83/1 P | FAIL 325/1 P | FAIL 87/1 P | FAIL 108/1 P | FAIL 58/1 P | FAIL 17/1 P | FAIL 61/1 P | proven sparkplug (callees, 6 functions) / sparkplug 13/13 / wasm natives (default tiering, tier reported): s10: liftoff,liftoff,liftoff,liftoff,liftoff,liftoff as called -> forced turbofan; ext: 124 operators liftoff as called -> forced turbofan; memory: 52 functions liftoff as called -> forced turbofan; hot loops after the runs: spin turbofan, memloop turbofan | 44 s |
| maglev | normal | PASS | PASS 82/0 | PASS 25/0 | PASS 83/0 | PASS 325/0 | PASS 87/0 | PASS 108/0 | PASS 58/0 | PASS 17/0 | PASS 61/0 | n/a (no Maglev in this build: the build record (lib/build.js) says has_maglev false for v8_current_cpu x86, the one CPU V8 builds no Maglev for) / n/a (no Maglev on ia32); reached interpreter,sparkplug,turbofan / wasm natives (default tiering, tier reported): s10: liftoff,liftoff,liftoff,liftoff,liftoff,liftoff as called -> forced turbofan; ext: 124 operators liftoff as called -> forced turbofan; memory: 52 functions liftoff as called -> forced turbofan; hot loops after the runs: spin turbofan, memloop turbofan | 43 s |
| maglev | armed | PASS | FAIL 82/1 P | FAIL 25/1 P | FAIL 83/1 P | FAIL 325/1 P | FAIL 87/1 P | FAIL 108/1 P | FAIL 58/1 P | FAIL 17/1 P | FAIL 61/1 P | n/a (no Maglev in this build: the build record (lib/build.js) says has_maglev false for v8_current_cpu x86, the one CPU V8 builds no Maglev for) / n/a (no Maglev on ia32); reached interpreter,sparkplug,turbofan / wasm natives (default tiering, tier reported): s10: liftoff,liftoff,liftoff,liftoff,liftoff,liftoff as called -> forced turbofan; ext: 124 operators liftoff as called -> forced turbofan; memory: 52 functions liftoff as called -> forced turbofan; hot loops after the runs: spin turbofan, memloop turbofan | 43 s |
| turbofan | normal | PASS | PASS 87/0 | PASS 36/0 | PASS 83/0 | PASS 325/0 | PASS 87/0 | PASS 108/0 | PASS 58/0 | PASS 17/0 | PASS 61/0 | proven turbofan (callees, 6 functions) / turbofan 13/13 / wasm natives (default tiering, tier reported): s10: liftoff,liftoff,liftoff,liftoff,liftoff,liftoff as called -> forced turbofan; ext: 124 operators liftoff as called -> forced turbofan; memory: 52 functions liftoff as called -> forced turbofan; hot loops after the runs: spin turbofan, memloop turbofan | 43 s |
| turbofan | armed | PASS | FAIL 87/1 P | FAIL 36/1 P | FAIL 83/1 P | FAIL 325/1 P | FAIL 87/1 P | FAIL 108/1 P | FAIL 58/1 P | FAIL 17/1 P | FAIL 61/1 P | proven turbofan (callees, 6 functions) / turbofan 13/13 / wasm natives (default tiering, tier reported): s10: liftoff,liftoff,liftoff,liftoff,liftoff,liftoff as called -> forced turbofan; ext: 124 operators liftoff as called -> forced turbofan; memory: 52 functions liftoff as called -> forced turbofan; hot loops after the runs: spin turbofan, memloop turbofan | 43 s |
| maglev-caller | normal | PASS | PASS 82/0 | PASS 26/0 | PASS 83/0 | PASS 325/0 | PASS 87/0 | PASS 108/0 | PASS 58/0 | PASS 17/0 | PASS 61/0 | n/a (no Maglev in this build: the build record (lib/build.js) says has_maglev false for v8_current_cpu x86, the one CPU V8 builds no Maglev for) / n/a (no Maglev on ia32); reached interpreter,sparkplug,turbofan / wasm natives (default tiering, tier reported): s10: liftoff,liftoff,liftoff,liftoff,liftoff,liftoff as called -> forced turbofan; ext: 124 operators liftoff as called -> forced turbofan; memory: 52 functions liftoff as called -> forced turbofan; hot loops after the runs: spin turbofan, memloop turbofan | 43 s |
| maglev-caller | armed | PASS | FAIL 82/1 P | FAIL 26/1 P | FAIL 83/1 P | FAIL 325/1 P | FAIL 87/1 P | FAIL 108/1 P | FAIL 58/1 P | FAIL 17/1 P | FAIL 61/1 P | n/a (no Maglev in this build: the build record (lib/build.js) says has_maglev false for v8_current_cpu x86, the one CPU V8 builds no Maglev for) / n/a (no Maglev on ia32); reached interpreter,sparkplug,turbofan / wasm natives (default tiering, tier reported): s10: liftoff,liftoff,liftoff,liftoff,liftoff,liftoff as called -> forced turbofan; ext: 124 operators liftoff as called -> forced turbofan; memory: 52 functions liftoff as called -> forced turbofan; hot loops after the runs: spin turbofan, memloop turbofan | 43 s |
| turbofan-caller | normal | PASS | PASS 105/0 | PASS 38/0 | PASS 83/0 | PASS 325/0 | PASS 87/0 | PASS 108/0 | PASS 58/0 | PASS 17/0 | PASS 61/0 | proven turbofan (callers, 24 functions) / turbofan 13/13 / wasm natives (default tiering, tier reported): s10: liftoff,liftoff,liftoff,liftoff,liftoff,liftoff as called -> forced turbofan; ext: 124 operators liftoff as called -> forced turbofan; memory: 52 functions liftoff as called -> forced turbofan; hot loops after the runs: spin turbofan, memloop turbofan | 43 s |
| turbofan-caller | armed | PASS | FAIL 105/1 P | FAIL 38/1 P | FAIL 83/1 P | FAIL 325/1 P | FAIL 87/1 P | FAIL 108/1 P | FAIL 58/1 P | FAIL 17/1 P | FAIL 61/1 P | proven turbofan (callers, 24 functions) / turbofan 13/13 / wasm natives (default tiering, tier reported): s10: liftoff,liftoff,liftoff,liftoff,liftoff,liftoff as called -> forced turbofan; ext: 124 operators liftoff as called -> forced turbofan; memory: 52 functions liftoff as called -> forced turbofan; hot loops after the runs: spin turbofan, memloop turbofan | 43 s |
| liftoff-only | normal | PASS | PASS 81/0 | PASS 22/0 | PASS 85/0 | PASS 325/0 | PASS 87/0 | PASS 108/0 | PASS 58/0 | PASS 17/0 | PASS 61/0 | n/a (configuration selects no JS tier) / n/a (this configuration selects no JS tier) / wasm natives (tier liftoff ASSERTED): s10: liftoff,liftoff,liftoff,liftoff,liftoff,liftoff as called -> forced turbofan; ext: 124 operators liftoff as called -> forced turbofan; memory: 52 functions liftoff as called -> forced turbofan; hot loops after the runs: spin liftoff, memloop liftoff | 49 s |
| liftoff-only | armed | PASS | FAIL 81/1 P | FAIL 22/1 P | FAIL 85/1 P | FAIL 325/1 P | FAIL 87/1 P | FAIL 108/1 P | FAIL 58/1 P | FAIL 17/1 P | FAIL 61/1 P | n/a (configuration selects no JS tier) / n/a (this configuration selects no JS tier) / wasm natives (tier liftoff ASSERTED): s10: liftoff,liftoff,liftoff,liftoff,liftoff,liftoff as called -> forced turbofan; ext: 124 operators liftoff as called -> forced turbofan; memory: 52 functions liftoff as called -> forced turbofan; hot loops after the runs: spin liftoff, memloop liftoff | 50 s |
| no-liftoff | normal | PASS | PASS 81/0 | PASS 22/0 | PASS 84/0 | PASS 325/0 | PASS 87/0 | PASS 108/0 | PASS 58/0 | PASS 17/0 | PASS 61/0 | n/a (configuration selects no JS tier) / n/a (this configuration selects no JS tier) / wasm natives (tier turbofan ASSERTED): s10: turbofan,turbofan,turbofan,turbofan,turbofan,turbofan as called -> forced turbofan; ext: 124 operators turbofan as called -> forced turbofan; memory: 52 functions turbofan as called -> forced turbofan; hot loops after the runs: spin turbofan, memloop turbofan | 43 s |
| no-liftoff | armed | PASS | FAIL 81/1 P | FAIL 22/1 P | FAIL 84/1 P | FAIL 325/1 P | FAIL 87/1 P | FAIL 108/1 P | FAIL 58/1 P | FAIL 17/1 P | FAIL 61/1 P | n/a (configuration selects no JS tier) / n/a (this configuration selects no JS tier) / wasm natives (tier turbofan ASSERTED): s10: turbofan,turbofan,turbofan,turbofan,turbofan,turbofan as called -> forced turbofan; ext: 124 operators turbofan as called -> forced turbofan; memory: 52 functions turbofan as called -> forced turbofan; hot loops after the runs: spin turbofan, memloop turbofan | 43 s |

Cells: verdict checks/failed (P = planted). Gate: normal = every page PASS; armed = every page FAIL, planted, exactly 1 failed check.
GATE: PASS

## Launchers from a path with spaces (RUNTIME)
The x86 portable zip unpacked to `F:\cr\device-1\profiles\launcher test with spaces\venetium-150.0.7871.226-win-x86\` with the kit
copy next to `venetium.exe` (the device layout), and two launchers run through `cmd /c` with extra arguments via `%*`
(`--headless=new --remote-debugging-port=0 --no-first-run --no-default-browser-check --use-fake-device-for-media-stream
--use-fake-ui-for-media-stream`), attached over DevTools (`launcher_test.js`):
- `default.cmd`: index **PASS**, 9/9 pages (js-f1 79, js-jit 22, wasm 56, images 325, media 87, canvas-webgl 108, webrtc 58,
  storage 17, fonts 61 checks); URL `file:///F:/cr/device-1/profiles/launcher%20test%20with%20spaces/…/kit/index.html?config=default`;
  profile `kit-profile-default` created next to the kit; the launcher exited 0 when the browser closed.
- `turbofan-caller.cmd` (three js-flags in one quoted `--js-flags`): index **PASS**, 9/9; the configuration line reads
  "turbofan-caller (js-flags --allow-natives-syntax --no-turbo-inlining --no-maglev)"; js-f1 105 checks including "flags reached V8"
  and "proven turbofan (callers, 24 functions)" in the tier-proof column (screenshot `F:\cr\device-1\launchers-spaces\turbofan-caller.png`).

## What changed in this batch (summary; details per page below and in `F:\cr\device-1\kit-notes\`)
- Shared: `lib\kit.js` (unknown `?config=` = failed check; an uncaught error publishes after 5 s; `Kit.arch()`; `Kit.build`),
  `lib\build.js` (new; null in the record, written per package), `index.html` (tier-proof column, also in the saved results),
  `runner\run.js` (the device's autoplay policy by default; profiles under `F:\cr\device-1\profiles`), `README.md` (his device guide),
  `MANIFEST.tsv` (merged; `F:\cr\device-1\tools\merge_manifest.py`).
- Pages: js-f1 (Maglev n/a only for an x86 build / ia32; flags-reached-V8 and lite-mode rows; liftoff-only/no-liftoff n/a),
  js-jit (same Maglev rule; jitless vs ignition), wasm (tier asserted; i64 shifts 31–33; narrow/extending/unaligned/f32/f64 memory
  probes with a forced-TurboFan pass; generator in `kit-tools\wasm`), images (circular error-exit check → STRUCTURAL info;
  semi-transparent REGRESSION rows → info because ARMv7 NEON un-premultiply rounds ties differently from x86), fonts (provenance in
  `kit-tools\fonts`), canvas-webgl (a throw outside a section still reports; per-version WebGL requirement), webrtc (quiet-microphone
  n/a; playout start-up up to 6 s), media / storage (default autoplay policy; manifests). Every touched page was proven in all 10
  configurations normal + armed before the matrix.


# Per page (what each page checks, what PASS means)

Source of every number below: the kit as it is now (D:\repo\supermium-rt\venetium\tests\kit\), the page notes
(F:\cr\device-1\kit-notes\), fix-results.md, fix2-results.md, and the stored run JSONs of the x86 control browser
(out\x86-rt21\venetium.exe, Chromium 150, ia32, headless, runner run.js, 10 configurations, normal and --armed:
F:\cr\device-1\runs\fix2-<page>-<config>[-armed]; webrtc fix3-..., storage fix-... because those pages were last run there).
"x86 control" always means that browser. Nothing in this file was measured on ARM32 or on the Surface 2.
Row counts are written total (real / info / n/a). index.html's "checks" column shows the total.
All 90 normal control runs are PASS with 0 bad rows; all 90 armed runs are FAIL, planted=true, with exactly 1 bad row (named per page);
armed runs have the same total as the normal run; 0 crashes, 0 console errors, 0 exceptions in every one of them.

## 1. How a verdict is formed

- `Kit.check(name, want, got, ok)` is a real check (red row when not ok). `Kit.eq` is a check by string equality.
  `Kit.info(name, got)` is a grey "(info)" row and `Kit.na(name, why)` a grey "n/a" row: both are ok by construction, never fail,
  and do not count as real checks. Only red rows fail a page.
- A page is PASS when it has at least 1 real check and every row is ok (lib\kit.js `done`). No checks at all = FAIL.
  A page that throws gets a red "uncaught error" / "unhandled rejection" row and publishes what it has 5 s later (FAIL).
  An unknown `?config=` name is itself a failed check (it never runs silently as "default").
- Armed run (`?armed=1`, the kit's self-test): `Kit.want(right, wrong)` returns the wrong value on its first call. Every page routes exactly one
  real check through it (wasm.html has two call sites, in the exclusive jitless / non-jitless branches). So an armed page must FAIL with
  planted=true. If a page planted nothing, `Kit.done` adds the failed check "ARMED RUN: this page planted no wrong expectation (page bug)"
  and planted stays false. The planted check of each page is named in its section.
- index.html runs the 9 pages in order (js-f1, js-jit, wasm, images, media, canvas-webgl, webrtc, storage, fonts), each in an iframe, and
  passes only `config` and `armed` (not `?webgl=`). Result per page by postMessage. Time limit per page (s): js-f1 240, js-jit 240, wasm 120,
  images 120, media 180, canvas-webgl 60, webrtc 90, storage 60, fonts 60. A page that does not report in time is FAIL ("no result within N s",
  planted false). Normal run: PASS = every page PASS. Armed run: PASS = every page FAIL and planted; otherwise the armed run FAILs.
  The table has page / verdict / checks / failed (first 3 names) / planted / time / tier proof (result.tier: reported by js-f1, js-jit, wasm only).
  "Save results" writes kit-results-<config>[-armed]-<time>.json with every row of every page.
- lib\build.js: `window.KIT_BUILD`. null in the record's copy. The packaging step writes each package's copy from that build's own
  v8_build_config.json (x86: v8_current_cpu x86, has_maglev false; ARM32: v8_current_cpu arm, has_maglev true - arm-rt21 has no top-level
  v8_build_config.json, the snapshot toolchain's clang_x86_v8_arm\v8_build_config.json says so). It may also carry `webgl` (canvas-webgl, section 3).
  With a null record the pages fall back to `Kit.arch()`: ia32 = architecture "x86" and (bitness "32" or wow64 true); x64, ARM32
  (architecture "" / bitness 64 / wow64 false: a Chromium quirk, STRUCTURAL, not a finding), ARM64, no userAgentData, a rejection or no answer
  in 3 s are all "not ia32".
- Maglev "n/a" (js-f1, js-jit only; configurations maglev and maglev-caller, `ia32: 'n/a'` in configs.js): if `%IsMaglevEnabled()` is true the
  Maglev tier is always PROVEN, whatever the CPU or record says. If it is false, n/a is allowed only with a record saying has_maglev === false
  AND v8_current_cpu === "x86", or with no record and `Kit.arch().ia32`. Everything else (record has_maglev true, an arm / arm64 / x64 / unnamed-CPU
  record that says false, a record without a boolean, x64, ARM32, no answer) is a FAILED check: Maglev is built for every CPU but x86, so a
  Maglev-less ARM32 browser cannot PASS. A record that disagrees with V8 is its own failed row.

## 2. The 10 configurations

Launchers (launchers\<name>.cmd) pass exactly the js-flags of lib\configs.js (`--js-flags=...`); default passes none. Do not append `--js-flags`
to a launcher: js-f1 and js-jit check that the tier switches reached V8 (sparkplug / maglev / turbofan enabled) and wasm checks the Liftoff tier; a dropped or replaced flag FAILs there.
"callee" = the tier of the 6 callee functions of the F1 probe (js-f1); "caller" = the tier of the 24 caller functions (js-f1, megamorphic warm-up).
js-jit measures the same 13 functions in both (the -caller configurations are nominal there: a PASS is not caller-inlining evidence).

| name | js-flags | tier that must be proven (who) | on x86 (ia32, no Maglev) | on the ARM32 device (Maglev present) |
|---|---|---|---|---|
| default | (none) | none; the as-shipped flag set, no natives | js-f1 / js-jit tier n/a, wasm "tier not provable without natives" | same |
| jitless | --jitless --allow-natives-syntax | interpreter (callee), lite-mode bit set; WebAssembly must be absent | PROVEN (js-f1 6 callees, js-jit 13/13) | same |
| ignition | --no-sparkplug --no-maglev --no-turbofan --allow-natives-syntax | interpreter (callee), lite-mode bit clear | PROVEN | same |
| sparkplug | --always-sparkplug --no-maglev --no-turbofan --allow-natives-syntax | sparkplug (callee) | PROVEN | same |
| maglev | --allow-natives-syntax | maglev (callee) | n/a: no Maglev in an x86 build (pages still PASS) | PROVEN required: js-f1 "proven maglev (callees, 6 functions)", js-jit "maglev 13/13"; n/a = FAIL |
| turbofan | --allow-natives-syntax | turbofan (callee) | PROVEN | same |
| maglev-caller | --allow-natives-syntax --no-maglev-inlining | maglev (caller) | n/a as for maglev | PROVEN required: js-f1 "proven maglev (callers, 24 functions)"; js-jit "maglev 13/13" (nominal) |
| turbofan-caller | --allow-natives-syntax --no-turbo-inlining --no-maglev | turbofan (caller) | PROVEN (js-f1 24 callers) | same |
| liftoff-only | --liftoff-only --allow-natives-syntax | no JS tier; wasm tier liftoff (wasm.html) | JS tier n/a; wasm ASSERTED liftoff | same |
| no-liftoff | --no-liftoff --allow-natives-syntax | no JS tier; wasm tier turbofan (wasm.html) | JS tier n/a; wasm ASSERTED turbofan | same |

Pages that use no V8 natives and no tier (images, media, canvas-webgl, webrtc, storage, fonts) behave the same in all 10 configurations
(the notes record identical results; for fonts the 'got' values of the non-info rows were compared and are identical); they are run 10 times only because the gate is "every configuration PASSes".

## 3. The pages

### js-f1 - the F1 probe (arguments adaptor on Windows ARM32)
- Real checks: 75 in every configuration = 72 probe checks (24 calls = sloppy/strict x plain/method x 0of2, 1of3, 2of3, 2of5, 4of2, 6of3; per call
  `params`, `arguments.length`, `receiver not among params`) + 3 page checks (probe ran 24 calls / 72 CHECK lines; `SUMMARY <mode> checks=72 bad=0`;
  "F1 pattern not seen": sloppy plain 2of3 params must not be `[global],1,2`, the F1 defect itself).
- Natives configurations add: `flags reached V8` rows (sparkplug / maglev / turbofan enabled, expected from the configuration's flags only; for Maglev,
  where no flag decides, from the build record's has_maglev: `build record (has_maglev) vs V8`), `tier lines found`, one `tier proof` row per function (6 or 24),
  and for jitless / ignition a `lite-mode bit` row per callee (set iff --jitless). Info rows: configuration, build record, CPU architecture, tiers enabled.
- Planted check: `sloppy plain 2of3 | params` (armed: its want gets " (PLANTED WRONG)").
- PASS per configuration: default, liftoff-only, no-liftoff: the 75 checks, tier "n/a (configuration selects no JS tier)". jitless / ignition / sparkplug /
  turbofan / turbofan-caller: the 75 + flags rows + the tier proven for every function (turbofan+maybe-deopted does not count). maglev / maglev-caller:
  on x86 the tier is n/a and one extra check "no function reports Maglev code while %IsMaglevEnabled() is false" (a consistency check of two V8
  answers, not Maglev evidence); on ARM32 every callee (6) / caller (24) status must start `maglev(`. Rules in section 1.
- Device notes: CPU row reads architecture '' / bitness 64 / wow64 false on Windows ARM32 (not a finding). Build record row on the device must show
  arm / has_maglev true. If the package carries no record, the CPU rule decides and ARM32 is not ia32, so a Maglev-less ARM32 still FAILs.
  Counted from lib\js-f1-host.js, not measured: a package record with a boolean has_maglev adds one real row `build record (has_maglev) vs V8` to turbofan,
  maglev, maglev-caller, liftoff-only, no-liftoff (natives on, no flag that decides Maglev), so the ARM32 totals expected are maglev 83, maglev-caller 101,
  turbofan 87, liftoff-only 81, no-liftoff 81 (others as the control); an x86 package copy is +1 in the same five.
- x86 control: default 79 (75/3/1), jitless 95 (91/4/0), ignition 95 (91/4/0), sparkplug 89 (85/4/0), maglev 81 (76/4/1),
  turbofan 86 (82/4/0), maglev-caller 81 (76/4/1), turbofan-caller 105 (101/4/0), liftoff-only 80 (75/4/1), no-liftoff 80 (75/4/1). Tier words: "proven interpreter
  (callees, 6 functions)" (jitless, ignition), "proven sparkplug ...", "proven turbofan (callees, 6 functions)", "proven turbofan (callers, 24 functions)",
  maglev / maglev-caller "n/a (no Maglev in this build: no build record; the browser reports ia32 ...)". Armed bad row always `sloppy plain 2of3 | params`.
  Page time ~0.6 s (limit 240 s).

### js-jit - core.js, 13 functions run 4 times, answers against the host
- Real checks: 16 in default = `typeof WebAssembly` ('undefined' iff --jitless, else 'object'), `core.js ran to completion`, 13 answers (12 compared with host python
  expected.py, `sin_crosstier` with host Node x64 - libm dependent), `core.js printed no unexpected keys`. Natives configurations add `natives syntax accepted`,
  `flags reached V8` rows (jitless / ignition / sparkplug 3, turbofan-caller 1, others 0), 13 `tier.<case>` rows, and for interpreter one `lite-mode bit` row.
  A missing reference value fails (never passes vacuously). Info: print collector, configuration, CPU architecture, build record, data-file agreement
  (python = Node on the 12 answers is a build-time property of the data file, not a browser check), tiers switched on.
- Planted check: `answer overflow (host python)` (armed: expectation +1).
- PASS per configuration: default / liftoff-only / no-liftoff: answers only, tier n/a. jitless: lite-mode set + 13/13 interpreter + WebAssembly absent. ignition:
  13/13 interpreter, lite-mode clear. sparkplug / turbofan / turbofan-caller: 13/13 in that tier. maglev / maglev-caller: x86 "n/a (no Maglev on ia32); reached
  ..." (rule in section 1); ARM32: `maglev 13/13` required (tierProof "proven"); Maglev-less ARM32 = FAIL with 14 bad rows (`Maglev is available in this V8` + 13 tier rows).
  A record that says has_maglev false while V8 has Maglev: row `lib/build.js and this V8 agree about Maglev` fails and the proof still runs.
- Device notes: same CPU row quirk as js-f1. Counted from the page, not measured: an ARM32 Maglev run has 30 real rows (like turbofan: 16 + natives accepted + 13 tier rows).
  sin_crosstier compares with host Node x64: a libm difference on ARM32 would look like a JIT failure and this page cannot separate the two.
- x86 control: default 22 (16/5/1), jitless 40 (34/6/0), ignition 40 (34/6/0), sparkplug 39 (33/6/0), maglev 25 (17/7/1), turbofan 36 (30/6/0), maglev-caller 26 (17/8/1),
  turbofan-caller 38 (31/7/0), liftoff-only 22 (16/5/1), no-liftoff 22 (16/5/1). Tier words: "interpreter 13/13 (lite-mode set|clear)", "sparkplug 13/13", "turbofan 13/13",
  maglev / maglev-caller "n/a (no Maglev on ia32); reached interpreter,sparkplug,turbofan". Armed bad row `answer overflow (host python)`. ~0.6 s each (limit 240 s).

### wasm - WebAssembly, 32-bit-sensitive operators, memory, wasm tier
- Real checks: default 48, natives configurations 73 (liftoff-only / no-liftoff 76). Groups: API members present; s10 module (the one STRUCTURAL row: the embedded bytes equal
  s10/jit/wasm.js's assembler output; validate, exports, instance shape); `wasm_i32` / `wasm_i64` (4 repetitions agree, equal host python and host Node x64); spot values, BigInt / Number,
  traps (RuntimeError x4); async compile / instantiate / streaming; invalid modules (CompileError, TypeError, LinkError); the ext module (4,276 bytes): 124 operators in 8 groups
  (15 / 10 / 10 / 8 / 15 / 16 / 5 / 45) incl. i64 shifts / rotates by variable amounts and by constants k = 0, 1, 31, 32, 33, 63, 64, 65, 0x100000020, each digest equal to the host's,
  plus a shape row (executed groups and counts equal the host record); memory: 27 probes + 5 JS-side `WebAssembly.Memory` probes, then a script of 1,423 steps in 5 phases
  (208 / 118 / 86 / 966 / 45: A loads at every address 0..16, B unaligned stores, C page end + trapped store writes nothing, D float bit patterns incl. signalling NaN payloads / -0 /
  denormals at 4-6 alignments, E static offsets / constant addresses / growth) over 52 functions against a python model, plus a shape row; hot loops spin / memloop. Natives configurations
  run the cases as called AND again after `%WasmTierUpFunction` forces TurboFan (memory script on a fresh instance).
- Planted check: `wasm_i32 (s10 case, 4 repetitions, last value) = the host (python, expected.py)` (armed: +1); under jitless `typeof WebAssembly under --jitless (V8 does not expose it)`.
- PASS per configuration: jitless: 4 real rows (typeof undefined, using it throws ReferenceError, not in globalThis and no WebAssembly.Module, %IsTurbofan / Sparkplug / MaglevEnabled all false)
  and 11 n/a. default: every case correct, no tier claim (n/a). ignition / sparkplug / maglev / turbofan / maglev-caller / turbofan-caller: cases correct, every function is compiled code as called
  (tier reported, not asserted), forced-TurboFan pass correct; the JS flag does not influence wasm. liftoff-only: cases correct AND all 6 + 124 + 6 + 52 functions Liftoff as called AND the
  hot loops still Liftoff after a soak of up to 6 s (it stops at the first TurboFan). no-liftoff: cases correct AND every function TurboFan from the first call.
- Device notes: a tab killed by an unaligned f32 / f64 access on ARM32 (alignment fault) or any FAIL in memory-script phase D is a real finding, not a harness bug. Info row `wasm trap handler` (true = out-of-bounds by signal handler, false = explicit bounds checks) is false on the x86 control. liftoff-only is slower on purpose
  (the soak). Step times are saved in the result (extra.wasm.stepMs, totalMs). Page watchdog 100 s, index limit 120 s.
- x86 control: default 56 (48/7/1), jitless 17 (4/2/11), ignition / sparkplug / maglev / turbofan / maglev-caller / turbofan-caller 83 (73/10/0) each, liftoff-only 85 (76/9/0), no-liftoff 84 (76/8/0).
  Tier words: "wasm natives (tier liftoff ASSERTED): ..." / "(tier turbofan ASSERTED)" / "(default tiering, tier reported)"; default "wasm tier not provable without natives"; jitless "n/a (no WebAssembly under --jitless)".
  Armed bad row `wasm_i32 ... = the host` (jitless: `typeof WebAssembly under --jitless`). Page time 0.6-1.2 s; liftoff-only 7.1-7.2 s (soak 6.1 s).

### images - decoders and error exits (JPEG, PNG, GIF, WebP, AVIF, BMP, ICO)
- Real checks: 307 = 53 valid files x (size 53; picture: exact checksum from the generator 39, or 8x8 mean grid 14, plus 2 opaque-pixels + alpha rows; createImageBitmap gives the same pixels 53);
  11 `REGRESSION vs the x86 build` rows (opaque files only); 12 encoder rows (6 round trips x [toBlob returns the type, the decoded result is the picture]); 55 broken files x 2 routes = 110 (49 "fails cleanly" per route, 6 "SURVIVES" per route =
  no hang / crash, identical outcome on all 5 attempts, any class accepted); route 3: `img.decode()` resolves for all 53 valid files, NO HANG for all 55 broken ones; 2 LIVENESS rows; 9 "decoder healthy after the errors".
- Info only (18 rows): the 7 decoder rows + JXL + encoders + "JPEG error-exit files" are STRUCTURAL static claims about the source tree, not queried from the running build, and the error-exit class is an observation,
  not proof that the longjmp ran. The 3 semi-transparent REGRESSION rows (wheel-png, wheel-webp, wheel-avif) are info: the canvas read-back un-premultiplies semi-transparent pixels and Skia rounds ties up on ARMv7 NEON
  but to even on x86 (SkSwizzler_opts.inc pixel_round_as_RP, STRUCTURAL), so a correct ARM32 decode can give another full-RGBA checksum; the row shows the checksum and "(x86 build: <hex>, same|different)". Those
  pictures are still judged by the opaque + alpha, 8x8 grid and createImageBitmap rows. Also info: browser CPU, heartbeat LIVENESS (cannot fail), two route-3 notes, totals.
- Planted check: `valid png-rgb8: pixel checksum (independent: ...)`, want 00000000, got 5a7cf3b6. No tier logic: the 10 configurations are the same run.
- PASS means: all 307 rows green. If only REGRESSION rows fail on ARM32 and the 8x8 grid rows pass: a numeric difference to triage (SIMD rounding), not a broken picture. "different" on the three info rows means nothing
  (prediction from the Skia source and a simulation, not measured: wheel-png and wheel-avif probably different, wheel-webp probably same).
- Device notes: a crashed or silent images page IS the D42 failure signal (no in-page check can see a renderer crash; a frame gone blank / "Aw, Snap" is itself a finding, index.html gives up after 120 s). The run-time evidence
  for the longjmp is differential: the renderer survives the 5th broken file (jpeg-badhuff-count, the first error-exit file) and the page reaches its verdict; a build without the D42 fix dies there (0xC0000028 per the audit).
  If the two route-3 rows fail on the device ("no answer" / "rejected") that is a finding about worker-thread decode in the device's compositing mode, not necessarily a broken image.
- x86 control: 325 (307/18/0) in all 10 configurations, armed 325 with 1 bad (the planted row); 2.1-3.7 s (limit 120 s).

### media - codec table and playback under the default autoplay policy
- Real checks: 75 = 14 codec-table rows (D13 must: avc1, AAC, avc1+AAC `canPlayType` "probably" and `MediaSource.isTypeSupported` true = 6; kit-assumption formats vp8, vp9, av01, opus, vorbis, mp3, flac, pcm:
  `canPlayType` not empty = 8) + 54 clip rows (12 clips played: avc1, vp8, vp9, av01, opus, vorbis, aac, aac-adts, mp3, flac, flac-native, pcm; each: loads without error, currentTime passes 2 s [flac-native: plays to its 1.07 s end],
  video: totalVideoFrames > 0, decoded frame size, a picture that is not blank (luma sd > 8, mean 5..250); audio: decoded bytes > 0 and decodeAudioData gives non-silent PCM of the right length; the generated pcm clip also checks
  level: sine peak 0.3) + 6 MSE rows (fragmented avc1 + AAC through MediaSource / SourceBuffer) + 1 row that every embedded clip matches its recorded size and sha256 (13/13).
- Info (12): page capabilities, MSE support of the stock formats, HEVC (`hvc1` is optional: reported, never failed; on x86 canPlayType "" and its clip "not asserted"), clip count and page time. mediaCapabilities (smooth /
  powerEfficient) appears in the page's codec table, not as a check.
- Planted check: `avc1 (H.264) / avc1: decoded frame size`, want 1281x720, got 1280x720.
- PASS means: H.264 and AAC supported and playing (D13), every other listed format supported and playing. A FAIL in a row marked "KIT ASSUMPTION" (the eight stock formats) is for the lead to judge: a build meant to ship without
  a codec gets a false FAIL there.
- Device notes: no launcher passes `--autoplay-policy`, so the browser's default policy applies and nobody clicks: every clip, the audio-only ones too, is played muted through a `<video>` element (the only element the default
  policy starts without a gesture); a muted `<audio>` or an unmuted element would be refused. Which decoder runs (FFmpeg software vs D3D11 / DXVA) is not visible to the page (README step 19: kVideoDecoderName in chrome://media-internals).
  Time: no new clip starts after 110 s (a clip left unrun is a failed check "played ... page budget used up"), index limit 180 s.
- x86 control: 87 (75/12/0) in all 10 configurations; 27.2-28.2 s page time (clips play in real time).

### canvas-webgl - 2D canvas checksums, WebGL 1 and WebGL 2
- Real checks: 94 = 44 for 2D (2 JS-engine rows: the scene generator and the software reference model reproduce the golden operation list and CRC32, no canvas; exact scene CRC = golden on default and willReadFrequently contexts, OffscreenCanvas, canvas reset, PNG round trips
  via blob / data URL / createImageBitmap, JPEG / WebP encode + decode error bound, composite modes, shadows, filters, anti-aliased shapes and gradients with tolerances, drawImage, putImageData / getImageData, Path2D, text,
  a 2048x2048 canvas) + 22 for WebGL 1 + 28 for WebGL 2 (VERSION / GLSL strings, renderer strings, MAX_TEXTURE_SIZE, clear + readPixels, scissor, shader compile + link with active attribute and uniforms compared with the page's
  own GLSL, draw, indices, interleaved attributes, textures, canvas interop, FBO, depth, blend, no GL error, context loss and restore; WebGL 2 adds instancing, std140 UBO, integer texture, 3D texture, texStorage).
- Info (14): context attributes, limits, extensions, text-CRC (x86 control be9cb9fc, text drawn with 24px Arial fallback, measureText 241.48 px; the ARM32 value will differ), anti-aliased scene CRC32 (x86 5bedacff, backend dependent),
  WebGL renderer strings, and `WebGL coverage` (requirement, source, tested / NOT AVAILABLE / NOT TESTED per version).
- Planted check: `2D canvas (default context): CRC32 of the 160x120 exact scene = golden`.
- WebGL requirement (comes from the run, never from the browser under test: URL `?webgl=` or lib\build.js `KIT_BUILD.webgl`, the stricter wins; index.html and the launchers forward no `?webgl=`): absent = nothing required;
  "required" = WebGL 1 must exist (WebGL 2 reported); "webgl2" = both; any other value is a failed check. Missing context, not required: info row + banner (WebGL 1 missing = RED banner "NO WEBGL AT ALL" when WebGL 2 is missing
  too, and the PASS covers 2D only; WebGL 2 missing while WebGL 1 works = GREY information banner only, normal). Missing context, required: failed check "WebGL <v> context" and a red banner.
- Device notes: WebGL 2 absent on a Direct3D 9_x GPU is a grey note, not a failure (that GPU fact is from knowledge of ANGLE, not measured; the page does not depend on it); whether the Surface 2 must give WebGL, and which setting,
  is the lead's decision (the recommended build.js value if yes is `webgl: 'required'`; 'webgl2' would be a permanent FAIL on a 9_x GPU). The shader row would FAIL if ANGLE on the device reports the active attribute or uniforms
  differently. Read the rows `WebGL 1 context` / `WebGL 2 context` / `WebGL coverage` and the banner: a PASS with a red banner does not cover WebGL.
- x86 control: 108 (94/14/0) in all 10 configurations (WebGL 1 and 2 both tested, banner none, ANGLE on the Microsoft Basic Render Driver); page time ~0.6 s (watchdog 45 s, index limit 60 s).

### webrtc - getUserMedia, loopback peer connections, 10 s of media flow
- Real checks: 30 = WebRTC + getUserMedia exist; getUserMedia returns 1 audio + 1 video track; tracks live; echoCancellation / noiseSuppression / autoGainControl applied (3); microphone and camera enumerated; local preview >= 3 frames in
  2 s; loopback connected; peer B got audio + video; first packet / frame arrive; 7 flow rows over 10 s (every 2 s window must grow: inbound audio packets, inbound video packets, frames decoded, source samples duration, outbound audio
  packets, outbound video frames; `totalSamplesReceived` may start up to 6 s late, reported, then every window must grow, at least 2); inbound frame size; remote `<video>` presented >= 10 frames in 10 s; remote frame colours within 40 of
  the local frame (both elements readyState >= 2); audio levels valid; concealment <= 10 %; the signal row; echo-canceller statistics present; tracks not muted; Opus negotiated; a video codec negotiated; tracks end on stop; peer
  connections close.
- Info (28): constraints, devices, settings, stats, codecs, encoder / decoder implementation, ICE pair, timeline, the rnn_fc row. rnn_fc is EXERCISED, not VERIFIED: no check depends on it.
- Planted check: `getUserMedia({audio, video}) returns the tracks` (want 1 audio + 1 video, armed 2 audio + 0 video).
- The signal row (`inbound audio carries the signal (inbound energy > 0)`) is real only when the remote element plays unmuted and the loudest source audioLevel is >= 0.003 (about 100 LSB); otherwise it is n/a with the reason
  (muted by the autoplay policy / digital silence / source level too low for the 0.1 playout gain). n/a never fails.
- Device notes: real camera and microphone, with the permission prompt: click Allow within 45 s (no answer = the tracks check fails and the page ends early). Talk or clap about 10 s so the signal row can be real. The remote element plays
  the microphone back at volume 0.1: use headphones or turn the volume down. Keep the window in front and unobscured: video frame callbacks stop in a hidden window (the two frame rows say the page's visibility in `got`).
  H.264 is listed in the sender capabilities but the loopback negotiates VP8 on x86 (libvpx); H.264 over WebRTC is not measured. Budget: page 75 s (hard stop 80 s), index limit 90 s.
- x86 control (fake camera and microphone, loud beep: max source level 0.986-1.0, inbound energy about 2e-2): 58 (30/28/0), 0 n/a, in all 10 configurations; page time 12.6 s; `totalSamplesReceived` start-up 0 s in all 10 normal fix3 runs.

### storage - localStorage, IndexedDB, Blob
- Real checks: 13 = 3 generator checks (the JS engine reproduces the host CRC-32 of the test payloads: 98,304 characters, 40,000 bytes, 300,000 bytes; computed host-side by Node zlib.crc32, not in the browser); localStorage 3 (set + get,
  the 98,304-character value read back = host CRC, removeItem reads null); IndexedDB 5 (put + get, value survives close() and a new open(), 40,000-byte and 300,000-byte Uint8Array through a new connection = host CRC [the larger one is
  stored as a blob file next to the database], deleteDatabase removes the data); Blob 2 (size and bytes, a script Blob loaded through its blob: URL). No fetch / XHR / worker.
- Info (4): the stamp / value an earlier load left in localStorage / IndexedDB (null in a fresh profile; non-null when the page is run twice in one profile), the download link, a statement of what the checks prove.
- Planted check: `localStorage set + get`. No tier logic: 10 identical runs.
- Device notes: the page proves API round trips inside one page load; it does NOT prove that data reaches the profile on disk (that needs a restart between two runs) and cannot see that the download arrived. After the run the
  Downloads folder holds venetium-kit-download.txt (45 bytes, "Venetium kit download venetium-<time>") next to the kit-results-*.json. On file:// every document is its own opaque origin: a SecurityError would be reported, not hidden.
- x86 control: 17 (13/4/0) in all 10 configurations; 0.6-0.8 s (limit 60 s); the runner records one completed 45-byte download.

### fonts - Venetium's name and native letters in 82 translations
- Real checks: 55 = 6 controls (the unassigned reference code points give one identical visible box; blank canvas and a space read blank; "A" and Cyrillic / Greek letters are neither blank nor the box; unassigned code points in six script
  blocks, drawn with that block's language, classify as the box; the canvas language is honoured: four Han characters under ja, zh-CN, zh-TW, ko give 4 distinct drawings each) + 9 Hindi / Nepali / data rows (the product name in Devanagari in
  the app menu and all 4 strings, no old Chromium name left, the name drawn on its own has glyphs, data sanity) + 40 language rows (39 scripts / locales and the Latin group of 43 locales: every distinct native letter of the translations is drawn
  with a real glyph, never the missing-glyph box, and each string run differs from a run of boxes). 1,182 letters in 40 cases, 82 translations.
- Info (6): canvas language, installed fonts found / not found (probe by metrics), which font drew each script (inference by identical pixels), scope, user agent.
- Planted check: `hi: the menu item "About Venetium" names Venetium in Devanagari` (armed wants the old Chromium name).
- PASS means: no script of the product's own strings draws boxes. Device notes: an x86 PASS says nothing about the Surface 2's fonts; a failed language row or the Han control is a font finding (the Han control fails if the device lacks
  one of Yu Gothic / Microsoft YaHei / Microsoft JhengHei / Malgun Gothic: two languages then fall back to the same font). A failure "fonts: all cases checked within 50 s" means slowness, not missing fonts. The README step asks the person
  to look at the page for empty boxes too.
- x86 control: 61 (55/6/0) in all 10 configurations; ~1.6 s (budget 50 s, index limit 60 s).

## 4. Known limits / NOT MEASURED

- Nothing ran on ARM32 or the Surface 2. Every control number is from the x86 browser (headless, runner, fake camera and microphone). The ARM32 branches (Maglev proof in Chromium 150, build record vs V8, the CPU row '' / 64 / false) are covered by
  Node rehearsal with mocked userAgentData, x86-browser mutations with a faked lib\build.js, and d8 replays; the Maglev proof branch has never run against real Maglev code in Chromium 150 (only under the ARM-simulator d8, which is not the browser,
  and as a failure mode in the x86 browser). What userAgentData reports on the Surface 2 is STRUCTURAL.
- Maglev n/a without a record rests on the browser's own userAgentData report: a spoofed x86 + wow64 report on a Maglev-less ARM32 browser would pass as n/a (js-jit d8 case C1d). A record {x86, has_maglev false} copied onto an ARM package would grant n/a
  too. The packaging step must write lib\build.js from that build's own v8_build_config.json; nothing yet refuses to ship an x86 or ARM32 package whose lib\build.js is still null.
- js-jit: the -caller configurations are nominal; sin_crosstier is compared with host Node x64 (a libm difference on ARM32 would look like a JIT failure); `typeof WebAssembly` under --jitless encodes V8 150 behaviour; Maglev to TurboFan tier-up under
  real concurrent compile on the device is untested (contingency `--max-opt=2` in configs.js, not applied).
- wasm: ARM32 behaviour of the float, unaligned and offset phases is unmeasured. liftoff-only is told from a dropped flag only by the 6 s soak: on a device so slow that default tiering would not promote in 6 s a dropped flag shows as a false PASS (never a
  false FAIL); SOAK_MS is in wasm.html. Default-tiering natives configurations report the tier, they do not assert it. A data file whose script and shape record are rewritten together passes at run time; that is guarded by the manifest sha256 and the
  byte-identical regeneration, not by the page.
- images: the STRUCTURAL rows are static claims about the source tree. Which thread decoded and what pixels it produced is never observed (route 3 reads only whether `img.decode()` completed, and cc reports resolved even for a broken file whose size was known:
  35 resolved, 20 rejected on x86). Whether `img.decode()` resolves for all 53 valid files in the device's compositing mode (software vs GPU raster), the device time against the 120 s limit, libyuv (AVIF) and libwebp NEON rounding for the 11 opaque
  REGRESSION rows are unmeasured. A damaged picture of known size painted in the gallery goes unnoticed (disclosed in the row names). Several "fails cleanly" rows stay weak by design.
- media: picture and audio checks are structural (non-blank picture, non-silent audio); a wrong-but-structured decode on ARM32 (wrong colours, noise instead of audio) passes; only the generated pcm clip checks level. A reference-value check was deferred
  (needs an independent decoder and a tolerance confirmed by a real ARM32 run). The decoder chosen on the device and sound output are not visible to the page.
- canvas-webgl: whether the Surface 2 gives WebGL 1 or WebGL 2 and what its ANGLE reports for the shader row are unmeasured; the WebGL requirement is not set by default, and index.html / the launchers forward no `?webgl=` (only a `webgl` field in the package's lib\build.js
  would require it there); the 2D default and willReadFrequently contexts were not compared on a device (same software backend on the x86 VM).
- webrtc: a real camera, microphone, permission prompt and headed window are unmeasured (the runner uses fake devices and no prompt); H.264 over WebRTC and rnn_fc (exercised, not verified) are not measured; the 0.003 source-level gate comes from synthetic noise
  through the fake device (at and above it the signal row was real and ok in every run); the concealment row prints "(NaN %)" when totalSamplesReceived is 0 (cosmetic); the window-stall row flaked once in 21 runs under server load before it was relaxed to
  allow a start-up of up to 6 s.
- storage: persistence across a browser restart, the download bubble of a headed browser, quota behaviour on the device and (as an assertion in the runner) the arrival of the download are not measured.
- fonts: the recommended hardening (about 20 script blocks and per-language reference agreement) was not added because it would make a device font that draws its own .notdef a loud false FAIL; the armed run proves the harness path, not the glyph detector.
- index.html keeps all 9 documents loaded (2 WebGL contexts and the webrtc page included) on a 2 GB device; setting each finished iframe to about:blank was suggested and not done.
