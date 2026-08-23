@echo off
setlocal enabledelayedexpansion

if not defined VSCMD_ARG_TGT_ARCH (
  set "VS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
  if not exist "!VS!" for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set "VS=%%i\VC\Auxiliary\Build\vcvars64.bat"
  if not exist "!VS!" echo [!] vcvars64.bat not found & exit /b 1
  call "!VS!" >nul
)

cd /d "%~dp0"
if not exist bin mkdir bin
if not exist bin\obj mkdir bin\obj

set SRC=src\main.cpp src\gfx\device.cpp src\gfx\renderer.cpp src\gfx\drawlist.cpp src\gfx\font.cpp src\gfx\image.cpp src\ui\ui.cpp src\ui\widgets.cpp src\app\app.cpp src\app\assets.cpp src\app\inject.cpp src\app\download.cpp src\app\manualmap.cpp src/prot/antidebug_junk.cpp src/prot/integrity_hash.cpp src/prot/string_crypt.cpp src/prot/control_flow.cpp src/prot/syscall_obf.cpp src/prot/vm_junk.cpp
set LIBS=d3d11.lib dxgi.lib d3dcompiler.lib dcomp.lib dwrite.lib windowscodecs.lib user32.lib gdi32.lib ole32.lib shell32.lib shlwapi.lib advapi32.lib winhttp.lib

cl /nologo /Ob2 /Oi /Ot /GL /arch:AVX2 /DOBFUSCATED /GS- /std:c++17 /EHsc /O2 /MT /W3 /wd4244 /wd4267 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src /Fobin\obj\ /Febin\swift.exe %SRC% /link /SUBSYSTEM:WINDOWS %LIBS%
if errorlevel 1 (echo. & echo [!] BUILD FAILED & exit /b 1)

echo. & echo [+] bin\swift.exe
