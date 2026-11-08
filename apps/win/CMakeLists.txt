# Native Windows 3.x API for murm386 EZ applications.
#
# The core (KERNEL/GDI/USER), the drivers and the runtime entry are compiled
# into every application; samples/<name> holds the application sources.
#
#   cmake -S apps/win -B build-win && cmake --build build-win
#
# The result is apps/compiled/winn.exe ("Windows native"); WIN_APP picks the
# application started by it (default: samples/hello).
#
# The same sources build on Linux for snapshot tests: see host/Makefile.

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
include(pico_sdk_import.cmake)

set(WIN_APP hello CACHE STRING "Application under samples/ to build")

project(winn C CXX ASM)
pico_sdk_init()

set(BUILD_NAME "${PROJECT_NAME}")
set(CMAKE_C_STANDARD 11)

set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/../../bin/${CMAKE_BUILD_TYPE}")
set(COMPILED_DIR "${CMAKE_SOURCE_DIR}/../../apps/compiled")
if(WIN32)
    set(ELF2EZ_TOOL "${CMAKE_SOURCE_DIR}/../../tools/elf2ez.exe")
else()
    set(ELF2EZ_TOOL "${CMAKE_SOURCE_DIR}/../../tools/elf2ez")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

file(GLOB WIN_CORE "${CMAKE_CURRENT_SOURCE_DIR}/core/*.c")
file(GLOB WIN_USER "${CMAKE_CURRENT_SOURCE_DIR}/user/*.c")
file(GLOB WIN_DRV  "${CMAKE_CURRENT_SOURCE_DIR}/drivers/dos/*.c")
file(GLOB APP_SRC  "${CMAKE_CURRENT_SOURCE_DIR}/samples/${WIN_APP}/*.c")

add_executable(${PROJECT_NAME}
    ${WIN_CORE}
    ${WIN_USER}
    ${WIN_DRV}
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/winmain_ez.c
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/ez_process.c
    ${APP_SRC}
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../api/native-dos-runtime.cmake)
native_dos_runtime_attach(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../api/crt0.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../api/crt0.S
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/ddi
    ${CMAKE_CURRENT_SOURCE_DIR}/user
    ${CMAKE_CURRENT_SOURCE_DIR}/../api
)

configure_file(memmap.ld.in memmap.ld @ONLY)
pico_set_linker_script(${PROJECT_NAME} ${CMAKE_CURRENT_BINARY_DIR}/memmap.ld)

pico_set_program_name(${PROJECT_NAME} "Windows native: ${WIN_APP}")
pico_set_program_version(${PROJECT_NAME} "${WIN_APP}")

target_link_options(${PROJECT_NAME} PRIVATE -Xlinker --print-memory-usage -r)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
target_compile_options(${PROJECT_NAME} PUBLIC
        -fno-jump-tables  # suppress __gnu_thumb1_case_uni symbol generation
        -Wall
        -Wno-unused-parameter
)
endif ()

target_compile_definitions(${PROJECT_NAME} PRIVATE ELF_MODE)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${BUILD_NAME}")

pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    # -r link: unresolved symbols must be rejected explicitly (no dynamic
    # linker at run time); elf2ez provides the __ez_*_array_* bounds.
    COMMAND ${CMAKE_COMMAND}
        -DNM=${CMAKE_NM}
        -DINPUT=$<TARGET_FILE:${PROJECT_NAME}>
        -DALLOW_UNRESOLVED=__ez_preinit_array_start,__ez_preinit_array_end,__ez_init_array_start,__ez_init_array_end,__ez_fini_array_start,__ez_fini_array_end
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_unresolved.cmake
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:${PROJECT_NAME}>
        ${COMPILED_DIR}/${BUILD_NAME}.crt
    COMMAND ${ELF2EZ_TOOL}
        /y ${COMPILED_DIR}/${BUILD_NAME}.crt
    COMMAND ${CMAKE_COMMAND} -E rm -f
        ${COMPILED_DIR}/${BUILD_NAME}.crt
)
