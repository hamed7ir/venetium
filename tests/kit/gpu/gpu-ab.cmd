@echo off
rem Venetium GPU A/B (BATCH-GPU-1 §7.21). Runs the GPU page (run.html: the 720p-video page, then the 60 s compositor stall page)
rem five times in a row, each with a different GPU configuration, its own fresh profile, and its own chrome log under
rem kit\gpu\logs\<config>\chrome_debug.log. Each run is given up to 5 minutes and is then closed so the next begins; the whole
rem sweep takes about 25 minutes. Leave the tablet alone until it prints "all done".
rem
rem IMPORTANT: this closes every Venetium window between runs (taskkill /im venetium.exe). Do not run other Venetium windows while
rem this is working. Extract the zip to a folder whose path has no "!" or "^" in it.
setlocal EnableExtensions EnableDelayedExpansion
set "GPU=%~dp0"
rem --- find venetium.exe: %VENETIUM%, else one folder up from kit\gpu, else two up ---
set "EXE="
if defined VENETIUM if exist "%VENETIUM%" for %%I in ("%VENETIUM%") do set "EXE=%%~fI"
if not defined EXE for %%I in ("%GPU%..\..\venetium.exe") do if exist "%%~fI" set "EXE=%%~fI"
if not defined EXE for %%I in ("%GPU%..\..\..\venetium.exe") do if exist "%%~fI" set "EXE=%%~fI"
if defined EXE goto have_exe
echo venetium.exe was not found. This script looked one and two folders above kit\gpu.
echo Put the kit inside the folder that holds venetium.exe, or set VENETIUM to its full path.
pause
exit /b 1
:have_exe
set "LOGS=%GPU%logs"
if not exist "%LOGS%" md "%LOGS%"
rem build the run.html file:// URL (escape backslashes and spaces)
set "U=%GPU%run.html"
set "U=!U:\=/!"
set "U=file:///!U: =%%20!"
echo Using Venetium: "!EXE!"
echo Logs under:     "!LOGS!"
echo.
call :run default        ""
call :run d3d11          "--use-angle=d3d11"
call :run d3d9           "--use-angle=d3d9"
call :run no-compositing "--disable-gpu-compositing"
call :run strict-es2     "--venetium-strict-es2-limits"
echo.
echo all done
echo Zip the whole kit\gpu folder (it now has logs\*) and send it.
pause
endlocal
exit /b 0

:run
set "NAME=%~1"
set "EXTRA=%~2"
set "CDIR=!LOGS!\!NAME!"
if exist "!CDIR!" rd /s /q "!CDIR!"
md "!CDIR!"
echo [!NAME!] starting (up to 5 minutes) ...
start "" "!EXE!" --user-data-dir="!CDIR!\profile" --no-sandbox --enable-logging --v=1 --log-file="!CDIR!\chrome_debug.log" --no-first-run --no-default-browser-check !EXTRA! "!U!?config=default&video-seconds=30&stall-seconds=60"
rem give the run up to 5 minutes; the pages finish well inside that, then we close this run and start the next
timeout /t 300 /nobreak >nul
taskkill /f /im venetium.exe >nul 2>&1
rem let the processes release the profile before the next run
timeout /t 3 /nobreak >nul
echo [!NAME!] done - log in "!CDIR!\chrome_debug.log"
goto :eof
