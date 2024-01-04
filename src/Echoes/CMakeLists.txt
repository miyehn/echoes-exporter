# CMake build for psd_sdk samples
# Copyright 2023, heavenstone <heavenstone1308@gmail.com>
# See LICENSE.txt for licensing details (2-clause BSD License: https://opensource.org/licenses/BSD-2-Clause)

cmake_minimum_required(VERSION 3.2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(Echoes)

include_directories(../External)

add_definitions(-DWINOS)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG=1")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DDEBUG=0")

set(echoes_exporter_source_win
    WinMain.cpp
    AssetPack.cpp
    Utils.cpp
    ../External/stb_image_write.cpp
    ../External/stb_image_resize.cpp
    ../External/jsoncpp.cpp
)

set(echoes_gather_sprites_source
    GatherSprites.cpp
    Utils.cpp
    ../External/stb_image.cpp
    ../External/jsoncpp.cpp
)

set(echoes_make_blueprint_source
    MakeBlueprint.cpp
    Utils.cpp
    ../External/jsoncpp.cpp
)

add_executable(${PROJECT_NAME}Exporter_Windowed WIN32 ${echoes_exporter_source_win})
add_executable(${PROJECT_NAME}GatherSprites ${echoes_gather_sprites_source})
add_executable(${PROJECT_NAME}MakeBlueprint ${echoes_make_blueprint_source})

target_compile_definitions(${PROJECT_NAME}Exporter_Windowed PRIVATE ECHOES_EXPORTER_WINDOWED=1)

target_link_libraries(${PROJECT_NAME}Exporter_Windowed Psd)
target_link_libraries(${PROJECT_NAME}GatherSprites Psd)
target_link_libraries(${PROJECT_NAME}MakeBlueprint Psd)
