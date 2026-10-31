make %*
if errorlevel 1 exit /b 1
C:\Users\Padidar\SEGGER\JLink_V812a\JLink.exe -device AMA4B2KP-KXR -if SWD -speed 4000 -NoGui 1 -CommandFile jlink_auto_file.jlink
