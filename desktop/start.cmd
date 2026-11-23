@echo off
setlocal
cd /d "%~dp0"
if exist "..\bin\PassportExport.exe" (
  "..\bin\PassportExport.exe" --settings "%~dp0settings.json" %*
  if errorlevel 1 pause
  exit /b
)
if not exist ".venv\Scripts\python.exe" (
  where py >nul 2>nul && (py -3 -m venv .venv) || (python -m venv .venv)
  if errorlevel 1 goto failed
)
".venv\Scripts\python.exe" -c "import av, serial" >nul 2>nul
if errorlevel 1 ".venv\Scripts\python.exe" -m pip install -r requirements.txt
if errorlevel 1 goto failed
".venv\Scripts\python.exe" exporter.py %*
if errorlevel 1 goto failed
exit /b
:failed
echo Export failed. Check the message above.
pause
exit /b 1
