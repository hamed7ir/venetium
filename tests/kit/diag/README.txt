Venetium diagnostics (kit\diag) - ARM-FIX-2, for the next Surface 2 trip
=========================================================================

What is new in this build: Venetium 0029. Windows 10 build 15035 on ARM32 refused every sandboxed child process (error 87,
"the parameter is incorrect") because of one sandbox setting (the process-creation mitigation policy). This build launches
the sandboxed processes without that setting on ARM32. Your re-run shows whether that clears the "GPU process isn't usable"
crash. A second, separate crash (exit code 0xC0000409, which also happened with --no-sandbox) leaves no dump on its own, so this
trip turns on Windows' own crash dumps first, and turns them off again at the end (step 7).

These files find venetium.exe two folders up (the browser folder) by themselves - leave them where they are.

1. Open an ADMINISTRATOR command prompt: Start menu > type cmd > right-click "Command Prompt" (on the touch screen: press and
   hold) > "Run as administrator". Type each line below and press Enter. "The operation completed successfully." is right;
   "Access is denied" means the prompt is not an administrator one.
2. First see whether crash dumps were ever set up on this computer:
     reg query "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps"
   WRITE DOWN whether it says "ERROR: The system was unable to find the specified registry key or value." (step 7 needs it).
3. Crash dumps ON for Venetium (two lines; the second keeps up to 30 dumps instead of 10):
     reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\venetium.exe" /v DumpType /t REG_DWORD /d 2 /f
     reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\venetium.exe" /v DumpCount /t REG_DWORD /d 30 /f
4. The sandbox probe (version 3.1): double-click kit\diag\run-probe.cmd. It runs a few hundred short tests (a few seconds to a
   few minutes) and ends with SUMMARY, DIAGNOSIS and "PROBE v3" blocks, then waits for a key. Do not tap or click inside the
   black window while it runs; if it seems frozen, press Esc once. It writes kit\diag\sbxprobe-<date>-<time>.log and changes
   nothing on the computer. New in version 3: it tests the older form of the refused setting, and the protections a child
   process applies to itself after it starts.
5. Launch Venetium as the kit README says (section B: double-click venetium.exe). If it closes by itself, wait about a minute,
   then type %LOCALAPPDATA%\CrashDumps into the File Explorer address bar: Windows should have written venetium.exe.<number>.dmp
   there. Dumps are expected for the main Venetium process (the window that closes); the GPU and sandboxed helper processes will
   probably not leave one. "No .dmp appeared" is also a result - write it down. A dump holds whatever the browser had open, so
   use only the kit's pages; each dump can be a few hundred MB.
6. The four logged runs, as on the last trip. Double-click kit\diag\log-run.cmd (plain). Then open a command prompt in kit\diag
   (open the folder in File Explorer, click the address bar, type cmd, press Enter) and type, one at a time:
     .\log-run.cmd --disable-gpu
     .\log-run.cmd --no-sandbox
     .\log-run.cmd --no-sandbox --disable-gpu
   Each run gets its own folder kit\diag\logs\run-N with its exit code. A crash shows as a negative exit code with its hex form,
   e.g. -1073740791 (0xC0000409) - just note it. Then carry on with the rest of the kit README, with the crash dumps still on.
7. At the very end:
   - send every .dmp file in %LOCALAPPDATA%\CrashDumps (zip them), and the kit\diag folder: right-click it > Send to >
     Compressed (zipped) folder. If that zip is too big, you may delete everything inside logs\run-N\profile EXCEPT the Crashpad
     folder. Add a short note per run: did a window appear, how long until it closed, any message;
   - turn the crash dumps OFF in an administrator command prompt. If step 2 said "unable to find", type:
       reg delete "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps" /f
     otherwise type:
       reg delete "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\venetium.exe" /f
