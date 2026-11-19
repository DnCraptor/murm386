@echo off
..\TCC.COM -nostdlib -r -I../INCLUDE -o fdhello-smoke.o fdhello.c ../LIB/FDOSCRT.O -L../LIB -lfdos
if errorlevel 1 goto fdosfail
..\ELF2EZ.COM /y --thumb2 -o FDHELLO.EXE fdhello-smoke.o
if errorlevel 1 goto fdosfail
FDHELLO.EXE
if errorlevel 1 goto fdosfail
..\TCC.COM -nostdlib -r -I../INCLUDE -o WNHELLO.O wnhello.c ../LIB/WINCRT.O -L../LIB -lfdos
if errorlevel 1 goto winnfail
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
