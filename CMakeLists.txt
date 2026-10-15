cmake_minimum_required(VERSION 3.20)
project(ClusterShift VERSION 0.5.0 LANGUAGES CXX)

include_directories(include)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS 
    src/*.cpp
)

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS 
    include/*.hpp
)

add_executable(clustershift ${SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(clustershift PRIVATE /std:c++latest /EHsc /utf-8 /W4)
else()
    target_compile_options(clustershift PRIVATE -std=c++23 -Wall)
endif()

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
