@echo off
setlocal
echo ========================================================
echo   ClusterShift - Dismount and Remove Virtual Lab
echo   Architect: Joive
echo ========================================================

set DISK_PATH=C:\test_storage.vhdx
set DISKPART_SCRIPT=%TEMP%\cs_remove_vhd.txt

echo select vdisk file="%DISK_PATH%" > "%DISKPART_SCRIPT%"
echo detach vdisk >> "%DISKPART_SCRIPT%"

echo [*] Dismounting virtual drive V:...
diskpart /s "%DISKPART_SCRIPT%"
del "%DISKPART_SCRIPT%"

if exist "%DISK_PATH%" (
    echo [*] Deleting virtual disk file %DISK_PATH%...
    del "%DISK_PATH%"
)

echo.
echo [+] Virtual drive V: has been safely removed. Clean state restored.
echo.
pause
