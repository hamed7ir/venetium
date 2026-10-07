@echo off
rem Venetium diagnostics - run venetium.exe under the debugger launcher (dbgrun.exe) so a fatal crash is captured even when
rem Windows writes no dump. Each run gets its own folder diag\dbglogs\run-N with: dbgrun.txt (the debugger log), crash-N.txt
rem and crash-N.dmp (if it crashes), stderr.txt (the browser's stderr), chrome_debug.log, and a fresh profile.
rem Extra arguments are passed to Venetium, e.g.:
rem    dbg-run.cmd
rem    dbg-run.cmd --no-sandbox
rem    dbg-run.cmd --disable-features=SegmentationPlatformDeviceSwitcher
rem Delayed expansion is deliberately OFF (a ! or ^ in the path would be eaten with it on).
setlocal EnableExtensions DisableDelayedExpansion
set "DIAG=%~dp0"
if not exist "%DIAG%dbgrun.exe" (
  echo dbgrun.exe was not found next to this script: "%DIAG%"
  echo Extract the whole zip first, then try again.
  pause
  exit /b 1
)
rem venetium.exe: %VENETIUM% if set, else next to this script, else one folder up, else two folders up
set "EXE="
if defined VENETIUM if exist "%VENETIUM%" for %%I in ("%VENETIUM%") do set "EXE=%%~fI"
if not defined EXE if exist "%DIAG%venetium.exe" for %%I in ("%DIAG%venetium.exe") do set "EXE=%%~fI"
if not defined EXE if exist "%DIAG%..\venetium.exe" for %%I in ("%DIAG%..\venetium.exe") do set "EXE=%%~fI"
if not defined EXE if exist "%DIAG%..\..\venetium.exe" for %%I in ("%DIAG%..\..\venetium.exe") do set "EXE=%%~fI"
if defined EXE goto have_exe
echo venetium.exe was not found. This script looked in:
echo   "%DIAG%"
for %%I in ("%DIAG%..") do echo   "%%~fI"
for %%I in ("%DIAG%..\..") do echo   "%%~fI"
echo Put the diag folder inside the folder that holds venetium.exe, or set VENETIUM to its full path.
pause
exit /b 1
:have_exe
if not exist "%DIAG%dbglogs" mkdir "%DIAG%dbglogs"
set /a N=1
:next
if exist "%DIAG%dbglogs\run-%N%" set /a N+=1 & goto next
set "RUN=%DIAG%dbglogs\run-%N%"
mkdir "%RUN%"
echo Run %N%: starting Venetium under the debugger. Close it, or wait for it to end. Logs: "%RUN%"
"%DIAG%dbgrun.exe" "%RUN%" "%EXE%" --user-data-dir="%RUN%\profile" --enable-logging --v=1 --log-file="%RUN%\chrome_debug.log" %* --dbgrun-stderr
set "RC=%ERRORLEVEL%"
echo.
echo Run %N% ended (browser exit code %RC%). If it crashed, "%RUN%" has crash-1.txt and crash-1.dmp.
echo Send the whole "%RUN%" folder.
pause
exit /b 0
