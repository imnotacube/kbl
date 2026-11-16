@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install-assoc.ps1" -Exe "%~dp0build\kbl.exe"
pause
