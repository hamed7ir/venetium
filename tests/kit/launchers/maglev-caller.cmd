@echo off
rem Venetium test kit launcher - configuration: maglev-caller (js-flags: --allow-natives-syntax --no-maglev-inlining)
rem Usage: double-click, or run with extra browser arguments, e.g.  maglev-caller.cmd --lang=de
rem venetium.exe: %VENETIUM% if set, else the folder above the kit. Profile: kit-profile-maglev-caller next to the kit.
setlocal EnableExtensions EnableDelayedExpansion
set "CONFIG=maglev-caller"
set "JSFLAGS=--allow-natives-syntax --no-maglev-inlining"
for %%I in ("%~dp0..") do set "KIT=%%~fI"
if defined VENETIUM (set "EXE=%VENETIUM%") else (for %%I in ("!KIT!\..\venetium.exe") do set "EXE=%%~fI")
if not exist "!EXE!" (
  echo venetium.exe not found at "!EXE!"
  echo Set VENETIUM to the full path of venetium.exe, or put the kit folder next to venetium.exe.
  exit /b 1
)
for %%I in ("!KIT!\..\kit-profile-%CONFIG%") do set "PROFILE=%%~fI"
set "URL=!KIT:\=/!"
set "URL=file:///!URL: =%%20!/index.html?config=%CONFIG%"
if defined JSFLAGS (
  "!EXE!" --user-data-dir="!PROFILE!" --js-flags="!JSFLAGS!" %* "!URL!"
) else (
  "!EXE!" --user-data-dir="!PROFILE!" %* "!URL!"
)
endlocal
