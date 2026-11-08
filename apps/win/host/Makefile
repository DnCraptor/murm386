# Host build of the native Windows core for snapshot tests (gcc, Linux).
# -m32: LPARAM/LONG carry pointers, as on the 32-bit target.
CC      ?= gcc
CFLAGS  ?= -m32 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined
INC      = -I../include -I../core -I../ddi -I../user -I.
CORE     = $(wildcard ../core/*.c)
USER     = $(wildcard ../user/*.c)
HOST     = host_drivers.c
# GDI test: no USER, so no tasks and no export tables
GDICORE  = $(filter-out ../core/exports.c ../core/task.c,$(CORE))

all: test_gdi demo demo800

test_gdi: $(GDICORE) $(HOST) test_gdi.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^

DEMO     = ../../winapps/demo/demo.c

demo: $(CORE) $(USER) $(HOST) host_main.c $(DEMO)
	$(CC) $(CFLAGS) $(INC) -DWinMain=demo_WinMain -o $@ $^

clean:
	rm -f test_gdi demo demo800 *.ppm *.png

# the VGA800 driver module, built in, with fake planar video memory
demo800: $(CORE) $(USER) $(HOST) host_main.c $(DEMO) ../../windrv/vga800/vga800.c
	$(CC) $(CFLAGS) $(INC) -DWinMain=demo_WinMain -DHOST_VGA800 -DWINDRV_HOST -o $@ $^
