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
cmake_minimum_required(VERSION 3.22)
# Same target as every working native app (doom, win, tcc, apps/test):
# ARMv6-M / Thumb-1 code, EZ_FLAG_ARMV6M.
set(PICO_BOARD pico CACHE STRING "Board type")
include(${CMAKE_CURRENT_LIST_DIR}/../../pico_sdk_import.cmake)
project(tiny C ASM)
pico_sdk_init()

set(CMAKE_C_STANDARD 11)
get_filename_component(ROOT "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(API "${ROOT}/apps/api")
set(OUT "${ROOT}/bin/${CMAKE_BUILD_TYPE}")
set(COMPILED "${ROOT}/apps/compiled")
if(WIN32)
    set(ELF2EZ "${ROOT}/tools/elf2ez.exe")
else()
    set(ELF2EZ "${ROOT}/tools/elf2ez")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUT}")

add_executable(tiny
    tiny.c
    tiny_native.c
    "${API}/crt0.c"
    "${API}/crt0.S"
)
include("${API}/native-dos-runtime.cmake")
native_dos_runtime_attach(tiny)
target_include_directories(tiny PRIVATE "${API}")
target_compile_definitions(tiny PRIVATE TINY_NATIVE ELF_MODE)
target_compile_options(tiny PRIVATE
    -Os -mfloat-abi=soft -fno-builtin -fno-jump-tables
    -ffunction-sections -fdata-sections
    -fno-unwind-tables -fno-asynchronous-unwind-tables
)
target_link_options(tiny PRIVATE -mfloat-abi=soft -nostdlib -r)
configure_file("${ROOT}/apps/test/memmap.ld.in" "${CMAKE_CURRENT_BINARY_DIR}/memmap.ld" @ONLY)
pico_set_linker_script(tiny "${CMAKE_CURRENT_BINARY_DIR}/memmap.ld")
pico_enable_stdio_uart(tiny 0)
pico_enable_stdio_usb(tiny 0)
pico_set_program_name(tiny "TINY 2.1 - native DOS")

add_custom_command(TARGET tiny POST_BUILD
    # -r link permits unresolved symbols; reject them as apps/doom does.
    COMMAND "${CMAKE_COMMAND}"
        -DNM=${CMAKE_NM}
        -DINPUT=$<TARGET_FILE:tiny>
        -DALLOW_UNRESOLVED=__ez_preinit_array_start,__ez_preinit_array_end,__ez_init_array_start,__ez_init_array_end,__ez_fini_array_start,__ez_fini_array_end
        -P "${ROOT}/apps/doom/check_unresolved.cmake"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${COMPILED}"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:tiny>" "${COMPILED}/tiny.crt"
    COMMAND "${ELF2EZ}" /y "${COMPILED}/tiny.crt"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${COMPILED}/tiny.crt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/TINY.BIN" "${COMPILED}/TINY.BIN"
    VERBATIM
)
