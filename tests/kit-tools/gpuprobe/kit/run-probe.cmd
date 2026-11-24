@echo off
rem ============================================================================
rem  Venetium GPU probe - step 1 of 2
rem  Reads what your GPU and its driver offer, so hardware acceleration can be
rem  fixed for your device. It only reads and draws off-screen; it changes
rem  nothing and installs nothing. Takes about one to three minutes.
rem ============================================================================
setlocal EnableExtensions
cd /d "%~dp0"
if not exist "gpuprobe.exe" (
  echo gpuprobe.exe is missing - extract the whole zip first, then run this again.
  pause & exit /b 1
)
if not exist "results" mkdir "results"
echo Running the GPU probe. Please wait - each part can take up to a minute.
echo.
gpuprobe.exe "%~dp0results"
echo.
echo ============================================================
echo  Step 1 done: %~dp0results\gpuprobe-report.txt
echo  Now run render-check.cmd (step 2).
echo ============================================================
pause
endlocal
