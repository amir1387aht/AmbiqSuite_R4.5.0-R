@echo off
rem Keep profiling enabled in flash.bat's nested make invocation.
rem This uses the existing build-failure gate, image, device and J-Link settings.
setlocal
pushd "%~dp0" || exit /b 1
set "GPU_PROFILE=1"
echo Profiling requested: startup must report profile=1.
call flash.bat
set "FLASH_RESULT=%errorlevel%"
popd
exit /b %FLASH_RESULT%
