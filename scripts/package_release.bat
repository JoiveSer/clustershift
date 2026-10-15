@echo off
setlocal
echo ========================================================
echo   ClusterShift v0.4 - 1-Click Release Packager
echo   Architect: Joive
echo ========================================================

cd /d "%~dp0\.."

if not exist "clustershift.exe" (
    echo [*] Compiling release binary...
    call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    cl /std:c++latest /EHsc /O2 /DUNICODE /D_UNICODE /W4 /utf-8 /Iinclude src\core\*.cpp src\main.cpp /Fe:clustershift.exe /link advapi32.lib shell32.lib
    del *.obj
)

set DIST_DIR=dist\ClusterShift-v0.4-by-Joive
if exist "dist" rd /s /q "dist"
mkdir "%DIST_DIR%"

echo [*] Copying release artifacts...
copy "clustershift.exe" "%DIST_DIR%\"
copy "README.md" "%DIST_DIR%\"
copy "LICENSE" "%DIST_DIR%\"

powershell -Command "Compress-Archive -Path '%DIST_DIR%' -DestinationPath 'ClusterShift-v0.4-by-Joive.zip' -Force"

echo.
echo [+] Release package created successfully:
echo     ClusterShift-v0.4-by-Joive.zip
echo.
echo [+] Ready for GitHub Releases upload!
