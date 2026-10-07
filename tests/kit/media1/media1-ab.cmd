@echo off
rem Venetium MEDIA-1 A/B (BATCH MEDIA-1). Runs the GPU page (..\gpu\run.html: the kit's 720p H.264 High clip, then VP9 and AV1
rem clips, then a short compositor stall) five times, each with a different configuration, a fresh profile and its own chrome log
rem under kit\media1\logs\<config>\chrome_debug.log. Each run is given about 3.5 minutes and then closed; the whole sweep takes
rem about 18 minutes. Leave the tablet alone until it prints "all done".
rem
rem The first four runs keep the SANDBOX ON (no --no-sandbox): this build's sandbox fix (0038) and the hardware H.264 decoder
rem (0041, which must load inside the sandboxed GPU process) are only tested that way. The last run is the no-sandbox fallback.
rem   default        everything on (hardware H.264 0041, GPU YUV for VP9/AV1 0042, GPU raster 0039)
rem   no-hw-decode   --disable-accelerated-video-decode                        (0041 off: software H.264)
rem   no-gpu-yuv     --disable-features=VenetiumSoftwareVideoGpuYuv            (0042 off: CPU YUV->RGB for VP9/AV1)
rem   no-gpu-raster  --disable-gpu-rasterization                               (0039 off: software raster)
rem   no-sandbox     --no-sandbox                                              (everything on, sandbox off)
rem
rem IMPORTANT: this closes every Venetium window between runs (taskkill /im venetium.exe). Do not run other Venetium windows while
rem this is working. Extract the zip to a folder whose path has no "!" or "^" in it.
setlocal EnableExtensions EnableDelayedExpansion
set "M1=%~dp0"
rem --- find venetium.exe: %VENETIUM%, else one folder up from kit\media1, else two up ---
set "EXE="
if defined VENETIUM if exist "%VENETIUM%" for %%I in ("%VENETIUM%") do set "EXE=%%~fI"
if not defined EXE for %%I in ("%M1%..\..\venetium.exe") do if exist "%%~fI" set "EXE=%%~fI"
if not defined EXE for %%I in ("%M1%..\..\..\venetium.exe") do if exist "%%~fI" set "EXE=%%~fI"
if defined EXE goto have_exe
echo venetium.exe was not found. This script looked one and two folders above kit\media1.
echo Put the kit inside the folder that holds venetium.exe, or set VENETIUM to its full path.
pause
exit /b 1
:have_exe
set "LOGS=%M1%logs"
if not exist "%LOGS%" md "%LOGS%"
rem build the ..\gpu\run.html file:// URL (escape backslashes and spaces)
for %%I in ("%M1%..\gpu\run.html") do set "U=%%~fI"
set "U=!U:\=/!"
set "U=file:///!U: =%%20!"
echo Using Venetium: "!EXE!"
echo Logs under:     "!LOGS!"
echo.
call :run default        ""
call :run no-hw-decode   "--disable-accelerated-video-decode"
call :run no-gpu-yuv     "--disable-features=VenetiumSoftwareVideoGpuYuv"
call :run no-gpu-raster  "--disable-gpu-rasterization"
call :run no-sandbox     "--no-sandbox"
echo.
echo all done
echo Zip the whole kit\media1 folder (it now has logs\*) and send it.
pause
endlocal
exit /b 0

:run
set "NAME=%~1"
set "EXTRA=%~2"
set "CDIR=!LOGS!\!NAME!"
if exist "!CDIR!" rd /s /q "!CDIR!"
md "!CDIR!"
echo [!NAME!] starting (about 3.5 minutes) ...
start "" "!EXE!" --user-data-dir="!CDIR!\profile" --enable-logging --v=1 --log-file="!CDIR!\chrome_debug.log" --no-first-run --no-default-browser-check !EXTRA! "!U!?config=!NAME!&video-seconds=45&stall-seconds=20"
rem three clips x 45 s + a 20 s stall + loading fit inside 210 s; then close this run and start the next
timeout /t 210 /nobreak >nul
taskkill /f /im venetium.exe >nul 2>&1
rem let the processes release the profile before the next run
timeout /t 3 /nobreak >nul
echo [!NAME!] done - log in "!CDIR!\chrome_debug.log"
goto :eof
