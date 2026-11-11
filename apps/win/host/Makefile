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
            ../core/ne.c ../core/ne_boot.c ../core/ne_exec.c \
            ../core/ne_guest.c ../core/ne_guest_api.c ../core/ne_guest_dos.c ../core/ne_guest_user.c \
            ../core/ne_res.c ../core/ne_guest_int21.c ../core/ne_guest_call.c ../core/ne_guest_kernel.c \
            ../core/x86bridge.c ../core/windll.c ../core/sound.c,$(CORE))

# the applications, built in; each WinMain gets its own name
APPS     = demo.o progman.o notepad.o clock.o calc.o control.o winfile.o taskman.o winmine.o winhelp.o hlpfile.o appinfo.o
# the native DLLs, built in (host_dlls.c names them); each WinDllInit its own
DLLS     = lzexpand.o ver.o ddeml.o mmsystem.o commdlg.o olecli.o olesvr.o sound.o host_dlls.c

all: test_gdi test_x86bridge test_ne test_ne_image test_ne_guest test_ne_guest_api test_ne_boot test_ne_multi test_ne_launch test_ne_rm_gate test_ne_run NEFIXTURES winhost winhost800 winhostvga HOSTWIN

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

# NE programs for WINN on the target (each with a DOS MZ stub): ordinal
# imports, MessageBox, a counted loop, an invalid instruction, a hang; two
# built by Open Watcom.
NEAPP-fixture.exe: mkne_winexec.py
	python3 mkne_winexec.py $@
NEDIALOG.EXE: mkne_dialog.py
	python3 mkne_dialog.py $@
NELOOP-fixture.exe: mkne_loop.py
	python3 mkne_loop.py $@
NEFAULT.EXE: mkne_fault.py
	python3 mkne_fault.py $@
NEHANG.EXE: mkne_fault.py
	python3 mkne_fault.py --hang $@
NESTACK.EXE: mkne_fault.py
	python3 mkne_fault.py --stack $@
NECLI.EXE: mkne_fault.py
	python3 mkne_fault.py --cli $@
NEOUT.EXE: mkne_fault.py
	python3 mkne_fault.py --out $@
NECR0.EXE: mkne_fault.py
	python3 mkne_fault.py --cr0 $@
NEHLT.EXE: mkne_fault.py
	python3 mkne_fault.py --hlt $@
NEFPU.EXE: mkne_fpu.py
	python3 mkne_fpu.py $@
NEUNDEF.EXE: mkne_undef.py
	python3 mkne_undef.py $@

WINNTEST.FON: mkfon.py ../core/sysfont.c ../core/gdi_text.c
	python3 mkfon.py $@
WINNTEST.HLP: mkhlp.py
	python3 mkhlp.py $@
OWHELLO.EXE OWBUSY.EXE OWWIN.EXE OWMENU.EXE OWSEGS.EXE OWDEMO.EXE OWFILE.EXE OWFILEL.EXE OWRES.EXE OWKERN.EXE OWUSE.EXE \
	OWDLL.DLL OWDLL2.DLL OWFPU.EXE OWAPI.EXE OWGDI.EXE OWAPI2.EXE OWMETA.EXE OWSYS.EXE OWFONT.EXE OWHELP.EXE OWDLG.EXE OWMISC.EXE OWSND.EXE OW31.EXE: mkne_watcom.py
	python3 mkne_watcom.py $(basename $@) $@
NEFIXTURES: NEAPP-fixture.exe NEDIALOG.EXE NELOOP-fixture.exe NEFAULT.EXE NEHANG.EXE \
	NESTACK.EXE NECLI.EXE NEOUT.EXE NECR0.EXE NEHLT.EXE NEFPU.EXE NEUNDEF.EXE OWHELLO.EXE OWBUSY.EXE \
	OWWIN.EXE OWMENU.EXE OWSEGS.EXE OWDEMO.EXE OWFILE.EXE OWFILEL.EXE OWRES.EXE OWKERN.EXE \
	OWUSE.EXE OWDLL.DLL OWDLL2.DLL OWFPU.EXE OWAPI.EXE OWGDI.EXE OWAPI2.EXE OWMETA.EXE OWSYS.EXE OWFONT.EXE OWHELP.EXE OWDLG.EXE OWMISC.EXE OWSND.EXE OW31.EXE WINNTEST.FON WINNTEST.HLP

test_ne_multi: ../core/ne.c ../core/ne_guest.c test_ne_multi.c NEAPP-fixture.exe
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o $@ ../core/ne.c ../core/ne_guest.c test_ne_multi.c

test_ne_launch: ../core/ne.c test_ne_launch.c NELOOP-fixture.exe
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o $@ ../core/ne.c test_ne_launch.c

# Host-compiled ARM executor with mocked shared DOS CPU and real API marshaller.
test_ne_rm_gate: ../core/ne_exec.c ../core/ne_guest_api.c test_ne_rm_gate.c
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -D__arm__ -ffunction-sections -fdata-sections \
		-Wl,--gc-sections -fsanitize=address,undefined -Igate_mock -o $@ \
		../core/ne_guest_api.c test_ne_rm_gate.c

# NE programs end to end on the emulator's own i386 core (its source as is,
# the Pico SDK and the rest of the firmware stood in for by x86run/).
X86RUN_CFLAGS = -m32 -O1 -g -w -Ui386 -DNO_PAGING -DI386_MODE -include x86run/pico.h -Ix86run -I../../../src
i386-host.o: ../../../src/i386.c x86run/pico.h
	$(CC) $(X86RUN_CFLAGS) -c -o $@ $<
# and its coprocessor (WIN87EM's programs)
fpu-host.o: ../../../src/fpu.c x86run/pico.h
	$(CC) $(X86RUN_CFLAGS) -c -o $@ $<

X86RUN   = x86run/i386_host.c x86run/ne_host.c i386-host.o fpu-host.o
test_ne_run: test_ne_run.c $(X86RUN) ../core/ne_exec.c ../core/ne_guest.c ../core/ne.c \
		../core/ne_boot.c ../core/ne_guest_api.c ../core/x86bridge.c
	$(CC) -std=c11 -m32 -O1 -g -Wall -Wextra -Werror -DWINN_NE_HOST -Ix86run -o $@ test_ne_run.c \
		$(X86RUN) ../core/ne_exec.c ../core/ne_guest.c ../core/ne.c ../core/ne_boot.c \
		../core/ne_guest_api.c ../core/x86bridge.c -lm

# Real/Protected/Real machine-code emitter checks (64-bit host compatible).
test_ne_boot: ../core/ne_boot.c test_ne_boot.c
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -o $@ $^

# Host cursor regression is 64-bit safe and does not need native GDI.
all check: test_cursor_wait
test_cursor_wait: ../core/cursor.c test_cursor_wait.c
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined $(INC) -o $@ $^

# The tests that take a file
check: test_x86bridge test_ne test_ne_image test_ne_guest test_ne_guest_api test_ne_boot test_ne_multi test_ne_launch test_ne_rm_gate test_ne_run NEFIXTURES
	./test_x86bridge && ./test_ne && ./test_ne_image && ./test_ne_guest && ./test_ne_guest_api && \
	./test_ne_boot && ./test_ne_multi NEAPP-fixture.exe && ./test_ne_launch NELOOP-fixture.exe && \
	./test_ne_rm_gate && ./test_ne_run NEDIALOG.EXE NELOOP-fixture.exe NEFAULT.EXE NEHANG.EXE \
		NESTACK.EXE NECLI.EXE NEOUT.EXE NECR0.EXE NEHLT.EXE NEFPU.EXE NEUNDEF.EXE OWHELLO.EXE OWBUSY.EXE \
		OWSEGS.EXE
	./test_cursor_wait

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
winhelp.o: ../../winapps/winhelp/winhelp.c ../../winapps/winhelp/hlpfile.h
	$(CC) $(CFLAGS) $(INC) -DWinMain=winhelp_WinMain -c -o $@ $<
hlpfile.o: ../../winapps/winhelp/hlpfile.c ../../winapps/winhelp/hlpfile.h
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

lzexpand.o: ../../windll/lzexpand/lzexpand.c
	$(CC) $(CFLAGS) $(INC) -DWinDllInit=lzexpand_WinDllInit -c -o $@ $<
ver.o: ../../windll/ver/ver.c
	$(CC) $(CFLAGS) $(INC) -DWinDllInit=ver_WinDllInit -c -o $@ $<
ddeml.o: ../../windll/ddeml/ddeml.c ../include/ddeml.h ../ddi/windll.h
	$(CC) $(CFLAGS) $(INC) -DWinDllInit=ddeml_WinDllInit -c -o $@ $<
mmsystem.o: ../../windll/mmsystem/mmsystem.c ../include/mmsystem.h ../ddi/windll.h
	$(CC) $(CFLAGS) $(INC) -DWinDllInit=mmsystem_WinDllInit -c -o $@ $<
commdlg.o: ../../windll/commdlg/commdlg.c ../include/commdlg.h ../ddi/windll.h
	$(CC) $(CFLAGS) $(INC) -DWinDllInit=commdlg_WinDllInit -c -o $@ $<
olecli.o: ../../windll/olecli/olecli.c ../include/ole.h ../ddi/windll.h
	$(CC) $(CFLAGS) $(INC) -DWinDllInit=olecli_WinDllInit -c -o $@ $<
olesvr.o: ../../windll/olesvr/olesvr.c ../include/ole.h ../ddi/windll.h
	$(CC) $(CFLAGS) $(INC) -DWinDllInit=olesvr_WinDllInit -c -o $@ $<
sound.o: ../../windll/sound/sound.c ../include/sound.h ../ddi/windll.h
	$(CC) $(CFLAGS) $(INC) -DWinDllInit=sound_WinDllInit -c -o $@ $<

# their names and icons, from their CMakeLists.txt and .icon files
appinfo.c: mkappinfo.py $(wildcard ../../winapps/*/CMakeLists.txt) $(wildcard ../../winapps/*/*.icon)
	python3 mkappinfo.py ../../winapps > $@

appinfo.o: appinfo.c
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

# with Win16 programs, on the emulator's i386 core (as test_ne_run)
winhost: $(CORE) $(USER) $(HOST) host_main.c $(APPS) $(DLLS) $(X86RUN)
	$(CC) $(CFLAGS) $(INC) -DWINN_NE_HOST -Ix86run -o $@ $^ -lm

# the VGA800 driver module, built in, with fake planar video memory
winhost800: $(CORE) $(USER) $(HOST) host_main.c $(APPS) ../../windrv/vga800/vga800.c
	$(CC) $(CFLAGS) $(INC) -I. -I../drivers/dos -DHOST_VGA800 -DWINDRV_HOST -o $@ $^

# winhost with the built-in VGA driver (640x480 in 16 colours, the target's
# default), fake planar video memory
winhostvga: $(CORE) $(USER) $(HOST) host_main.c $(APPS) $(DLLS) $(X86RUN) ../drivers/dos/disp_vga.c
	$(CC) $(CFLAGS) $(INC) -I. -I../drivers/dos -DHOST_VGA -DWINDRV_HOST -DWINN_NE_HOST -Ix86run -o $@ $^ -lm

# the Windows directory of the tests: stand-ins for the application files
# (the applications themselves are built in), a DOS program and a text file
HOSTWIN: NEFIXTURES
	mkdir -p HOSTWIN
	cp NEDIALOG.EXE NEFAULT.EXE NEHANG.EXE OWHELLO.EXE OWBUSY.EXE OWWIN.EXE OWMENU.EXE OWSEGS.EXE \
		OWDEMO.EXE OWFILE.EXE OWFILEL.EXE OWRES.EXE OWKERN.EXE OWUSE.EXE OWDLL.DLL OWDLL2.DLL OWFPU.EXE \
		OWAPI.EXE OWGDI.EXE OWAPI2.EXE OWMETA.EXE OWSYS.EXE OWFONT.EXE OWHELP.EXE OWDLG.EXE OWMISC.EXE OWSND.EXE OW31.EXE WINNTEST.FON WINNTEST.HLP HOSTWIN/
	cp ../WIN.INI HOSTWIN/
	touch HOSTWIN/DEMO.EXE HOSTWIN/PROGMAN.EXE HOSTWIN/NOTEPAD.EXE HOSTWIN/CLOCK.EXE HOSTWIN/CALC.EXE HOSTWIN/CONTROL.EXE HOSTWIN/WINFILE.EXE HOSTWIN/TASKMAN.EXE HOSTWIN/WINMINE.EXE HOSTWIN/WINN.EXE HOSTWIN/NC.EXE
	mkdir -p HOSTWIN/DOCS
	printf 'Notepad test file\n\tTabbed line\tand more\nThis is a rather long line that goes on and on past the right edge of the window so that horizontal scrolling is needed to see its end.\nLast line\n' > HOSTWIN/README.TXT
	mkdir -p HOSTWIN/DOCS/SUB
	cp HOSTWIN/README.TXT HOSTWIN/DOCS/README.TXT

clean:
	rm -rf test_gdi test_x86bridge test_ne test_ne_image test_ne_guest test_ne_guest_api test_ne_boot test_ne_multi test_ne_launch test_ne_rm_gate test_ne_run i386-host.o *-fixture.exe NEDIALOG.EXE NEFAULT.EXE NEHANG.EXE NESTACK.EXE NECLI.EXE NEOUT.EXE NECR0.EXE NEHLT.EXE NEFPU.EXE NEUNDEF.EXE OWHELLO.EXE OWBUSY.EXE OWWIN.EXE OWMENU.EXE OWSEGS.EXE OWDEMO.EXE OWDEMO.TMP OWFILE.EXE OWFILEL.EXE OWRES.EXE OWKERN.EXE OWUSE.EXE OWDLL.DLL OWDLL2.DLL OWFPU.EXE OWAPI.EXE OWGDI.EXE OWAPI2.EXE OWMETA.EXE OWSYS.EXE OWFONT.EXE OWHELP.EXE OWDLG.EXE OWMISC.EXE OWSND.EXE OW31.EXE WINNTEST.FON WINNTEST.HLP winhost winhost800 winhostvga *.o *.ppm *.png appinfo.c HOSTWIN

.PHONY: all check clean HOSTWIN NEFIXTURES

clean: clean_cursor_wait
clean_cursor_wait:
	rm -f test_cursor_wait
.PHONY: clean_cursor_wait
