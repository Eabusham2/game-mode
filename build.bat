@echo off
REM GameMode - native Win32 (C) build using the MSVC toolchain.
REM Run from a "Developer Command Prompt for VS" so cl.exe and rc.exe are on PATH.

setlocal
echo Compiling resources...
rc /nologo /fo resource.res resource.rc
if errorlevel 1 goto :fail

echo Compiling and linking...
cl /nologo /O2 /W4 /MT ^
   /DWINVER=0x0601 /D_WIN32_WINNT=0x0601 ^
   /Fe:GameMode.exe ^
   src\known_lists.c src\modes.c src\config.c src\engine.c src\gui.c ^
   resource.res ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO ^
   user32.lib gdi32.lib comctl32.lib advapi32.lib shell32.lib ole32.lib psapi.lib
if errorlevel 1 goto :fail

echo.
echo Build OK -^> GameMode.exe
del /q *.obj resource.res >nul 2>&1
goto :eof

:fail
echo.
echo BUILD FAILED.
exit /b 1
