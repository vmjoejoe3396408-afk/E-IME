@echo off
chcp 65001 >nul
where pwsh.exe >nul 2>&1
if errorlevel 1 (
    echo [E-IME] PowerShell 7 is required. Install it and run this script again.
    pause
    exit /b 1
)

:: Use the checked installer; it resolves the DLL and requests UAC if needed.
pwsh.exe -NoProfile -File "%~dp0scripts\install.ps1" %*
set "EIME_RESULT=%ERRORLEVEL%"
if not "%EIME_RESULT%"=="0" (
    echo [E-IME] Registration failed. See the error above.
) else (
    echo [E-IME] Registration updated. Restart applications to load this version.
)
pause
exit /b %EIME_RESULT%
