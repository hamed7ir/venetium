@echo off
rem Venetium diagnostics - run venetium.exe with Chrome's own logging, in a fresh test profile, and keep the exit code.
rem Each run gets its own folder: diag\logs\run-1, run-2, ...  (profile, chrome_debug.log, crash dumps, exit code).
rem Extra arguments are passed to Venetium, for example:
rem    log-run.cmd
rem    log-run.cmd --disable-gpu
rem Delayed expansion is deliberately OFF: with it on, a ! or ^ in the folder path is eaten and the run goes to the wrong place.
setlocal EnableExtensions DisableDelayedExpansion
set "DIAG=%~dp0"
rem venetium.exe: %VENETIUM% if set, else next to this script, else one folder up (diag inside the browser folder), else
rem two folders up (the zip extracted with an extra folder around diag)
set "EXE="
if defined VENETIUM if exist "%VENETIUM%" for %%I in ("%VENETIUM%") do set "EXE=%%~fI"
if not defined EXE if exist "%~dp0venetium.exe" for %%I in ("%~dp0venetium.exe") do set "EXE=%%~fI"
if not defined EXE if exist "%~dp0..\venetium.exe" for %%I in ("%~dp0..\venetium.exe") do set "EXE=%%~fI"
if not defined EXE if exist "%~dp0..\..\venetium.exe" for %%I in ("%~dp0..\..\venetium.exe") do set "EXE=%%~fI"
if defined EXE goto have_exe
echo venetium.exe was not found. This script looked in:
echo   "%~dp0"
for %%I in ("%~dp0..") do echo   "%%~fI"
for %%I in ("%~dp0..\..") do echo   "%%~fI"
echo Extract the zip first (do not start this from inside the zip), and put the diag folder inside the folder that holds
echo venetium.exe. Or set VENETIUM to the full path of venetium.exe, e.g.  set VENETIUM=C:\Venetium\venetium.exe
pause
exit /b 1
:have_exe
for %%I in ("%EXE%") do set "ROOT=%%~dpI"
echo Using "%EXE%"
if not exist "%DIAG%logs" mkdir "%DIAG%logs"
set /a N=1
:next
if exist "%DIAG%logs\run-%N%" set /a N+=1 & goto next
set "RUN=%DIAG%logs\run-%N%"
mkdir "%RUN%"
if exist "%RUN%" goto have_run
echo Cannot create the folder "%RUN%" - is this location read-only? Copy the whole folder to your Desktop and try again.
pause
exit /b 1
:have_run
rem keep an old debug.log (written next to venetium.exe by earlier runs) out of this run's results
if exist "%ROOT%debug.log" move /y "%ROOT%debug.log" "%RUN%\debug.log.before-run" >nul
> "%RUN%\run.txt" echo arguments: %*
>> "%RUN%\run.txt" echo started: %DATE% %TIME%
echo Run %N%: starting Venetium (close it, or wait for it to end). Logs: "%RUN%"
rem /D: Venetium's working directory is the run folder, so a debug.log it writes relative to it lands in this run's folder
start "" /D "%RUN%" /wait "%EXE%" --user-data-dir="%RUN%\profile" --enable-logging --v=1 --log-file="%RUN%\chrome_debug.log" %*
set "RC=%ERRORLEVEL%"
call :hex %RC%
>> "%RUN%\run.txt" echo ended: %DATE% %TIME%
>> "%RUN%\run.txt" echo exit code: %RC% (0x%HEX%)
if exist "%ROOT%debug.log" copy /y "%ROOT%debug.log" "%RUN%\debug.log" >nul
echo.
echo Run %N% ended with exit code %RC% (0x%HEX%). Its logs are in:
echo   "%RUN%"
pause
exit /b 0

:hex
rem %1 = signed decimal exit code; sets HEX to its 8-digit hex form (a crash shows up as e.g. -1073741819 = 0xC0000005)
setlocal EnableDelayedExpansion
set "H="
set "V=%1"
set "DIG=0123456789ABCDEF"
for %%S in (28 24 20 16 12 8 4 0) do (
  set /a "D=(V>>%%S)&15"
  for %%D in (!D!) do set "H=!H!!DIG:~%%D,1!"
)
endlocal & set "HEX=%H%"
exit /b 0
