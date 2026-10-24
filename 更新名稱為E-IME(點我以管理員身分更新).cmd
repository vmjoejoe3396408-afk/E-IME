@echo off
chcp 65001 >nul
echo 正在準備更新 Windows 系統輸入法名稱...

:: 檢查是否具備管理員權限
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [提示] 正在請求系統管理員權限，請在彈出的 UAC 視窗點擊「是」以允許寫入系統登錄檔...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process cmd.exe -ArgumentList '/c \"\"%~f0\"\"' -Verb RunAs"
    exit /b
)

:: 已取得管理員權限
cd /d "%~dp0"
set "DLL_PATH=%LOCALAPPDATA%\E-IME\versions\ab4248f0349d23409049d33d75189548ee84d2a5d97b75b997cdafbe2d26ba36\eime_tip.dll"
if not exist "%DLL_PATH%" (
    if exist "%~dp0build\eime_tip.dll" set "DLL_PATH=%~dp0build\eime_tip.dll"
    if exist "%~dp0build_out\eime_tip.dll" set "DLL_PATH=%~dp0build_out\eime_tip.dll"
)

echo 目標 DLL: %DLL_PATH%
echo 正在執行 regsvr32 寫入 HKEY_LOCAL_MACHINE 系統名稱...
regsvr32.exe /s "%DLL_PATH%"

echo.
echo ==========================================================
echo   [完成] Windows 系統語言列名稱已正式更新！
echo   請查看右下角工作列，已變更為：
echo   「繁體中文 (台灣) - E-IME (改進型注音)」
echo ==========================================================
echo.
pause
