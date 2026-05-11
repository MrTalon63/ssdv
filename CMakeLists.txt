cmake_minimum_required(VERSION 3.16)
project(ssdv-ng LANGUAGES C CXX)

if(POLICY CMP0174)
    cmake_policy(SET CMP0174 NEW)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

foreach(CONFIG "DEBUG" "RELEASE" "MINSIZEREL" "RELWITHDEBINFO")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG} "${CMAKE_BINARY_DIR}/bin")
endforeach()

set(VERSION "2.0.0" CACHE STRING "SSDV Version")
add_compile_definitions(SSDV_VERSION="${VERSION}")

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

add_executable(ssdv main.c ssdv.c rs8.c)
if(MSVC)
    target_compile_options(ssdv PRIVATE /W3 /wd4244 /wd4267)
else()
    target_compile_options(ssdv PRIVATE -O3 -Wall -g)
endif()
if(WIN32)
    target_link_libraries(ssdv PRIVATE ws2_32)
endif()

set(WEBVIEW_STATIC 1)

include(FetchContent)

if(WIN32 AND NOT MSVC)
    FetchContent_Declare(
        webview2
        URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2849.39
    )
    FetchContent_MakeAvailable(webview2)
    include_directories(${webview2_SOURCE_DIR}/build/native/include)
endif()

FetchContent_Declare(
    webview
    GIT_REPOSITORY https://github.com/webview/webview
    GIT_TAG 0.12.0
)
FetchContent_MakeAvailable(webview)

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

add_executable(ssdv-gui WIN32 gui.cpp ssdv.c rs8.c ${CMAKE_CURRENT_BINARY_DIR}/gui_html.h)
target_include_directories(ssdv-gui PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
if(MSVC)
    target_compile_options(ssdv-gui PRIVATE /W3 /wd4244 /wd4267)
endif()
target_link_libraries(ssdv-gui PRIVATE webview::core)

# Extract numeric version for Windows RC file (e.g., "2.0.0" -> "2,0,0,0")
if(VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(RC_VERSION_COMMA "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
else()
    set(RC_VERSION_COMMA "0,0,0,0")
endif()

if(WIN32)
    target_link_libraries(ssdv-gui PRIVATE comdlg32 ws2_32)
    configure_file(resources.rc ${CMAKE_CURRENT_BINARY_DIR}/resources.rc @ONLY)
    target_sources(ssdv-gui PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/resources.rc)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
    target_link_options(ssdv PRIVATE -static-libgcc)
    target_link_options(ssdv-gui PRIVATE -static-libstdc++ -static-libgcc)
endif()