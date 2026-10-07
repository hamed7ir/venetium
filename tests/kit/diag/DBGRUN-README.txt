Venetium diagnostics - catch the 0xC0000409 crash (DEVICE-5)
============================================================

The 0029+0030 build fixed the sandbox launch: the GPU and the other sandboxed processes now start. But the browser still ends
by itself, exit code 0xC0000409, about 1.4 seconds after it opens, and it leaves no crash dump on this computer. 0xC0000409 is a
"fast fail": Windows ends the process so fast that neither the browser's own crash reporter nor Windows Error Reporting gets to
write anything.

These files run Venetium under a tiny debugger (dbgrun.exe) that is told about the crash before the process dies, and writes down
what it was and where. The debugger watches only the main Venetium process; the GPU, renderer and other child processes run
normally. Nothing on the computer is changed - everything is written inside the dbglogs folder.

This is a small add-on to the kit you already have. You do not need a new browser - the browser zip is unchanged.

Setup
-----
Put dbgrun.exe, dbg-run.cmd and this file into the kit\diag folder you already have (next to log-run.cmd), inside your Venetium
folder. (They also work if you put them directly next to venetium.exe.)

The runs
--------
Each run makes its own folder diag\dbglogs\run-1, run-2, ... with the debugger log, the crash report, the dump, the browser's
stderr, chrome_debug.log and a fresh profile. Do these, in order:

1. Double-click dbg-run.cmd (plain). Wait until the window says the run ended, then press a key.
2. Open a command prompt in the diag folder (open the folder in File Explorer, click the address bar, type cmd, press Enter) and
   run these one at a time, waiting for each to end:
     .\dbg-run.cmd --no-sandbox
     .\dbg-run.cmd --disable-features=SegmentationPlatformDeviceSwitcher
     .\dbg-run.cmd --disable-features=SegmentationPlatform
   (The last two tell us whether the crash is in the part that ran just before it in the previous trip's log.)

Each run should write, in its run-N folder:
  crash-1.txt  - the crash: its code, the fast-fail reason, and the address as module+offset
  crash-1.dmp  - a dump of the process (a few hundred KB)
  stderr.txt   - anything the browser or a library printed as it died
If a run ends with NO crash-1.txt, that run did not crash the way we are chasing - write that down too.

What to send back
-----------------
The whole diag\dbglogs folder: right-click it > Send to > Compressed (zipped) folder, and send that. A note of what you saw for
each run (did a window appear, how long, any message) helps.
