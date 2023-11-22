# CMake build for psd_sdk samples
# Copyright 2023, heavenstone <heavenstone1308@gmail.com>
# See LICENSE.txt for licensing details (2-clause BSD License: https://opensource.org/licenses/BSD-2-Clause)

cmake_minimum_required(VERSION 3.2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(EchoesExporter)

include_directories(../External)

add_definitions(-DWINOS)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG=1")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DDEBUG=0")

set(echoes_exporter_source_console
    ConsoleMain.cpp
    AssetPack.cpp
    ../External/stb_image_write.cpp
    ../External/stb_image_resize.cpp
    ../External/jsoncpp.cpp
)

set(echoes_exporter_source_win
    WinMain.cpp
    AssetPack.cpp
    ../External/stb_image_write.cpp
    ../External/stb_image_resize.cpp
    ../External/jsoncpp.cpp
)

add_executable(${PROJECT_NAME}_Console ${echoes_exporter_source_console})
add_executable(${PROJECT_NAME}_Windowed WIN32 ${echoes_exporter_source_win})

target_link_libraries(${PROJECT_NAME}_Console Psd)
target_link_libraries(${PROJECT_NAME}_Windowed Psd)
