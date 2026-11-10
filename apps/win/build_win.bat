@echo off
rem Rebuild everything of the native Windows: WINN.EXE (apps\win), the driver
rem modules (apps\windrv\*), the native DLLs (apps\windll\*) and the
rem applications (apps\winapps\*), each in a
rem fresh build directory, into apps\compiled. Mirrors build_win.sh.
rem
rem The same sources give byte-identical files only with the same toolchain,
rem Pico SDK and build type. This script fixes the build type (MinSizeRel)
rem and the generator (Ninja); the SDK and the toolchain are the ones the
rem CMakeLists name for the Pico VS Code extension (%USERPROFILE%\.pico-sdk:
rem SDK 2.2.0, Arm GNU Toolchain 14_2_Rel1), or PICO_SDK_PATH /
rem PICO_TOOLCHAIN_PATH from the environment. At the end it prints the
rem compiler and a SHA-256 of every file, to compare builds.
rem
rem   apps\win\build_win.bat [-j N]
setlocal EnableExtensions EnableDelayedExpansion

set "WIN_DIR=%~dp0"
if "%WIN_DIR:~-1%"=="\" set "WIN_DIR=%WIN_DIR:~0,-1%"
for %%R in ("%WIN_DIR%\..\..") do set "ROOT=%%~fR"
set "BUILD_ROOT=%ROOT%\build\win"
set "COMPILED=%ROOT%\apps\compiled"
set "BUILD_TYPE=MinSizeRel"
set "JOBS="

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="-j" set "JOBS=--parallel %~2"& shift& shift& goto parse
if /I "%~1"=="--jobs" set "JOBS=--parallel %~2"& shift& shift& goto parse
if /I "%~1"=="-h" goto help
if /I "%~1"=="--help" goto help
echo Unknown option: %~1 1>&2
exit /b 2
:help
echo Usage: apps\win\build_win.bat [-j N]
exit /b 0
:parsed

rem Ninja: PATH first, then the one of the Pico VS Code extension
set "NINJA_EXE="
for /f "delims=" %%N in ('where ninja.exe 2^>nul') do if not defined NINJA_EXE set "NINJA_EXE=%%N"
if not defined NINJA_EXE (
    for /d %%D in ("%USERPROFILE%\.pico-sdk\ninja\*") do (
        if exist "%%~fD\ninja.exe" set "NINJA_EXE=%%~fD\ninja.exe"
    )
)
if not defined NINJA_EXE (
    echo Ninja was not found on PATH or under "%USERPROFILE%\.pico-sdk\ninja". 1>&2
    exit /b 2
)

set "SDK_ARGS="
if defined PICO_SDK_PATH set "SDK_ARGS=!SDK_ARGS! "-DPICO_SDK_PATH=%PICO_SDK_PATH%""
if defined PICO_TOOLCHAIN_PATH set "SDK_ARGS=!SDK_ARGS! "-DPICO_TOOLCHAIN_PATH=%PICO_TOOLCHAIN_PATH%""

rem elf2ez turns the linked modules into EZ files
if not exist "%ROOT%\tools\elf2ez.exe" (
    echo == elf2ez ^(host tool^)
    pushd "%ROOT%\apps\elf2ez"
    call build-host.bat
    popd
    if not exist "%ROOT%\tools\elf2ez.exe" (
        echo elf2ez could not be built 1>&2
        exit /b 1
    )
)

if not exist "%BUILD_ROOT%" mkdir "%BUILD_ROOT%"
set "OUTPUTS="

rem the projects, in build order (the core first: it owns the export tables)
call :build_one "%WIN_DIR%" || exit /b 1
for /d %%D in ("%ROOT%\apps\windrv\*") do (
    if exist "%%~fD\CMakeLists.txt" call :build_one "%%~fD" || exit /b 1
)
for /d %%D in ("%ROOT%\apps\windll\*") do (
    if exist "%%~fD\CMakeLists.txt" call :build_one "%%~fD" || exit /b 1
)
for /d %%D in ("%ROOT%\apps\winapps\*") do (
    if exist "%%~fD\CMakeLists.txt" call :build_one "%%~fD" || exit /b 1
)

rem what was used, and what came out
set "CACHE=%BUILD_ROOT%\win\CMakeCache.txt"
set "CC="
set "SDK="
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_C_COMPILER:" "%CACHE%"') do set "CC=%%B"
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"PICO_SDK_PATH:" "%CACHE%"') do set "SDK=%%B"
set "SDKV="
for %%K in (MAJOR MINOR REVISION) do (
    for /f "tokens=2 delims=) " %%V in ('findstr /r /c:"^ *set(PICO_SDK_VERSION_%%K [0-9]" "!SDK!\pico_sdk_version.cmake"') do (
        if defined SDKV (set "SDKV=!SDKV!.%%V") else set "SDKV=%%V"
    )
)
echo.
set "CCV="
for /f "delims=" %%L in ('call "!CC!" --version') do if not defined CCV set "CCV=%%L"
echo Compiler : !CCV!
echo Pico SDK : !SDKV! ^(!SDK!^)
echo Type     : %BUILD_TYPE%
echo.
for %%F in (!OUTPUTS!) do (
    set "HASH="
    for /f "skip=1 delims=" %%H in ('certutil -hashfile "%COMPILED%\%%~F" SHA256') do (
        if not defined HASH set "HASH=%%H"
    )
    set "HASH=!HASH: =!"
    set "NAME=%%~F            "
    for %%S in ("%COMPILED%\%%~F") do set "SIZE=        %%~zS"
    echo !NAME:~0,12! !SIZE:~-8!  !HASH!
)
exit /b 0

:build_one
set "P=%~1"
set "NAME=%~nx1"
set "DIR=%BUILD_ROOT%\!NAME!"
echo.
echo == !NAME!
if exist "!DIR!" rmdir /s /q "!DIR!"
cmake -G Ninja "-DCMAKE_MAKE_PROGRAM:FILEPATH=!NINJA_EXE!" -S "!P!" -B "!DIR!" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% !SDK_ARGS! > "!DIR!.log" 2>&1
if errorlevel 1 (
    type "!DIR!.log"
    echo configure failed: !NAME! 1>&2
    exit /b 1
)
cmake --build "!DIR!" %JOBS% >> "!DIR!.log" 2>&1
if errorlevel 1 (
    type "!DIR!.log"
    echo build failed: !NAME! 1>&2
    exit /b 1
)
findstr /c:"EZ image" /c:"warning" "!DIR!.log"
rem the EZ file this project wrote: the newest file in apps\compiled
for /f "delims=" %%F in ('dir /b /a-d /o-d "%COMPILED%"') do (
    set "OUTPUTS=!OUTPUTS! "%%F""
    exit /b 0
)
exit /b 0
