@echo off
rem Venetium diagnostics - sandboxed chrome.dll load probe (BATCH-SBX-1 section 3).
rem Reproduces how the renderer tries to load chrome.dll under the sandbox's restricted token, and reports whether that token can
rem read chrome.dll and find its dependencies - the measurement that says WHY the sandboxed renderer shows 0xC0000135 while the GPU
rem process loads fine. It changes nothing on the machine and needs no admin. Writes dllprobe-<date>-<time>.txt into this folder.
setlocal EnableExtensions DisableDelayedExpansion
set "HERE=%~dp0"
if exist "%HERE%dllprobe.exe" goto have_exe
echo dllprobe.exe was not found next to this script: "%HERE%"
echo Extract the whole zip first (do not start this from inside the zip), then try again.
pause
exit /b 1
:have_exe
rem find chrome.dll: next to venetium.exe (one or two folders up from kit\diag), or %VENETIUM_DLL%
set "DLL="
if defined VENETIUM_DLL if exist "%VENETIUM_DLL%" for %%I in ("%VENETIUM_DLL%") do set "DLL=%%~fI"
if not defined DLL if exist "%HERE%..\..\chrome.dll" for %%I in ("%HERE%..\..\chrome.dll") do set "DLL=%%~fI"
if not defined DLL if exist "%HERE%..\chrome.dll" for %%I in ("%HERE%..\chrome.dll") do set "DLL=%%~fI"
echo Running the sandboxed-load probe - this takes a few seconds.
echo.
if defined DLL ("%HERE%dllprobe.exe" --dll "%DLL%") else ("%HERE%dllprobe.exe")
set "RC=%ERRORLEVEL%"
echo.
echo done  (exit code %RC%)
echo Send the dllprobe-*.txt file from this folder:
echo   "%HERE%"
pause
endlocal
