# VGA800: display driver module for the native Windows core
# (VBE 102h, 800x600, 16 colours). Built as an EZ image without crt0 and
# written to apps/compiled/VGA800.DRV; enable it in SYSTEM.INI:
#
#   [boot]
#   display.drv=VGA800.DRV

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

project(vga800 C CXX ASM)
pico_sdk_init()

set(DRV_NAME "VGA800")
set(CMAKE_C_STANDARD 11)

set(WIN_DIR "${CMAKE_SOURCE_DIR}/../../win")
set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/../../../bin/${CMAKE_BUILD_TYPE}")
set(COMPILED_DIR "${CMAKE_SOURCE_DIR}/../../compiled")
if(WIN32)
    set(ELF2EZ_TOOL "${CMAKE_SOURCE_DIR}/../../../tools/elf2ez.exe")
else()
    set(ELF2EZ_TOOL "${CMAKE_SOURCE_DIR}/../../../tools/elf2ez")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

add_executable(${PROJECT_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/vga800.c
    ${WIN_DIR}/ddi/winmod_shim.c
)

# malloc and friends come from the native DOS runtime; there is no crt0:
# the module's entry is __ez_start, called by the Windows core.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../api/native-dos-runtime.cmake)
native_dos_runtime_attach(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${WIN_DIR}/ddi
    ${WIN_DIR}/drivers/dos
    ${CMAKE_CURRENT_SOURCE_DIR}/../../api
)

configure_file(memmap.ld.in memmap.ld @ONLY)
pico_set_linker_script(${PROJECT_NAME} ${CMAKE_CURRENT_BINARY_DIR}/memmap.ld)
pico_set_program_name(${PROJECT_NAME} "Windows driver module: ${DRV_NAME}")

target_link_options(${PROJECT_NAME} PRIVATE -Xlinker --print-memory-usage -r)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
target_compile_options(${PROJECT_NAME} PUBLIC
        -fno-jump-tables
        -Wall
        -Wno-unused-parameter
)
endif ()

target_compile_definitions(${PROJECT_NAME} PRIVATE ELF_MODE)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DNM=${CMAKE_NM}
        -DINPUT=$<TARGET_FILE:${PROJECT_NAME}>
        -DALLOW_UNRESOLVED=__ez_preinit_array_start,__ez_preinit_array_end,__ez_init_array_start,__ez_init_array_end,__ez_fini_array_start,__ez_fini_array_end
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_unresolved.cmake
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:${PROJECT_NAME}>
        ${COMPILED_DIR}/${DRV_NAME}.crt
    COMMAND ${ELF2EZ_TOOL}
        /y ${COMPILED_DIR}/${DRV_NAME}.crt
    COMMAND ${CMAKE_COMMAND} -E rm -f
        ${COMPILED_DIR}/${DRV_NAME}.crt
    COMMAND ${CMAKE_COMMAND} -E rename
        ${COMPILED_DIR}/${DRV_NAME}.exe
        ${COMPILED_DIR}/${DRV_NAME}.DRV
)
