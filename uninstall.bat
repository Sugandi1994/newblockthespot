@echo off
echo *****************
echo Author: @rednek46
echo *****************
echo Removing Patch
if exist "%APPDATA%\Spotify\chrome_elf.dll.bak" (
    del /s /q "%APPDATA%\Spotify\chrome_elf.dll" > NUL 2>&1
    move "%APPDATA%\Spotify\chrome_elf.dll.bak" "%APPDATA%\Spotify\chrome_elf.dll" > NUL 2>&1
) else (
    echo done
)
pause
