#############################################################
CC := gcc
CLANG_FORMAT=$(shell which clang-format)

RNES_VERSION ?= \"1.0\"

LDFLAGS = -lpthread
CFLAGS = -Wall -I$(PWD)/include/ -I$(PWD)/cpu/include/ \
		 -DSBUS_MEM -DCPU_THREAD_SAFE -DAPU_LINEAR_DAC_OUTPUT -DRNES_VERSION=$(RNES_VERSION)

executable = rNES
sources = cpu/cpu650x.c sbus.c romdec.c palette.c \
		  cartridge.c controller.c ppu.c apu.c \
		  mappers/mappers.c \
		  mappers/m0_NROM.c \
		  mappers/m1_SxROM.c \
		  mappers/m2_UxROM.c \
		  mappers/m3_CNROM.c \
		  mappers/m4_MMC3.c \
		  mappers/m7_AxROM.c \
		  hal/fileops.c ringbuffer.c \
		  interface/posix/rNES.c \

## GUI options
gui_objects = gui/sdl/gui.o gui/sdl/audio.o gui/sdl/clock.o
gui_sources = gui/sdl
CFLAGS += $(shell pkg-config --cflags sdl2 SDL2_ttf)
LDFLAGS += $(shell pkg-config --libs sdl2 SDL2_ttf)
#############################################################

export CC
export CFLAGS
export LDFLAGS

.PHONY: clean gui gui-clean docs format-src help

##
# General rules
#

objects = $(sources:.c=.o)

all: $(executable)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(executable) : $(objects) gui
	$(CC) $(objects) $(gui_objects) $(LDFLAGS) -o $(executable)

.Makefile.dep: *.c
	@$(CC) $(CFLAGS) $(CPP_FLAGS) -MM *.c > $@

-include .Makefile.dep

##
# docs
#
docs:
	$(MAKE) -C docs/

##
# format-src
#
format-src:
	@$(CLANG_FORMAT) -Werror --dry-run $(shell find gui/ hal/ include/ interface/ mappers/ *.[ch] -type f -name "*.[ch]")

##
# GUI
#
gui:
	$(MAKE) -C $(gui_sources)

gui-clean:
	$(MAKE) -C $(gui_sources) clean

##
# help
#
help:
	@echo "all        : Build rNES for POSIX systems using SDL library"
	@echo "docs       : Build doxygen documentation from source code"
	@echo "clean      : Clean up build files"
	@echo "format-src : Check coding style"
	@echo "help       : Show this help"

##
# clean
#
clean: gui-clean
	@rm -f $(objects) \#* *~  .Makefile.dep
	@rm -f $(executable)
	$(MAKE) -C docs/ clean

