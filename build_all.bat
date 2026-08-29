@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "ROOT=%~dp0"
cd /d "%ROOT%"

set "CPU_TARGET=286"
if not "%~1"=="" if not "%~1:~0,1%"=="-" (
    set "CPU_TARGET=%~1"
    shift
)
if not "!CPU_TARGET!"=="286" (
    echo CPU target '!CPU_TARGET!' is not enabled in build_all: the 386 branch is currently untested. 1>&2
    exit /b 2
)

set "EXTRA_ARGS="
:collect_args
if "%~1"=="" goto args_done
if /I "%~1"=="--emm" (
    echo build_all controls EMM itself and builds both OFF and ON variants; do not pass --emm. 1>&2
    exit /b 2
)
set "EXTRA_ARGS=!EXTRA_ARGS! %~1"
shift
goto collect_args
:args_done
set /a COUNT=0
set "TOTAL=32"

for %%B in (M1 M2 PC Z2 C2) do (
    rem One paging firmware: MCGA/EGA128/VGA128/VGA256 selected at runtime.
    if "%%B"=="PC" (
        call :build_one %%B RUNTIME PWM OFF
        if errorlevel 1 exit /b !errorlevel!
        call :build_one %%B RUNTIME PWM ON
        if errorlevel 1 exit /b !errorlevel!
    ) else if "%%B"=="C2" (
        call :build_one %%B RUNTIME I2S OFF
        if errorlevel 1 exit /b !errorlevel!
        call :build_one %%B RUNTIME I2S ON
        if errorlevel 1 exit /b !errorlevel!
    ) else (
        call :build_one %%B RUNTIME I2S OFF
        if errorlevel 1 exit /b !errorlevel!
        call :build_one %%B RUNTIME I2S ON
        if errorlevel 1 exit /b !errorlevel!
        call :build_one %%B RUNTIME PWM OFF
        if errorlevel 1 exit /b !errorlevel!
        call :build_one %%B RUNTIME PWM ON
        if errorlevel 1 exit /b !errorlevel!
    )

    rem The only separate memory model: VGA256 with direct QSPI guest RAM.
    if "%%B"=="PC" (
        call :build_one %%B VGA256 PWM OFF NP
        if errorlevel 1 exit /b !errorlevel!
        call :build_one %%B VGA256 PWM ON NP
        if errorlevel 1 exit /b !errorlevel!
    ) else if "%%B"=="C2" (
        call :build_one %%B VGA256 I2S OFF NP
        if errorlevel 1 exit /b !errorlevel!
        call :build_one %%B VGA256 I2S ON NP
        if errorlevel 1 exit /b !errorlevel!
    ) else (
        call :build_one %%B VGA256 I2S OFF NP
        if errorlevel 1 exit /b !errorlevel!
        call :build_one %%B VGA256 I2S ON NP
        if errorlevel 1 exit /b !errorlevel!
        call :build_one %%B VGA256 PWM OFF NP
        if errorlevel 1 exit /b !errorlevel!
        call :build_one %%B VGA256 PWM ON NP
        if errorlevel 1 exit /b !errorlevel!
    )
)
echo.
echo All %TOTAL% supported 286 variants ^(with and without EMM^) built. UF2 files are under bin/^<build-type^>/.
exit /b 0

:build_one
set /a COUNT+=1
set "B=%~1"
set "V=%~2"
set "A=%~3"
set "E=%~4"
set "P=%~5"
set "TAG=!B!-286-!V!-!A!"
set "EMM_ARG="
set "PAGING_ARG="
if /I "!E!"=="ON" (
    set "TAG=!TAG!-emm"
    set "EMM_ARG=--emm"
)
if /I "!P!"=="NP" (
    set "TAG=!TAG!-np"
    set "PAGING_ARG=--no-paging"
)
echo.
echo [!COUNT!/%TOTAL%] !TAG!
call "%ROOT%build.bat" --board !B! --video !V! --audio !A! --build-dir "%ROOT%build\all\!TAG!" !EMM_ARG! !PAGING_ARG! %EXTRA_ARGS%
exit /b %errorlevel%
