@echo off
rem ============================================================================
rem Venetium BATCH-ISSUES-1 Part B - enable WER LocalDumps for venetium.exe
rem Run this ON THE DEVICE, from an elevated (Administrator) command prompt.
rem It makes Windows write a FULL minidump whenever venetium.exe (or any of its
rem child processes - renderer, gpu, utility) crashes, instead of silently
rem closing. Dumps land in %LOCALAPPDATA%\VenetiumCrashDumps.
rem
rem This is a per-executable WER setting (HKLM). It touches the registry, so
rem you are running it yourself - Claude does not change device settings.
rem Undo with disable-wer-localdumps.cmd when you are done.
rem ============================================================================
setlocal
set KEY=HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\venetium.exe
set DUMPDIR=%LOCALAPPDATA%\VenetiumCrashDumps

if not exist "%DUMPDIR%" mkdir "%DUMPDIR%"

reg add "%KEY%" /v DumpFolder /t REG_EXPAND_SZ /d "%%LOCALAPPDATA%%\VenetiumCrashDumps" /f
reg add "%KEY%" /v DumpCount  /t REG_DWORD     /d 10 /f
rem DumpType 2 = full dump (has the crashing thread's stack + the memory at PC,
rem which mdump.py needs to show the faulting instruction). 1 = mini (smaller).
reg add "%KEY%" /v DumpType   /t REG_DWORD     /d 2 /f

echo.
echo WER LocalDumps enabled for venetium.exe
echo Dumps will be written to: %DUMPDIR%
echo.
echo Reproduce the crash (see README.md), then copy the newest *.dmp off the
echo device and run:  python -I mdump.py the-crash.dmp
endlocal
