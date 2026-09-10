CIRCLEHOME = /home/lbijn/circle

# Compiler optimalisaties toevoegen
DEFINE += -O3 -ffast-math -fno-math-errno

INCLUDE += -I $(CIRCLEHOME)/addon -I $(CIRCLEHOME)/addon/fatfs

OBJS = kernel.o mandelbrot.o

LIBS = $(CIRCLEHOME)/lib/usb/libusb.a \
       $(CIRCLEHOME)/lib/input/libinput.a \
       $(CIRCLEHOME)/lib/fs/libfs.a \
       $(CIRCLEHOME)/lib/libcircle.a

include ../Rules.mk

-include $(DEPS)