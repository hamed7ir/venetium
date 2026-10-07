@echo off
rem Venetium diagnostics - Direct3D capability probe (BATCH-GPU-1 §7.18).
rem Runs d3dprobe.exe, which asks this machine's own drivers what they support: the DXGI adapter, every Direct3D 11 feature level
rem it can create, hardware video-decode support, and - the ones that matter for the Surface 2 - the Direct3D 9 shader-model 2
rem caps (ps_2_0 / ps_2_a / ps_2_b slot counts) and whether the driver actually compiles and accepts a ~330-slot ps_2_a shader and
rem a level_9_3 shader. It changes nothing on the machine and needs no network. Writes d3dprobe-DATE-TIME.txt into this folder.
setlocal EnableExtensions DisableDelayedExpansion
set "HERE=%~dp0"
if exist "%HERE%d3dprobe.exe" goto have_exe
echo d3dprobe.exe was not found next to this script: "%HERE%"
echo Extract the whole zip first (do not start this from inside the zip), then try again.
pause
exit /b 1
:have_exe
rem a filename-safe timestamp (locale-independent via WMIC when present, else a fixed name)
set "STAMP="
for /f "usebackq skip=1 tokens=1" %%I in (`wmic os get localdatetime 2^>nul`) do if not defined STAMP set "STAMP=%%I"
if defined STAMP (set "OUT=%HERE%d3dprobe-%STAMP:~0,8%-%STAMP:~8,6%.txt") else (set "OUT=%HERE%d3dprobe-output.txt")
echo Running the Direct3D probe - this takes a few seconds.
echo.
"%HERE%d3dprobe.exe" > "%OUT%" 2>&1
set "RC=%ERRORLEVEL%"
echo Exit code %RC%.
if not "%RC%"=="0" (
  echo.
  echo d3dprobe.exe ended with a non-zero exit code; the output file below still holds whatever it printed before stopping.
)
echo.
echo done
echo Send this file:
echo   "%OUT%"
pause
endlocal
