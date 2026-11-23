@echo off
cd /d "%~dp0"
echo Release device space after verifying each local recording.
echo Your archived audio on this PC will be kept.
choice /M "Reclaim the verified recordings on the connected Passport"
if errorlevel 2 exit /b
call start.cmd --once --reclaim
