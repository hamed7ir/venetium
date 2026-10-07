# kit-tools/sbxprobe — source of `kit/diag/sbxprobe.exe`

The sandbox process-creation probe. Its working copy is `F:\cr\sbxprobe\src` on build server 2; this copy is the record.
- `sbxprobe.c` and `replay.inc` (Chromium's call per process type, derived from the sandbox source at state-0028).
- `build.sh`: rt2.1 clang-cl + lld-link, `/O2 /MT /W4 /GS /guard:cf`. It builds x64, x86 (server checks) and ARM32 (the device).
  ARM32 links `clang_rt.builtins-arm.lib` first (the `__rt_*` division shim). Paths are the server's.
- `build_inject.sh`: TEST builds with faked refusals (`SBX_INJECT`). They exercise the DIAGNOSIS and the section 6 failure path on
  the server and are never shipped; the packagers refuse them.

Versions:
- **1** (DEVICE-3): attributes and mitigation fields one at a time.
- **2** (DEVICE-3, after review `wf_50f0e839-955`): no error dialogs; call-shape section; CreateProcessAsUserW pass; expectations
  + UNEXPECTED list; replay DIAGNOSIS (leave-one-out, cumulative, "needed together"); real `venetium.exe` control with
  Chromium-like handles. Its device run (2026-10-04) found that 15035 ARM32 refuses `PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY`
  for every value in the 8- and 16-byte forms; that led to Venetium 0029.
- **3** (BATCH-ARM-FIX-2):
  - section 5: the older 4-byte (DWORD) form, both as single values and as Chromium's full call;
  - section 6: the mitigations a sandboxed child applies to itself after startup (Chromium PCHECKs those calls), under the own
    token and a USER_LIMITED-like token;
  - summary block "PROBE v3".
- **3.1** (after review `wf_8ca010d1-e4a`):
  - section 6 also runs under a USER_LOCKDOWN-like primary token with the initial token on the main thread and `RevertToSelf`
    in the child (renderer / storage service);
  - honest read-backs: 41 = the call returned TRUE and no read-back exists; 0x20000 = the read-back itself failed;
  - section 5 effect checks: a `--report` child compared with a plain baseline gives "effective" / "ignored" / "unclear";
  - DEP rows and `CREATE_BREAKAWAY_FROM_JOB` rows; leave-one-out for a refused 4-byte full call;
  - QuickEdit is off while it runs.

Results and runs: `ARM-FIX-2-RESULTS.md`; server runs in `F:\cr\sbxprobe\runs\`.
