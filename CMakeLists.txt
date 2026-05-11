cmake_minimum_required(VERSION 3.16)
project(ssdv-ng LANGUAGES C CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

foreach(CONFIG "DEBUG" "RELEASE" "MINSIZEREL" "RELWITHDEBINFO")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG} "${CMAKE_BINARY_DIR}/bin")
endforeach()

set(VERSION "2.0.0" CACHE STRING "SSDV Version")
add_compile_definitions(SSDV_VERSION="${VERSION}")

add_executable(ssdv main.c ssdv.c rs8.c)
if(MSVC)
    target_compile_options(ssdv PRIVATE /W3)
else()
    target_compile_options(ssdv PRIVATE -O3 -Wall -g)
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
target_link_libraries(ssdv-gui PRIVATE webview::core)

if(WIN32)
    target_link_libraries(ssdv-gui PRIVATE comdlg32)
endif()