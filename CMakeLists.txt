cmake_minimum_required(VERSION 3.21)
project(visual_governor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "This Nexus addon is Windows/MSVC only.")
endif()

set(IMGUI_DIR "" CACHE PATH "Path to Dear ImGui source folder, e.g. C:/dev/imgui-v180")
if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR "IMGUI_DIR must point to a Dear ImGui source folder containing imgui.cpp. Pass -DIMGUI_DIR=... or use tools/build.ps1 -ImguiDir ...")
endif()

add_library(visual_governor SHARED
    src/Addon.cpp
    src/addon.def
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
)

target_include_directories(visual_governor PRIVATE
    sdk
    ${IMGUI_DIR}
)

target_compile_definitions(visual_governor PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(visual_governor PRIVATE shlwapi)

if(MSVC)
    target_compile_options(visual_governor PRIVATE /W4 /permissive- /EHsc /MP)
    target_link_options(visual_governor PRIVATE /DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/addon.def)
endif()

set_target_properties(visual_governor PROPERTIES
    OUTPUT_NAME "visual_governor"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
