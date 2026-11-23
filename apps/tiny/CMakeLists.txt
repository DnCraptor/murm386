cmake_minimum_required(VERSION 3.22)
set(PICO_BOARD pico2 CACHE STRING "RP2350 target")
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
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${COMPILED}"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:tiny>" "${COMPILED}/tiny.crt"
    COMMAND "${ELF2EZ}" /y "${COMPILED}/tiny.crt"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${COMPILED}/tiny.crt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/TINY.BIN" "${COMPILED}/TINY.BIN"
    VERBATIM
)
