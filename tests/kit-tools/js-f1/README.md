# kit-tools/js-f1

Server-side tools for the kit page `js-f1` (the F1 probe inside the browser). Nothing here goes to the device and the page never loads
it. All of it is Node-only (use the server's Node, `F:\cr\src\third_party\node\win\node.exe`; paths are relative to these files, so a
scratch copy of `tests\` works as well). `js-f1-derive.js` used to sit in `kit\lib\`; it is Node-only and loaded by no page, so it
lives here and is not in the shipped kit.

| file | what |
|---|---|
| `js-f1-derive.js` | derives `kit\lib\js-f1-probe.js` and `kit\lib\js-f1-natives.js` byte for byte from `..\..\f1probe.js` (the original probe stays in `venetium\tests\`). `--check` compares with the files on disk (exit 1 on a difference), `--kit <dir>` aims it at another copy of the kit |
| `js-f1-manifest.js` | writes / checks `kit\manifest\js-f1.tsv` (header, 4 rows, sha256 of the files as they are now). Run it after any edit of the page, the glue or the derived files |
| `js-f1-nodetest.js` | the rehearsal gate (Node, real `lib\kit.js` in a stub DOM, not the product). Runs 14 parts in child processes and exits 0 only if all pass: `static` (script order, manifest, one `Kit.want`, ASCII, no network/worker/absolute path), `derive`, one `config:<name>` per configuration (original probe == derived probe; normal PASS; armed exactly one bad row; the F1 bug planted in the probe is seen; wrong tier wanted, natives off with a named tier, no natives script, wrong flags-reached-V8 claims and wrong lite-mode bit all FAIL), `matrix:absent` and `matrix:present` (the Maglev n/a decision: 9 reported-CPU variants x build records x Maglev absent via `--no-maglev` / present x `maglev` and `maglev-caller`, including `has_maglev: false` records for arm / arm64 / x64 / ia32 / X86 / no CPU, which must FAIL, against the x86 record, which must give n/a; plus "Maglev present but the tier is not reached"). `--only static,derive,config:maglev,matrix:absent` runs a subset, `--kit <dir>` aims it at a scratch copy |
| `js-f1-runall.js` | the runtime proof: runs `runner\run.js` for the js-f1 page in all 10 configurations, normal and `--armed`, reads the results and checks the gate (normal PASS, armed FAIL with exactly the planted row, no crash / console error, the expected tier word). `--maglev proven` for a build that has Maglev |

## What the page decides (see the header of `kit\lib\js-f1-host.js`)

* `maglev` and `maglev-caller` PROVE Maglev whenever `%IsMaglevEnabled()` is true. With it false the tier proof is n/a only where the
  build has no Maglev: `kit\lib\build.js` present (`Kit.build`, written per package by the packaging step) and `has_maglev === false` AND
  `v8_current_cpu === "x86"`, or no record and `Kit.arch().ia32`. A record that says `has_maglev: false` for any other CPU (arm, arm64, x64,
  or none named) is a FAILED check, "the build record says <cpu> has no Maglev, but V8 builds Maglev for <cpu>": V8 builds Maglev for every
  CPU but x86. Everything else (`has_maglev` true, a record without a boolean, x64, ARM32, an unknown or hung userAgentData) is a FAILED
  check too.
* On the Surface 2 (Windows ARM32) the page's "CPU architecture" row is expected to read architecture `""`, bitness `64`, wow64 `false`
  (Chromium has no Windows ARM32 case in `base\win\windows_version.cc`): a Chromium quirk, not a finding. The packaged ARM32 kit must carry
  `lib\build.js` with `has_maglev: true`, and `maglev` / `maglev-caller` must then say `proven maglev (callees, 6 functions)` /
  `proven maglev (callers, 24 functions)` in the saved result, with the `tier proof` rows green and not grey n/a.
* default, liftoff-only and no-liftoff select no JS tier: n/a (natives syntax on or off). A configuration that names a tier but has natives
  off is a FAILED check.
* The flags that reached V8 (from `lib\configs.js` only, plus `has_maglev` of the build record for Maglev where no flag decides) and the
  lite-mode bit (set only by `--jitless`) are checked, so jitless / ignition / sparkplug cannot silently run with the wrong flags.
