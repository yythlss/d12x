@echo off
setlocal enabledelayedexpansion
title AiBurn Flasher

rem ============================================================
rem  AiBurn command-line flasher (demo68-nor)
rem  - auto-picks the newest .img in the images dir
rem  - no need to edit this file when version changes
rem  - close AiBurn GUI first (it holds the COM port)
rem ============================================================

rem ---------- Config ----------
set "UPGCMD=D:\Project\claude\RTthread\AiBurn-1.5.3_Setup\AiBurn\upgcmd.exe"
set "IMAGES=D:\Project\claude\RTthread\luban-lite\output\d12x_demo68-nor_rt-thread_helloworld\images"
set "DEFAULT_COM=COM7"
set "BAUD=115200"
rem -----------------------------

if not exist "%UPGCMD%" (
    echo [ERROR] upgcmd not found: %UPGCMD%
    pause
    exit /b 1
)

if not exist "%IMAGES%" (
    echo [ERROR] images dir not found: %IMAGES%
    pause
    exit /b 1
)

rem ---------- Pick newest .img (newest modification time first) ----------
set "NEWEST="
for /f "delims=" %%f in ('dir /b /o-d "%IMAGES%\*.img"') do (
    if not defined NEWEST set "NEWEST=%IMAGES%\%%f"
)

if not defined NEWEST (
    echo [ERROR] no .img found in %IMAGES%
    pause
    exit /b 1
)

rem ---------- Choose COM port ----------
set "COM=%DEFAULT_COM%"
set /p "INP=COM port [Enter for %DEFAULT_COM%]: "
if /i not "%INP%"=="" set "COM=%INP%"

cls
echo ============================================================
echo   Image : %NEWEST%
echo   COM   : %COM%
echo   Baud  : %BAUD%
echo ============================================================
echo.
echo  Make sure:
echo   1. board is in download mode (hold UBOOT key on power up)
echo   2. AiBurn GUI is closed
echo.
pause

echo.
echo Flashing... (Got ACK means normal)
echo.
"%UPGCMD%" -u %COM% -b %BAUD% -p image "%NEWEST%"
set RET=%ERRORLEVEL%

echo.
if %RET%==0 (
    echo ============================================================
    echo   DONE. Reset the board to boot.
    echo ============================================================
) else (
    echo ============================================================
    echo   [WARN] flash did not exit 0 (code %RET%).
    echo   Re-enter download mode and rerun if needed.
    echo ============================================================
)
echo.
pause
endlocal