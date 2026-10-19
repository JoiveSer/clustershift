@echo off
setlocal
echo ========================================================
echo   ClusterShift v0.6.0 - Dedicated GUI Builder & Packager
echo   Architect: Joive
echo ========================================================

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if %errorlevel% neq 0 (
    echo [-] Failed to initialize MSVC environment.
    exit /b %errorlevel%
)

cd /d "%~dp0\.."

echo [*] Compiling DirectX 11 + Dear ImGui Windows XP Shell...
cl /std:c++latest /EHsc /O2 /DUNICODE /D_UNICODE /utf-8 /Iinclude /Ithird_party/imgui /Ithird_party/imgui/backends src/core/*.cpp third_party/imgui/imgui*.cpp third_party/imgui/backends/imgui_impl_win32.cpp third_party/imgui/backends/imgui_impl_dx11.cpp src/gui/gui_main.cpp /Fe:clustershift-gui.exe /link /subsystem:windows d3d11.lib dxgi.lib dwmapi.lib advapi32.lib shell32.lib user32.lib gdi32.lib ole32.lib windowscodecs.lib winmm.lib
if %errorlevel% neq 0 (
    echo [-] Compilation failed.
    exit /b %errorlevel%
)

echo [*] Cleaning up intermediate object files...
del *.obj 2>nul

echo [+] Compilation successful! Running release packager...
call scripts\package_release.bat
