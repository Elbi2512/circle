CIRCLEHOME = /home/lbijn/circle

INCLUDE += -I $(CIRCLEHOME)/addon -I $(CIRCLEHOME)/addon/fatfs

OBJS = main.o kernel.o beeb.o via6522.o 

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

-include $(DEPS)