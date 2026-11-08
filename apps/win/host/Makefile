# Host build of the native Windows core for snapshot tests (gcc, Linux).
# -m32: LPARAM/LONG carry pointers, as on the 32-bit target.
CC      ?= gcc
CFLAGS  ?= -m32 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined
INC      = -I../include -I../core -I../ddi -I../user -I.
CORE     = $(wildcard ../core/*.c)
USER     = $(wildcard ../user/*.c)
HOST     = host_drivers.c

all: test_gdi hello

test_gdi: $(CORE) $(HOST) test_gdi.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^

hello: $(CORE) $(USER) $(HOST) host_main.c ../samples/hello/hello.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^

clean:
	rm -f test_gdi hello *.ppm *.png
