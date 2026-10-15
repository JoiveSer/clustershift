@echo off
setlocal
echo ========================================================
echo   ClusterShift v0.6.0 - 1-Click Dual Release Packager
echo   Architect: Joive
echo ========================================================

cd /d "%~dp0\.."

set DIST_DIR=dist\ClusterShift-v0.6-by-Joive
if exist "dist" rd /s /q "dist"
mkdir "%DIST_DIR%"

echo [*] Copying release artifacts...
copy "clustershift.exe" "%DIST_DIR%\"
copy "clustershift-gui.exe" "%DIST_DIR%\"
copy "README.md" "%DIST_DIR%\"
copy "LICENSE" "%DIST_DIR%\"

powershell -Command "Compress-Archive -Path '%DIST_DIR%' -DestinationPath 'ClusterShift-v0.6-by-Joive.zip' -Force"

echo.
echo [+] Release package created successfully:
echo     ClusterShift-v0.6-by-Joive.zip
echo.
echo [+] Included binaries:
echo     - clustershift.exe (Headless CLI edition)
echo     - clustershift-gui.exe (DirectX 11 + ImGui GUI edition)
echo.
echo [+] Ready for GitHub Releases upload!
