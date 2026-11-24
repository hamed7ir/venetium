@echo off
rem ============================================================================
rem  Venetium GPU probe - step 2 of 2
rem  Opens Venetium four times, each with different GPU settings and a fresh
rem  test profile (your own profile is not touched), and saves Venetium's log
rem  and its chrome://gpu report for each run.
rem ============================================================================
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
set "HERE=%~dp0"

net session >nul 2>&1
if not errorlevel 1 (
  echo This window runs as Administrator. Venetium restarts itself as a normal user
  echo then, and the log would be lost. Close this window and double-click
  echo render-check.cmd normally ^(not "Run as administrator"^).
  pause & exit /b 1
)

set "EXE="
if defined VENETIUM if exist "%VENETIUM%" set "EXE=%VENETIUM%"
if not defined EXE if exist "%HERE%venetium.exe" set "EXE=%HERE%venetium.exe"
if not defined EXE if exist "%HERE%..\venetium.exe" set "EXE=%HERE%..\venetium.exe"
if not defined EXE if exist "%HERE%..\..\venetium.exe" set "EXE=%HERE%..\..\venetium.exe"
if not defined EXE if exist "%LOCALAPPDATA%\Programs\Venetium\venetium.exe" set "EXE=%LOCALAPPDATA%\Programs\Venetium\venetium.exe"
if not defined EXE (
  echo Could not find venetium.exe.
  echo Drag venetium.exe onto this window and press Enter:
  set /p "EXE="
)
if not defined EXE (
  echo No path given. Run this again.
  pause & exit /b 1
)
set "EXE=%EXE:"=%"
if not exist "%EXE%" (
  echo "%EXE%" does not exist. Run this again.
  pause & exit /b 1
)
echo Using: %EXE%
if not exist "results" mkdir "results"
echo(%EXE%> "results\venetium-path.txt"

call :run 1 default ""
call :run 2 ignore-blocklist "--ignore-gpu-blocklist"
call :run 3 angle-d3d11 "--use-angle=d3d11 --ignore-gpu-blocklist"
call :run 4 angle-d3d9 "--use-angle=d3d9 --ignore-gpu-blocklist"

echo.
echo ============================================================
echo  ALL DONE. Please zip the whole "results" folder:
echo     %HERE%results
echo  and send the zip. Thank you!
echo ============================================================
pause
endlocal
exit /b 0

:run
set "N=%~1"
set "NAME=%~2"
set "FLAGS=%~3"
set "OUT=%HERE%results\run%N%-%NAME%"
set "PROF=%HERE%profiles\%NAME%"
if not exist "%OUT%" mkdir "%OUT%"
gpuprobe.exe --clear-clipboard >nul 2>&1
echo.
echo ============================================================
echo  Run %N% of 4: %NAME%   %FLAGS%
echo  Venetium opens with two tabs: chrome://gpu and a WebGL fish tank.
echo   1. Look at the fish tank tab: smooth, slow, black, or garbled?
echo   2. Go to the chrome://gpu tab and click "Copy Report to Clipboard"
echo      (a button near the top of the page).
echo   3. Close Venetium. This script continues by itself.
echo ============================================================
echo(%FLAGS%> "%OUT%\flags.txt"
start "" /wait "%EXE%" --user-data-dir="%PROF%" --no-first-run --no-default-browser-check --enable-logging --v=1 %FLAGS% "chrome://gpu" "https://webglsamples.org/aquarium/aquarium.html"
copy /y "%PROF%\chrome_debug.log" "%OUT%\chrome_debug.log" >nul 2>&1
gpuprobe.exe --save-clipboard "%OUT%\gpu-report.txt"
if errorlevel 1 echo   (no chrome://gpu report was on the clipboard for this run - that is OK, the log is saved)
set "NOTE="
set /p "NOTE=How did the fish tank look in run %N%? (smooth / slow / black / garbled / crashed / other): "
rem delayed expansion: characters such as & or > in the answer are written as text, not run
setlocal EnableDelayedExpansion
> "!OUT!\what-i-saw.txt" echo(!NOTE!
endlocal
exit /b 0
