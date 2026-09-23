@echo off
rem 在 “x64 Native Tools Command Prompt for VS” 之类的开发者命令行中运行
rem 用法: build.bat          编译 x64
rem       build.bat x86      编译 x86

setlocal
set ARCH=%1
if "%ARCH%"=="" set ARCH=x64

if not exist build mkdir build

cl /nologo /c /O2 /MT /GL /W3 /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Fobuild\ src\main.c src\ports.c src\proc.c src\ui.c
if errorlevel 1 goto :fail

rc /nologo /I src /fobuild\app.res src\app.rc
if errorlevel 1 goto :fail

link /nologo /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS ^
     /OUT:build\PortView.exe ^
     build\main.obj build\ports.obj build\proc.obj build\ui.obj build\app.res ^
     user32.lib gdi32.lib comctl32.lib shell32.lib advapi32.lib iphlpapi.lib ws2_32.lib psapi.lib uxtheme.lib
if errorlevel 1 goto :fail

echo.
echo Build OK: build\PortView.exe
endlocal
exit /b 0

:fail
echo.
echo Build FAILED
endlocal
exit /b 1
