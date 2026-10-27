@echo off
setlocal
cd /d "%~dp0" || goto failed

if exist ".venv\Scripts\python.exe" (
    ".venv\Scripts\python.exe" "main.py"
    goto finished
)

where py >nul 2>nul
if not errorlevel 1 (
    py -3 -c "import sys" >nul 2>nul
    if not errorlevel 1 (
        py -3 "main.py"
        goto finished
    )
)

where python >nul 2>nul
if not errorlevel 1 (
    python "main.py"
    goto finished
)

echo Python 3 was not found. Please install Python 3 first.
goto failed

:finished
if errorlevel 1 goto failed
exit /b 0

:failed
echo.
echo Startup failed. Install dependencies with:
echo python -m pip install -r requirements.txt
pause
exit /b 1
