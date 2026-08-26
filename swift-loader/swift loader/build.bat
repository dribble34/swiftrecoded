@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "bin\obj" mkdir "bin\obj"
del /f /q "bin\obj\*" >nul 2>&1
if exist "bin\swift.exe" move /y "bin\swift.exe" "bin\swift_old.exe" >nul 2>&1
del /f /q "bin\swift_old.exe" >nul 2>&1

set "VMP_INC=C:\Users\ar0wz\Downloads\DEBUG PROTECTION\DEBUG PROTECTION\VMProtect Collection\VMProtect Ultimate 3.9.4\VMProtect Ultimate 3.94\Include\C"
set "VMP_LIB=C:\Users\ar0wz\Downloads\DEBUG PROTECTION\DEBUG PROTECTION\VMProtect Collection\VMProtect Ultimate 3.9.4\VMProtect Ultimate 3.94\Lib\Windows"

cl /std:c++17 /EHsc /MD /O2 /Zi /FS /DNOMINMAX ^
   /I"%VMP_INC%" /I"src" /I"src/loader" ^
   src/main.cpp ^
   src/app/*.cpp ^
   src/gfx/*.cpp ^
   src/ui/*.cpp ^
   src/prot/*.cpp ^
   src/loader/network/network.cpp ^
   src/loader/crypto/*.cpp ^
   src/loader/protection/*.cpp ^
   src/loader/steam/*.cpp ^
   src/loader/mapper/*.cpp ^
   /Fe:bin/swift.exe /Fobin\obj\ ^
   user32.lib gdi32.lib d3d11.lib d3dcompiler.lib winhttp.lib advapi32.lib shlwapi.lib ole32.lib shell32.lib bcrypt.lib crypt32.lib "%VMP_LIB%\VMProtectSDK64.lib" ^
   /link /DEBUG /MAP:bin/swift.map
