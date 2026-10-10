# Part B capture kit — device crash triage (issues #4 and #2)

These two issues can't be fixed blind — we need the faulting instruction and the
module it lives in. This kit captures that on the device; nothing here is run by
Claude, and no fix is guessed from it. Bring the `.dmp` back to the build server
and run `mdump.py` on it there.

## What we're measuring

- **#4 — `STATUS_DATATYPE_MISALIGNMENT` (0x80000002)**: an ARM32 alignment fault —
  an unaligned `LDRD`/`STRD`/`VLDR`/`VSTR` (these require 8- or 4-byte alignment on
  ARMv7; x86 would have tolerated it). The question is **where**: V8's JIT region
  (an anonymous RWX page — `mdump.py` prints `<no module>` for the PC), AOT code in
  `chrome.dll` (prints `chrome.dll+0xRVA`), or a system DLL. That decides the fix
  class (JIT codegen alignment vs a compiled-code `#pragma pack` / cast).
- **#2 — Telegram WebAssembly crash**: likely the same alignment class inside V8's
  wasm tier. Same capture; compare the PC's owning region and exception code to #4.

## Steps (on the device)

1. Copy this whole `partb-capture` folder onto the device (it's also inside the
   portable zip under `kit/partb-capture/`).
2. Open an **elevated** Command Prompt and run:

   ```
   enable-wer-localdumps.cmd
   ```

   That makes Windows write a full dump to `%LOCALAPPDATA%\VenetiumCrashDumps`
   whenever `venetium.exe` or a child process dies.

3. Reproduce the crash:
   - **#4**: launch Venetium normally and drive the workload that crashed before
     (the page/action from the issue report). If it's startup-only, just launch.
     Reproduce 2–3 times so there are a few dumps to compare.
   - **#2**: open Telegram Web (`web.telegram.org`), sign in, and do the action
     that triggered the WASM crash (opening a chat / media). One dump is enough.
   Note which child process died — the dump filename includes the PID; the window
     or `chrome://crashes` tells you renderer vs gpu vs utility.

4. Copy the newest `*.dmp` files off the device (USB/share). Optionally also grab
   `chrome://crashes` IDs if WER uploaded anything.

5. When finished, run `disable-wer-localdumps.cmd` (elevated) to restore the
   setting. Collected dumps are kept.

## Steps (on the build server)

```
python -I mdump.py the-crash.dmp
```

It prints the exception code, the faulting address as `module+RVA`, and the
faulting thread's registers (ARM32: r0–r12, sp, lr, **pc**, cpsr). For the PC it
says either `chrome.dll+0xRVA` / a system dll, or `<no module (JIT/anon?)>`.

- `chrome.dll+0xRVA` → symbolize it:
  `llvm-symbolizer --obj=chrome.dll.pdb 0xRVA` (use the matching build's pdb).
- `<no module>` for the PC → the fault is in a JIT/anonymous region (V8 or the
  regexp engine); that points the next batch at V8 codegen alignment, not AOT code.

Attach `mdump.py`'s output (and the `.dmp` if small enough) to the issue; that's
the input for the ISSUE-4 / ISSUE-2 fix pass.
