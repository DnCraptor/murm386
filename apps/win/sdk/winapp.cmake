# Building a Windows application module for WINN.EXE.
#
# In the application's CMakeLists.txt (see apps/winapps/demo):
#
#   include(${CMAKE_CURRENT_SOURCE_DIR}/../../win/sdk/winapp.cmake)
#   win_app(DEMO demo.c)
#
# The result is apps/compiled/<NAME>.EXE: an EZ image whose entry is the
# SDK's __ez_start (no crt0), calling WinMain on its own task. API calls go
# through the export tables of WINN.EXE by Win16 ordinal.

set(WIN_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(WIN_DIR "${WIN_SDK_DIR}/..")
set(WIN_APPS_ROOT "${WIN_DIR}/../..")

function(win_app NAME)
    set(sources ${ARGN})
    set(target ${PROJECT_NAME})
    add_executable(${target}
        ${sources}
        ${WIN_SDK_DIR}/winapp_entry.c
        ${WIN_SDK_DIR}/winapi_stubs.S
    )
    include(${WIN_APPS_ROOT}/apps/api/native-dos-runtime.cmake)
    native_dos_runtime_attach(${target})
    target_include_directories(${target} PRIVATE
        ${WIN_DIR}/include
        ${WIN_DIR}/ddi
        ${WIN_APPS_ROOT}/apps/api
    )
    configure_file(${WIN_DIR}/memmap.ld.in ${CMAKE_CURRENT_BINARY_DIR}/memmap.ld @ONLY)
    pico_set_linker_script(${target} ${CMAKE_CURRENT_BINARY_DIR}/memmap.ld)
    pico_set_program_name(${target} "Windows application: ${NAME}")
    target_link_options(${target} PRIVATE -Xlinker --print-memory-usage -r)
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PUBLIC -fno-jump-tables -Wall -Wno-unused-parameter)
    endif ()
    target_compile_definitions(${target} PRIVATE ELF_MODE)
    pico_enable_stdio_uart(${target} 0)
    pico_enable_stdio_usb(${target} 0)

    set(compiled "${WIN_APPS_ROOT}/apps/compiled")
    if(WIN32)
        set(elf2ez "${WIN_APPS_ROOT}/tools/elf2ez.exe")
    else()
        set(elf2ez "${WIN_APPS_ROOT}/tools/elf2ez")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DNM=${CMAKE_NM}
            -DINPUT=$<TARGET_FILE:${target}>
            -DALLOW_UNRESOLVED=__ez_preinit_array_start,__ez_preinit_array_end,__ez_init_array_start,__ez_init_array_end,__ez_fini_array_start,__ez_fini_array_end
            -P ${WIN_DIR}/check_unresolved.cmake
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> ${compiled}/${NAME}.crt
        COMMAND ${elf2ez} /y ${compiled}/${NAME}.crt
        COMMAND ${CMAKE_COMMAND} -E rm -f ${compiled}/${NAME}.crt
    )
endfunction()
