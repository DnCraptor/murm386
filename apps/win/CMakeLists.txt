# Native Windows 3.x for murm386: WIN.COM loads the WINN.EXE core.
#
#   cmake -S apps/win -B build-win && cmake --build build-win
#
# The outputs are apps/compiled/win.com and apps/compiled/winn.exe.
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

project(winn C CXX ASM)
pico_sdk_init()

set(CMAKE_C_STANDARD 11)

set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/../../bin/${CMAKE_BUILD_TYPE}")
set(COMPILED_DIR "${CMAKE_SOURCE_DIR}/../../apps/compiled")
if(CMAKE_HOST_WIN32)
    set(ELF2EZ_TOOL "${CMAKE_SOURCE_DIR}/../../tools/elf2ez.exe")
else()
    set(ELF2EZ_TOOL "${CMAKE_SOURCE_DIR}/../../tools/elf2ez")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

file(GLOB WIN_CORE "${CMAKE_CURRENT_SOURCE_DIR}/core/*.c")
file(GLOB WIN_USER "${CMAKE_CURRENT_SOURCE_DIR}/user/*.c")
file(GLOB WIN_DRV  "${CMAKE_CURRENT_SOURCE_DIR}/drivers/dos/*.c")

add_executable(${PROJECT_NAME}
    ${WIN_CORE}
    ${WIN_USER}
    ${WIN_DRV}
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/winmain_ez.c
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/ez_process.c
)

add_executable(win_loader
    core/ez_image.c
    runtime/win_loader.c
    runtime/ez_process.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../api/native-dos-runtime.cmake)
configure_file(memmap.ld.in memmap.ld @ONLY)

function(win_ez_target target filename)
    native_dos_runtime_attach(${target})
    target_sources(${target} PRIVATE ../api/crt0.c ../api/crt0.S)
    target_include_directories(${target} PRIVATE include core ddi user)
    pico_set_linker_script(${target} ${CMAKE_CURRENT_BINARY_DIR}/memmap.ld)
    target_link_options(${target} PRIVATE -Xlinker --print-memory-usage -r)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE -fno-jump-tables -Wall -Wno-unused-parameter)
    endif()
    target_compile_definitions(${target} PRIVATE ELF_MODE)
    pico_enable_stdio_uart(${target} 0)
    pico_enable_stdio_usb(${target} 0)

    # Older host converters always derive a .exe filename from the input.
    # Convert in the build directory before publishing the final .exe or .com.
    set(ez_stage "${CMAKE_CURRENT_BINARY_DIR}/${target}-ez")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DNM=${CMAKE_NM}
            -DINPUT=$<TARGET_FILE:${target}>
            -DALLOW_UNRESOLVED=__ez_preinit_array_start,__ez_preinit_array_end,__ez_init_array_start,__ez_init_array_end,__ez_fini_array_start,__ez_fini_array_end
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_unresolved.cmake
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> ${ez_stage}.crt
        COMMAND ${ELF2EZ_TOOL} /y ${ez_stage}.crt
        COMMAND ${CMAKE_COMMAND} -E copy ${ez_stage}.exe ${COMPILED_DIR}/${filename}
        COMMAND ${CMAKE_COMMAND} -E rm -f ${ez_stage}.crt ${ez_stage}.exe
        VERBATIM
    )
endfunction()

win_ez_target(winn winn.exe)
win_ez_target(win_loader win.com)
add_dependencies(win_loader winn)
