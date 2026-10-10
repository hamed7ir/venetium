@echo off
rem Undo enable-wer-localdumps.cmd. Run elevated on the device.
set KEY=HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\venetium.exe
reg delete "%KEY%" /f
echo WER LocalDumps for venetium.exe removed. Collected dumps in %LOCALAPPDATA%\VenetiumCrashDumps are kept.
