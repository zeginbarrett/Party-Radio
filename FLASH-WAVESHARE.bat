@echo off
setlocal
cd /d "%~dp0"
rem Prefer the already-installed Python 3.13, independent of the default Python 3.15.
for /f "delims=" %%P in ('py -3.13 -c "import sys; print(sys.executable)" 2^>nul') do set "RADIOVAULT_PYTHON=%%P"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\flash.ps1"
if errorlevel 1 (
  echo.
  echo ERROR: Installation, compilation or upload failed. Read the messages above.
)
echo.
pause
