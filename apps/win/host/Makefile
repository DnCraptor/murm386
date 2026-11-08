# Host build of the native Windows core for snapshot tests (gcc, Linux).
# -m32: LPARAM/LONG carry pointers, as on the 32-bit target.
CC      ?= gcc
CFLAGS  ?= -m32 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined
INC      = -I../include -I../core -I../ddi -I../user -I.
CORE     = $(wildcard ../core/*.c)
USER     = $(wildcard ../user/*.c)
HOST     = host_drivers.c
# GDI test: no USER, so no tasks, files and export tables
GDICORE  = $(filter-out ../core/exports.c ../core/task.c ../core/file.c,$(CORE))

# the applications, built in; each WinMain gets its own name
APPS     = demo.o progman.o notepad.o clock.o calc.o control.o winfile.o taskman.o winmine.o

all: test_gdi winhost winhost800 HOSTWIN

test_gdi: $(GDICORE) $(HOST) test_gdi.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^

demo.o: ../../winapps/demo/demo.c
	$(CC) $(CFLAGS) $(INC) -DWinMain=demo_WinMain -c -o $@ $<

progman.o: ../../winapps/progman/progman.c
	$(CC) $(CFLAGS) $(INC) -DWinMain=progman_WinMain -c -o $@ $<

notepad.o: ../../winapps/notepad/notepad.c
	$(CC) $(CFLAGS) $(INC) -DWinMain=notepad_WinMain -c -o $@ $<

clock.o: ../../winapps/clock/clock.c
	$(CC) $(CFLAGS) $(INC) -DWinMain=clock_WinMain -c -o $@ $<

calc.o: ../../winapps/calc/calc.c
	$(CC) $(CFLAGS) $(INC) -DWinMain=calc_WinMain -c -o $@ $<

control.o: ../../winapps/control/control.c
	$(CC) $(CFLAGS) $(INC) -DWinMain=control_WinMain -c -o $@ $<

winfile.o: ../../winapps/winfile/winfile.c
	$(CC) $(CFLAGS) $(INC) -DWinMain=winfile_WinMain -c -o $@ $<

taskman.o: ../../winapps/taskman/taskman.c
	$(CC) $(CFLAGS) $(INC) -DWinMain=taskman_WinMain -c -o $@ $<

winmine.o: ../../winapps/winmine/winmine.c
	$(CC) $(CFLAGS) $(INC) -DWinMain=winmine_WinMain -c -o $@ $<

winhost: $(CORE) $(USER) $(HOST) host_main.c $(APPS)
	$(CC) $(CFLAGS) $(INC) -o $@ $^

# the VGA800 driver module, built in, with fake planar video memory
winhost800: $(CORE) $(USER) $(HOST) host_main.c $(APPS) ../../windrv/vga800/vga800.c
	$(CC) $(CFLAGS) $(INC) -DHOST_VGA800 -DWINDRV_HOST -o $@ $^

# the Windows directory of the tests: stand-ins for the application files
# (the applications themselves are built in), a DOS program and a text file
HOSTWIN:
	mkdir -p HOSTWIN
	touch HOSTWIN/DEMO.EXE HOSTWIN/PROGMAN.EXE HOSTWIN/NOTEPAD.EXE HOSTWIN/CLOCK.EXE HOSTWIN/CALC.EXE HOSTWIN/CONTROL.EXE HOSTWIN/WINFILE.EXE HOSTWIN/TASKMAN.EXE HOSTWIN/WINMINE.EXE HOSTWIN/WINN.EXE HOSTWIN/NC.EXE
	mkdir -p HOSTWIN/DOCS
	printf 'Notepad test file\n\tTabbed line\tand more\nThis is a rather long line that goes on and on past the right edge of the window so that horizontal scrolling is needed to see its end.\nLast line\n' > HOSTWIN/README.TXT
	mkdir -p HOSTWIN/DOCS/SUB
	cp HOSTWIN/README.TXT HOSTWIN/DOCS/README.TXT

clean:
	rm -rf test_gdi winhost winhost800 *.o *.ppm *.png HOSTWIN

.PHONY: all clean HOSTWIN
