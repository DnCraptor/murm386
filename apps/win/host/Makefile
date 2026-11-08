# Host build of the native Windows core for snapshot tests (gcc, Linux).
# -m32: LPARAM/LONG carry pointers, as on the 32-bit target.
CC      ?= gcc
CFLAGS  ?= -m32 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined
INC      = -I../include -I../core -I../ddi -I../user -I.
CORE     = $(wildcard ../core/*.c)
USER     = $(wildcard ../user/*.c)
HOST     = host_drivers.c
# GDI test: no USER, so no tasks, files and export tables
GDICORE  = $(filter-out ../core/exports.c ../core/task.c ../core/file.c ../core/logo.c \
            ../core/ne.c ../core/ne_boot.c ../core/ne_entry.c ../core/ne_exec.c \
            ../core/ne_guest.c ../core/ne_guest_api.c ../core/ne_guest_dos.c \
            ../core/x86bridge.c,$(CORE))

# the applications, built in; each WinMain gets its own name
APPS     = demo.o progman.o notepad.o clock.o calc.o control.o winfile.o taskman.o winmine.o appinfo.o

all: test_gdi test_x86bridge test_ne test_ne_image test_ne_guest test_ne_guest_api test_ne_boot test_ne_multi test_ne_entry test_ne_args winhost winhost800 HOSTWIN

test_gdi: $(GDICORE) $(HOST) test_gdi.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^

# The bridge has no pointer-sized Win16 types, so it also runs on a 64-bit host.
test_x86bridge: ../core/x86bridge.c test_x86bridge.c
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o $@ $^

# Self-contained NE parser and staged relocation regression tests.
test_ne: ../core/ne.c test_ne.c
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o $@ $^

test_ne_image: ../core/ne.c test_ne_image.c
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o $@ $^

test_ne_guest: ../core/ne.c ../core/ne_guest.c test_ne_guest.c
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o $@ $^

test_ne_guest_api: ../core/ne_guest_api.c test_ne_guest_api.c
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined $(INC) -o $@ $^

# NE with three safe Win16 ordinal imports; generator includes a real DOS MZ stub.
NEAPP-fixture.exe: mkne_winexec.py
	python3 mkne_winexec.py $@

test_ne_multi: ../core/ne.c ../core/ne_guest.c test_ne_multi.c NEAPP-fixture.exe
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o $@ ../core/ne.c ../core/ne_guest.c test_ne_multi.c

test_ne_entry: ../core/ne_entry.c test_ne_entry.c
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o $@ $^

# Checked Pascal stack, selector pointers and relocations in a two-DLL NE.
NEARGS-fixture.exe: mkne_args.py
	python3 mkne_args.py $@

test_ne_args: ../core/ne.c ../core/ne_guest.c ../core/ne_entry.c test_ne_args.c NEARGS-fixture.exe
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o $@ ../core/ne.c ../core/ne_guest.c ../core/ne_entry.c test_ne_args.c

# Real/Protected/Real machine-code emitter checks (64-bit host compatible).
test_ne_boot: ../core/ne_boot.c test_ne_boot.c
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o $@ $^

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

# their names and icons, from their CMakeLists.txt and .icon files
appinfo.c: mkappinfo.py $(wildcard ../../winapps/*/CMakeLists.txt) $(wildcard ../../winapps/*/*.icon)
	python3 mkappinfo.py ../../winapps > $@

appinfo.o: appinfo.c
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

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
	rm -rf test_gdi test_x86bridge test_ne test_ne_image test_ne_guest test_ne_guest_api test_ne_boot test_ne_multi test_ne_entry test_ne_args winhost winhost800 *.o *.ppm *.png appinfo.c HOSTWIN

.PHONY: all clean HOSTWIN
