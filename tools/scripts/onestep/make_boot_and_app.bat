@echo off
rem SPDX-License-Identifier: Apache-2.0
rem
rem Copyright (C) 2023-2024 ArtInChip Technology Co., Ltd

set clean_flag=%1
setlocal enabledelayedexpansion
if NOT exist %SDK_PRJ_TOP_DIR%\.config (
	echo Not lunch project yet
	goto exit_label
)

set /P defconfig=<%SDK_PRJ_TOP_DIR%\.defconfig
set defconfig=!defconfig:~0,-1!

for /f "tokens=1* delims=_" %%a in ("%defconfig%") do (
	set chip=%%a
)
for /f "tokens=2* delims=_" %%a in ("%defconfig%") do (
	set board=%%a
)
for /f "tokens=3* delims=_" %%a in ("%defconfig%") do (
	set kernel=%%a
)
for /f "tokens=4* delims=_" %%a in ("%defconfig%") do (
	set app=%%a
)

set app_defconfig=!defconfig!_defconfig
set boot_defconfig=!chip!_!board!_baremetal_bootloader_defconfig

set LOG_DIR=%SDK_PRJ_TOP_DIR%\.log
if not exist %LOG_DIR% mkdir %LOG_DIR%
del %LOG_DIR%\*.log /f /q 2>nul

:: (1) Build bootloader
if not exist "%SDK_PRJ_TOP_DIR%\target\configs\%boot_defconfig%" (
	echo %boot_defconfig% does not exist!
	goto build_app
)

set boot_solution=!boot_defconfig:~0,-10!
echo.
echo --------------------------------------------------------------
echo Build !boot_solution!
echo --------------------------------------------------------------
call scons --apply-def=%boot_defconfig% -C %SDK_PRJ_TOP_DIR%

if "%clean_flag%" == "clean" (
	call scons -c -C %SDK_PRJ_TOP_DIR%
)
call scons -C %SDK_PRJ_TOP_DIR%	 -j 8 2>&1 | tee %LOG_DIR%\%boot_defconfig%.log

if "%kernel%_%app%" == "baremetal_bootloader" (
	goto exit_label
)

:: Use findstr to check build result (always available on Windows)
findstr /c:"Luban-Lite is built successfully" /c:"is up to date" %LOG_DIR%\%boot_defconfig%.log > nul
if not %errorlevel% equ 0 (
	call scons --apply-def=%app_defconfig% -C %SDK_PRJ_TOP_DIR%
	goto exit_label
)

:build_app
:: (2) Build app
echo.
echo --------------------------------------------------------------
echo Build %defconfig%
echo --------------------------------------------------------------
call scons --apply-def=%app_defconfig% -C %SDK_PRJ_TOP_DIR%

if "%clean_flag%" == "clean" (
	call scons -c -C %SDK_PRJ_TOP_DIR%
)
call scons -C %SDK_PRJ_TOP_DIR% -j 8

:exit_label
endlocal
