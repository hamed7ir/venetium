@echo off
rem Venetium diagnostics - sandbox process-creation probe.
rem Runs sbxprobe.exe, which starts copies of itself with each part of Chromium's sandboxed CreateProcess call and logs which
rem part Windows refuses. It changes nothing on the machine. Writes sbxprobe-DATE-TIME.log into this folder.
setlocal
set "HERE=%~dp0"
if exist "%HERE%sbxprobe.exe" goto have_exe
echo sbxprobe.exe was not found next to this script: "%HERE%"
echo Extract the whole zip first (do not start this from inside the zip), then try again.
pause
exit /b 1
:have_exe
echo Running the sandbox probe - this takes from a few seconds to a few minutes.
echo Do not tap or click inside this window while it runs (that pauses it); if it seems frozen, press Esc once.
echo.
"%HERE%sbxprobe.exe"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
  echo.
  echo sbxprobe.exe ended abnormally, exit code %RC%. Send whatever sbxprobe-*.log exists in the folder below, and say what you saw.
)
echo.
echo Done. Send the sbxprobe-*.log file from this folder:
echo   "%HERE%"
pause
