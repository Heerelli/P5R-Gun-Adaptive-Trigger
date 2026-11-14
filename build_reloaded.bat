@echo off
rem Run from "x64 Native Tools Command Prompt for VS 2022".
rem Produces a Reloaded-II mod folder: out\reloaded\p5rpc.gun.adaptivetrigger\
set MODDIR=out\reloaded\p5rpc.gun.adaptivetrigger
if not exist %MODDIR% mkdir %MODDIR%
if not exist out\obj mkdir out\obj
cl /nologo /O2 /MT /W4 /EHsc /LD /std:c++17 /DUNICODE /D_UNICODE src\dllmain.cpp src\inputhook.cpp src\haptics.cpp ^
   /Fe:%MODDIR%\P5RGunTrigger.dll /Fo:out\obj\ /link /MACHINE:X64 setupapi.lib hid.lib user32.lib ole32.lib
if errorlevel 1 exit /b 1
copy /y reloaded\ModConfig.json %MODDIR%\ModConfig.json >nul
copy /y P5RGunTrigger.ini %MODDIR%\P5RGunTrigger.ini >nul
del /q %MODDIR%\*.lib %MODDIR%\*.exp 2>nul
echo.
echo Built %MODDIR%
echo Copy that folder into Reloaded-II\Mods\ (or zip it and drag it onto Reloaded-II).
