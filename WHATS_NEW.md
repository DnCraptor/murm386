# What's new in 1.18.0

murm386 1.18.0 brings the 386 core back into the release build, adds ESP32 ZiModem and USB-drive integration, substantially expands joystick/mouse support, and adds new hardware and emulated AY/Sound Master audio paths.

- **286 + 386 release builds:** the Windows build wrappers now expose both CPU targets, and `build_all.bat` builds both matrices.
- **USB modem:** ESP32 ZiModem support through the emulated UART, with reset control and firmware flashing from murm386.
- **USB storage:** USB mass-storage devices can be exposed as emulated disks; GPT media can be mapped to the MBR-style view expected by the guest.
- **Disk ordering:** the SD-card disk can be disabled or placed first/last in BIOS disk order.
- **Audio:** hardware AY-3-8910 and AY8930 output plus Covox Sound Master AYDMA/DAC support.
- **Controllers:** more USB joystick types, multiple simultaneous USB joysticks, two NES pads, axis fixes, mouse-to-joystick mode and button swapping.
- **DOS/BIOS fixes:** floppy reporting, CHS handling, mouse INT 33h behaviour, FCOM line endings/variable expansion and several disk/DMA fixes.
- **Platform:** Z2 fixes, performance work and Pico SDK 2.3.1.

For the detailed change summary and build-matrix notes, see `RELEASE_NOTES-1.18.0.md`.
