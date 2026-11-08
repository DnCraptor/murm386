# CONTROL.EXE: the Control Panel.

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.22)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../win/pico_sdk_import.cmake)

project(control C CXX ASM)
pico_sdk_init()
set(CMAKE_C_STANDARD 11)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../../../bin/${CMAKE_BUILD_TYPE}")

include(${CMAKE_CURRENT_SOURCE_DIR}/../../win/sdk/winapp.cmake)
win_app(CONTROL control.c)
