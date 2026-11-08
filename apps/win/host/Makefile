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
APPS     = demo.o progman.o

all: test_gdi winhost winhost800 hostwin

test_gdi: $(GDICORE) $(HOST) test_gdi.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^

demo.o: ../../winapps/demo/demo.c
	$(CC) $(CFLAGS) $(INC) -DWinMain=demo_WinMain -c -o $@ $<

progman.o: ../../winapps/progman/progman.c
	$(CC) $(CFLAGS) $(INC) -DWinMain=progman_WinMain -c -o $@ $<

winhost: $(CORE) $(USER) $(HOST) host_main.c $(APPS)
	$(CC) $(CFLAGS) $(INC) -o $@ $^

# the VGA800 driver module, built in, with fake planar video memory
winhost800: $(CORE) $(USER) $(HOST) host_main.c $(APPS) ../../windrv/vga800/vga800.c
	$(CC) $(CFLAGS) $(INC) -DHOST_VGA800 -DWINDRV_HOST -o $@ $^

# the Windows directory of the tests: stand-ins for the application files
# (the applications themselves are built in) and a DOS program
hostwin:
	mkdir -p hostwin
	touch hostwin/DEMO.EXE hostwin/PROGMAN.EXE hostwin/WINN.EXE hostwin/NC.EXE

clean:
	rm -rf test_gdi winhost winhost800 *.o *.ppm *.png hostwin

.PHONY: all clean hostwin
