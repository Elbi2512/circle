CIRCLEHOME = /home/lbijn/circle

CPPFLAGS += -I$(CIRCLEHOME)/include
CPPFLAGS += -I$(CIRCLEHOME)/include/circle
CPPFLAGS += -I$(CIRCLEHOME)/include/circle/fs
CPPFLAGS += -I$(CIRCLEHOME)/addon/SDCard
CPPFLAGS += -I$(CIRCLEHOME)/addon/fatfs

OBJS = main.o kernel.o atomemulator.o via6522.o pia8255.o AtomMC.o AtomVideo.o debugger.o textconsole.o

ADDON_DIR = $(CIRCLEHOME)/addon/SDCard

LIBS = $(ADDON_DIR)/libsdcard.a \
       $(CIRCLEHOME)/addon/fatfs/libfatfs.a \
       $(CIRCLEHOME)/lib/fs/fat/libfatfs.a \
       $(CIRCLEHOME)/lib/fs/libfs.a \
       $(CIRCLEHOME)/lib/usb/libusb.a \
       $(CIRCLEHOME)/lib/input/libinput.a \
       $(CIRCLEHOME)/lib/sound/libsound.a \
       $(CIRCLEHOME)/lib/libcircle.a

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)