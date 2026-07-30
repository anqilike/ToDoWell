@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (echo [build] vcvars not found & pause & exit /b 1)
echo [build] compiling resources...
rc.exe /nologo /fo app.res app.rc
if errorlevel 1 (echo [build] rc failed & pause & exit /b 1)
echo [build] compiling...
cl.exe /nologo /EHsc /O2 /MT /W3 /utf-8 ^
  /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_NTDDI_VERSION=0x06010000 /DUNICODE /D_UNICODE ^
  main.cpp app.cpp gfx.cpp storage.cpp json.cpp app.res ^
  /Fe:ToDoWell.exe ^
  /link d2d1.lib dwrite.lib user32.lib gdi32.lib advapi32.lib winmm.lib imm32.lib ^
  /SUBSYSTEM:WINDOWS /MANIFEST:NO
if errorlevel 1 (echo [build] FAILED & pause & exit /b 1)
del /q *.obj 2>nul
echo [build] OK - ToDoWell.exe
