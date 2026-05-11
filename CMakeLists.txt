cmake_minimum_required(VERSION 3.16)
project(ssdv-ng LANGUAGES C CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Force multi-config generators (like Visual Studio) to output to the same flat directory
foreach(CONFIG "DEBUG" "RELEASE" "MINSIZEREL" "RELWITHDEBINFO")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG} "${CMAKE_BINARY_DIR}/bin")
endforeach()

# Set version (matching the old Makefile behavior)
set(VERSION "2.0.0" CACHE STRING "SSDV Version")
add_compile_definitions(SSDV_VERSION="${VERSION}")

# 1. Build the original SSDV CLI
add_executable(ssdv main.c ssdv.c rs8.c)
target_compile_options(ssdv PRIVATE -O3 -Wall -g)

# Force webview to be built as a static library.
# This is required on Windows to ensure the WinMain entry point is linked correctly into the final executable.
set(WEBVIEW_STATIC 1)

# 2. Fetch the Webview library
include(FetchContent)
FetchContent_Declare(
    webview
    GIT_REPOSITORY https://github.com/webview/webview
    GIT_TAG 0.12.0
)
FetchContent_MakeAvailable(webview)

# Generate gui_html.h from gui.html at build time without needing external tools
file(WRITE ${CMAKE_BINARY_DIR}/embed_html.cmake "
file(WRITE \"\${DST}\" \"#pragma once\\n#include <string>\\nconst std::string html_content = R\\\"HTML(\\n\")
file(READ \"\${SRC}\" CONTENT)
file(APPEND \"\${DST}\" \"\${CONTENT}\")
file(APPEND \"\${DST}\" \"\\n)HTML\\\";\\n\")
")

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gui_html.h
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/gui.html -DDST=${CMAKE_CURRENT_BINARY_DIR}/gui_html.h -P ${CMAKE_BINARY_DIR}/embed_html.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gui.html
)

# 3. Build the GUI executable
# WIN32 prevents the console window from popping up on Windows
add_executable(ssdv-gui WIN32 gui.cpp ssdv.c rs8.c ${CMAKE_CURRENT_BINARY_DIR}/gui_html.h)
target_include_directories(ssdv-gui PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(ssdv-gui PRIVATE webview::core)

if(WIN32)
    target_link_libraries(ssdv-gui PRIVATE comdlg32)
endif()