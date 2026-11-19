# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.22)

set(PICO_BOARD pico CACHE STRING "Bootstrap ARM board")
include(../../pico_sdk_import.cmake)
project(native_tcc C CXX ASM)
pico_sdk_init()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

get_filename_component(ROOT "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(API "${ROOT}/apps/api")
set(WIN "${ROOT}/apps/win")
if(CMAKE_HOST_WIN32)
    set(elf2ez_host_name elf2ez.exe)
else()
    set(elf2ez_host_name elf2ez)
endif()

set(ELF2EZ_HOST_TOOL "" CACHE FILEPATH "Optional separately built host elf2ez with -o support")
set(ELF2EZ_HOST_GENERATOR "" CACHE STRING "Optional CMake generator for the native elf2ez build")
# Existing build directories may still have the old default in their cache.
cmake_path(COMPARE "${ELF2EZ_HOST_TOOL}" EQUAL "${ROOT}/tools/${elf2ez_host_name}" elf2ez_old_default)
if(NOT ELF2EZ_HOST_TOOL OR elf2ez_old_default)
    include(ExternalProject)
    set(elf2ez_host_source "${ROOT}/apps/elf2ez")
    set(elf2ez_host_dir "${CMAKE_CURRENT_BINARY_DIR}/host-elf2ez")
    set(elf2ez_host_generator "${ELF2EZ_HOST_GENERATOR}")
    set(elf2ez_host_args)
    if(CMAKE_HOST_WIN32)
        find_program(ELF2EZ_HOST_GCC NAMES gcc.exe NO_CMAKE_FIND_ROOT_PATH)
        if(NOT ELF2EZ_HOST_GCC)
            message(FATAL_ERROR
                "MinGW GCC (gcc.exe) is required to build the host elf2ez. "
                "Set ELF2EZ_HOST_GCC or put the MinGW bin directory on PATH.")
        endif()
        list(APPEND elf2ez_host_args "-DCMAKE_C_COMPILER:FILEPATH=${ELF2EZ_HOST_GCC}")
        # Use a fresh native cache when upgrading from the previous host build.
        set(elf2ez_host_dir "${CMAKE_CURRENT_BINARY_DIR}/host-elf2ez-gcc")
        if(NOT elf2ez_host_generator)
            if(CMAKE_GENERATOR MATCHES "^Ninja")
                set(elf2ez_host_generator Ninja)
                list(APPEND elf2ez_host_args "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}")
            else()
                set(elf2ez_host_generator "MinGW Makefiles")
            endif()
        endif()
    endif()
    if(elf2ez_host_generator)
        list(APPEND elf2ez_host_args -G "${elf2ez_host_generator}")
    endif()
    set(elf2ez_converter "${elf2ez_host_dir}/${elf2ez_host_name}")
    # Configure a separate native project without the parent ARM toolchain.
    ExternalProject_Add(tcc_host_elf2ez
        SOURCE_DIR "${elf2ez_host_source}"
        BINARY_DIR "${elf2ez_host_dir}"
        CONFIGURE_COMMAND "${CMAKE_COMMAND}" -S <SOURCE_DIR> -B <BINARY_DIR>
            ${elf2ez_host_args}
            -DCMAKE_TOOLCHAIN_FILE:FILEPATH=
            -DCMAKE_BUILD_TYPE:STRING=Release
            -DELF2EZ_HOST:BOOL=ON
            "-DELF2EZ_HOST_OUTPUT_DIR:PATH=${elf2ez_host_dir}"
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release --target elf2ez
        BUILD_ALWAYS TRUE
        BUILD_BYPRODUCTS "${elf2ez_converter}"
        INSTALL_COMMAND ""
    )
    set(elf2ez_host_dependency tcc_host_elf2ez)
elseif(EXISTS "${ELF2EZ_HOST_TOOL}")
    set(elf2ez_converter "${ELF2EZ_HOST_TOOL}")
    set(elf2ez_host_dependency "${elf2ez_converter}")
else()
    message(FATAL_ERROR "ELF2EZ_HOST_TOOL does not exist: ${ELF2EZ_HOST_TOOL}")
endif()

function(native_objects target)
    target_compile_options(${target} PRIVATE
        -mfloat-abi=soft -Os -fno-builtin -fno-jump-tables -ffunction-sections -fdata-sections)
    target_include_directories(${target} PRIVATE "${API}"
        "${CMAKE_CURRENT_SOURCE_DIR}/compat" "${CMAKE_CURRENT_SOURCE_DIR}")
endfunction()

function(native_link target)
    native_objects(${target})
    target_link_options(${target} PRIVATE -mfloat-abi=soft -nostdlib -r)
    pico_enable_stdio_uart(${target} 0)
    pico_enable_stdio_usb(${target} 0)
endfunction()

# Same libc and AEABI shims as Doom plus the compiler's small POSIX subset.
add_library(fdos STATIC mos_time.c fdos_compat.c setjmp.S strto.c strtod.c ldexp.c localtime.c)
include("${API}/native-dos-runtime.cmake")
native_dos_runtime_attach(fdos)
native_objects(fdos)

add_executable(fdoscrt "${API}/crt0.c" "${API}/crt0.S")
native_link(fdoscrt)

include("${WIN}/sdk/winapp.cmake")
win_app_info_header("${CMAKE_CURRENT_BINARY_DIR}/winapp_info.h" "Native C application")
add_executable(wincrt "${WIN}/sdk/winapp_entry.c" "${WIN}/sdk/winapi_stubs.S")
target_include_directories(wincrt PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${WIN}/include" "${WIN}/ddi")
native_link(wincrt)

add_executable(tcc_bootstrap tcc.c requirements.c "${API}/crt0.c" "${API}/crt0.S")
native_link(tcc_bootstrap)
target_compile_definitions(tcc_bootstrap PRIVATE NO_SEMAPHORE_H=1)
target_link_libraries(tcc_bootstrap PRIVATE fdos)

add_executable(elf2ez_bootstrap "${ROOT}/apps/elf2ez/elf2ez.c" "${API}/crt0.c" "${API}/crt0.S")
native_link(elf2ez_bootstrap)
target_link_libraries(elf2ez_bootstrap PRIVATE fdos)

set(SDK "${CMAKE_CURRENT_BINARY_DIR}/TCC")
set(COMPILED_DIR "${ROOT}/apps/compiled")
add_custom_target(tcc_sdk ALL
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/stage_sdk.py"
        "${ROOT}" "${SDK}" "$<TARGET_FILE:fdoscrt>" "$<TARGET_FILE:wincrt>" "$<TARGET_FILE:fdos>"
        "$<TARGET_FILE:tcc_bootstrap>" "$<TARGET_FILE:elf2ez_bootstrap>" "${elf2ez_converter}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${COMPILED_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SDK}/TCC.COM" "${COMPILED_DIR}/tcc.com"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SDK}/ELF2EZ.COM" "${COMPILED_DIR}/elf2ez.com"
    DEPENDS fdoscrt wincrt fdos tcc_bootstrap elf2ez_bootstrap "${elf2ez_host_dependency}"
    VERBATIM
)
