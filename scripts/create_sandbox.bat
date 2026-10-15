@echo off
setlocal
echo ========================================================
echo   ClusterShift - Mount Virtual Testing Lab (VHDX)
echo   Architect: Joive
echo ========================================================

set DISK_PATH=C:\test_storage.vhdx
set DISKPART_SCRIPT=%TEMP%\cs_create_vhd.txt

echo create vdisk file="%DISK_PATH%" maximum=2048 type=expandable > "%DISKPART_SCRIPT%"
echo select vdisk file="%DISK_PATH%" >> "%DISKPART_SCRIPT%"
echo attach vdisk >> "%DISKPART_SCRIPT%"
echo create partition primary >> "%DISKPART_SCRIPT%"
echo format fs=ntfs quick label="ClusterShiftLab" >> "%DISKPART_SCRIPT%"
echo assign letter=V >> "%DISKPART_SCRIPT%"

echo [*] Creating and mounting 2GB virtual drive V: via DiskPart...
diskpart /s "%DISKPART_SCRIPT%"
del "%DISKPART_SCRIPT%"

echo.
echo [+] Virtual drive V: is ready!
echo [+] You can now run:
echo       .\clustershift.exe info V
echo       .\clustershift.exe map V
echo.
pause
