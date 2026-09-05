CIRCLEHOME = /home/lbijn/circle

INCLUDE += -I $(CIRCLEHOME)/addon -I $(CIRCLEHOME)/addon/fatfs

OBJS = main.o kernel.o beeb.o circle_stubs.o saa5050_font.o

OBJS += bbc_machine.o bbc_memory.o bbc_cpu.o bbc_video.o bbc_video_ula.o \
        mc6845.o wd1770.o bbc_sysvia.o bbc_uservia.o bbc_tape.o \
        sn76489.o saa5050.o vrEmu6502.o m6522.o

OBJS += os12_rom.o basic2_rom.o dfs1770_rom.o

ADDON_DIR = $(CIRCLEHOME)/addon/SDCard

LIBS := $(CIRCLEHOME)/addon/wlan/libwlan.a \
        $(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant/libwpa_supplicant.a \
        $(CIRCLEHOME)/lib/net/libnet.a \
        $(CIRCLEHOME)/lib/sched/libsched.a \
        $(ADDON_DIR)/libsdcard.a \
        $(CIRCLEHOME)/addon/fatfs/libfatfs.a \
        $(CIRCLEHOME)/lib/fs/fat/libfatfs.a \
        $(CIRCLEHOME)/lib/fs/libfs.a \
        $(CIRCLEHOME)/lib/usb/libusb.a \
        $(CIRCLEHOME)/lib/input/libinput.a \
        $(CIRCLEHOME)/lib/sound/libsound.a \
        $(CIRCLEHOME)/lib/libcircle.a

include $(CIRCLEHOME)/Rules.mk

# Regels om binaire ROMs vanuit rom/ om te zetten naar linkbare objectbestanden (AArch64)
os12_rom.o: rom/os12.rom
	cd rom && $(OBJCOPY) -I binary -B aarch64 -O elf64-littleaarch64 os12.rom ../$@

basic2_rom.o: rom/basic2.rom
	cd rom && $(OBJCOPY) -I binary -B aarch64 -O elf64-littleaarch64 basic2.rom ../$@

dfs1770_rom.o: rom/dfs1770.rom
	cd rom && $(OBJCOPY) -I binary -B aarch64 -O elf64-littleaarch64 dfs1770.rom ../$@

-include $(DEPS)