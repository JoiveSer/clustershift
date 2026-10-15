cmake_minimum_required(VERSION 3.20)
project(ClusterShift VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

file(GLOB CORE_SOURCES src/core/*.cpp)
file(GLOB CORE_HEADERS include/core/*.hpp)

# 1. Headless CLI Target
add_executable(clustershift src/main.cpp ${CORE_SOURCES} ${CORE_HEADERS})

target_compile_definitions(clustershift PRIVATE 
    WIN32_LEAN_AND_MEAN 
    NOMINMAX 
    UNICODE 
    _UNICODE
)

target_link_libraries(clustershift PRIVATE 
    kernel32 
    advapi32 
    shell32
)

# 2. DirectX 11 + Dear ImGui GUI Target
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp")
    file(GLOB IMGUI_SOURCES 
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/backends/imgui_impl_win32.cpp
        third_party/imgui/backends/imgui_impl_dx11.cpp
    )

    add_executable(clustershift-gui WIN32 
        src/gui/gui_main.cpp 
        ${CORE_SOURCES} 
        ${CORE_HEADERS} 
        ${IMGUI_SOURCES}
    )

    target_include_directories(clustershift-gui PRIVATE 
        third_party/imgui 
        third_party/imgui/backends
    )

    target_compile_definitions(clustershift-gui PRIVATE 
        WIN32_LEAN_AND_MEAN 
        NOMINMAX 
        UNICODE 
        _UNICODE
    )

    target_link_libraries(clustershift-gui PRIVATE 
        kernel32 
        user32 
        gdi32 
        advapi32 
        shell32 
        d3d11 
        dxgi 
        dwmapi
    )
endif()

if(MSVC)
    add_compile_options(/std:c++latest /EHsc /utf-8 /W4)
endif()
