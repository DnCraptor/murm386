@echo off
echo Building FDOS example...
..\TCC.COM -nostdlib -r -I../INCLUDE -o fdhello-smoke.o fdhello.c ../LIB/FDOSCRT.O -L../LIB -lfdos
if errorlevel 1 goto fdosfail
echo Converting FDOS example...
..\ELF2EZ.COM /y --thumb2 -o FDHELLO.EXE fdhello-smoke.o
if errorlevel 1 goto fdosfail
echo Running FDOS example...
FDHELLO.EXE
if errorlevel 1 goto fdosfail
echo Building WINN example...
..\TCC.COM -nostdlib -r -I../INCLUDE -o WNHELLO.O wnhello.c ../LIB/WINCRT.O -L../LIB -lfdos
if errorlevel 1 goto winnfail
echo Converting WINN example...
..\ELF2EZ.COM /y --thumb2 -o WNHELLO.EXE WNHELLO.O
if errorlevel 1 goto winnfail
echo Open WNHELLO.EXE from WINN to finish the check.
goto done
:fdosfail
echo FDOS example failed.
goto done
:winnfail
echo WINN example build failed.
:done
