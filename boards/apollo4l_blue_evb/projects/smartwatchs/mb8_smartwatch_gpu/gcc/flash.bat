@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Usage: flash.bat [smoke|app]. Default is the GPU smoke test.
set "GPU_MODE=1"
if not "%~2"=="" goto usage
if "%~1"=="" goto mode_ready
if /I "%~1"=="smoke" goto mode_ready
if /I "%~1"=="app" goto app_mode
goto usage

:app_mode
set "GPU_MODE=0"

:mode_ready
pushd "%~dp0"
if errorlevel 1 exit /b 1

rem Preserve the original watch project's device, interface, speed and image origin.
if not defined JLINK_DEVICE set "JLINK_DEVICE=AMA4B2KP-KXR"
if defined JLINK_EXE goto check_jlink
for /f "delims=" %%I in ('where JLink.exe 2^>nul') do if not defined JLINK_EXE set "JLINK_EXE=%%I"
if not defined JLINK_EXE if exist "%USERPROFILE%\SEGGER\JLink_V812a\JLink.exe" set "JLINK_EXE=%USERPROFILE%\SEGGER\JLink_V812a\JLink.exe"
if not defined JLINK_EXE if exist "%ProgramFiles%\SEGGER\JLink\JLink.exe" set "JLINK_EXE=%ProgramFiles%\SEGGER\JLink\JLink.exe"
if not defined JLINK_EXE if exist "%ProgramFiles(x86)%\SEGGER\JLink\JLink.exe" set "JLINK_EXE=%ProgramFiles(x86)%\SEGGER\JLink\JLink.exe"
if not defined JLINK_EXE goto missing_jlink

:check_jlink
if not exist "%JLINK_EXE%" goto missing_jlink
if not exist "jlink_auto_file.jlink" goto missing_commands

echo Building GPU firmware: WATCH_GPU_SMOKE_TEST=%GPU_MODE%, CONFIG=bin
call make all GPU_SMOKE_TEST=%GPU_MODE% CONFIG=bin
if errorlevel 1 goto build_failed
if not exist "bin\mb8_smartwatch_gpu.bin" goto missing_binary

echo Flashing bin\mb8_smartwatch_gpu.bin at 0x00018000 via SWD.
echo J-Link device: %JLINK_DEVICE%
"%JLINK_EXE%" -device "%JLINK_DEVICE%" -if SWD -speed 4000 -NoGui 1 -ExitOnError 1 -CommandFile "jlink_auto_file.jlink"
set "FLASH_RC=%ERRORLEVEL%"
if not "%FLASH_RC%"=="0" echo ERROR: J-Link failed. Review the output above.
popd
exit /b %FLASH_RC%

:missing_jlink
echo ERROR: JLink.exe was not found. Set JLINK_EXE to its full executable path.
echo Example in Git Bash: export JLINK_EXE='C:\Users\Padidar\SEGGER\JLink_V812a\JLink.exe'
goto failed
:missing_commands
echo ERROR: jlink_auto_file.jlink is missing. Update the GPU project files together.
goto failed
:build_failed
echo ERROR: Build failed. Nothing was flashed.
goto failed
:missing_binary
echo ERROR: The build did not produce bin\mb8_smartwatch_gpu.bin. Nothing was flashed.
:failed
popd
exit /b 1

:usage
echo Usage: flash.bat [smoke^|app]
echo Default: build and flash the GPU smoke test. Use app for the watch menu and sensors.
exit /b 2
