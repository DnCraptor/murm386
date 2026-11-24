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
set(PICO_BOARD pico CACHE STRING "Board type")
include(${CMAKE_CURRENT_LIST_DIR}/../pico_sdk_import.cmake)
project(tiny_test C ASM)
pico_sdk_init()

set(CMAKE_C_STANDARD 11)
get_filename_component(ROOT "../../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(API "${ROOT}/apps/api")
set(TINY "${ROOT}/apps/tiny")
set(OUT "${ROOT}/bin/${CMAKE_BUILD_TYPE}")
set(COMPILED "${ROOT}/apps/compiled")
if(WIN32)
    set(ELF2EZ "${ROOT}/tools/elf2ez.exe")
else()
    set(ELF2EZ "${ROOT}/tools/elf2ez")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUT}")

# Same native runtime as TINY itself. test.c includes ../tiny.c, so the
# application's static functions run unchanged under this harness.
add_executable(tiny_test
    test.c
    "${TINY}/tiny_native.c"
    "${API}/crt0.c"
    "${API}/crt0.S"
)
include("${API}/native-dos-runtime.cmake")
native_dos_runtime_attach(tiny_test)
target_include_directories(tiny_test PRIVATE "${API}" "${TINY}")
target_compile_definitions(tiny_test PRIVATE TINY_NATIVE ELF_MODE)
target_compile_options(tiny_test PRIVATE
    -Os -mfloat-abi=soft -fno-builtin -fno-jump-tables
    -ffunction-sections -fdata-sections
    -fno-unwind-tables -fno-asynchronous-unwind-tables
)
target_link_options(tiny_test PRIVATE -mfloat-abi=soft -nostdlib -r)
configure_file("${ROOT}/apps/test/memmap.ld.in" "${CMAKE_CURRENT_BINARY_DIR}/memmap.ld" @ONLY)
pico_set_linker_script(tiny_test "${CMAKE_CURRENT_BINARY_DIR}/memmap.ld")
pico_enable_stdio_uart(tiny_test 0)
pico_enable_stdio_usb(tiny_test 0)
pico_set_program_name(tiny_test "TEST.EXE - TINY startup probes")

add_custom_command(TARGET tiny_test POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${COMPILED}"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:tiny_test>" "${COMPILED}/TTEST.crt"
    COMMAND "${ELF2EZ}" /y "${COMPILED}/TTEST.crt"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${COMPILED}/TTEST.crt"
    VERBATIM
)
