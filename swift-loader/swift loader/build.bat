@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "bin\obj" mkdir "bin\obj"
del /f /q "bin\obj\*" >nul 2>&1
if exist "bin\swift.exe" move /y "bin\swift.exe" "bin\swift_old.exe" >nul 2>&1
del /f /q "bin\swift_old.exe" >nul 2>&1
cl /std:c++17 /EHsc /MD /O2 /DNOMINMAX /I"src" src/main.cpp src/app/*.cpp src/gfx/*.cpp src/ui/*.cpp src/prot/*.cpp src/loader/crypto/*.cpp /Fe:bin/swift.exe /Fobin\obj\ user32.lib gdi32.lib d3d11.lib d3dcompiler.lib winhttp.lib advapi32.lib shlwapi.lib ole32.lib shell32.lib bcrypt.lib crypt32.lib




