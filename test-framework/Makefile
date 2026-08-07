#############################################################
# rNES headless test harness
#
# Builds `rnes_headless` by linking the SDL-free emulator core from ../src
# together with the null HAL backend and the headless runner in harness/.
# No SDL, no X server, no audio device required -> ideal for CI.
#############################################################
CC ?= gcc

SRC := ../src

# Emulator core (same list as $(SRC)/Makefile, WITHOUT interface/posix/rNES.c
# and WITHOUT gui/sdl/* -- those are replaced by harness/null_hal.c).
CORE_SRCS := \
	$(SRC)/cpu/cpu650x.c \
	$(SRC)/sbus.c \
	$(SRC)/romdec.c \
	$(SRC)/palette.c \
	$(SRC)/cartridge.c \
	$(SRC)/controller.c \
	$(SRC)/ppu.c \
	$(SRC)/apu.c \
	$(SRC)/mappers/mappers.c \
	$(SRC)/mappers/m0_NROM.c \
	$(SRC)/mappers/m1_SxROM.c \
	$(SRC)/mappers/m2_UxROM.c \
	$(SRC)/mappers/m3_CNROM.c \
	$(SRC)/mappers/m4_MMC3.c \
	$(SRC)/mappers/m7_AxROM.c \
	$(SRC)/hal/fileops.c \
	$(SRC)/ringbuffer.c

HARNESS_SRCS := harness/null_hal.c harness/rnes_headless.c

SRCS := $(CORE_SRCS) $(HARNESS_SRCS)

# Same feature flags as the main build.
CFLAGS ?= -O2 -Wall
CFLAGS += -I$(SRC)/include -I$(SRC)/cpu/include -Iharness \
		  -DSBUS_MEM -DCPU_THREAD_SAFE -DAPU_LINEAR_DAC_OUTPUT \
		  -DRNES_VERSION=\"test\"

LDFLAGS += -lpthread -lrt -lm

TARGET := rnes_headless

.PHONY: all clean

all: $(TARGET)

# One-shot build keeps object files out of the source tree (the main build
# owns the .o files under ../src). The core is small; this is fast enough.
$(TARGET): $(SRCS) harness/null_hal.h
	$(CC) $(CFLAGS) $(CORE_SRCS) $(HARNESS_SRCS) $(LDFLAGS) -o $(TARGET)

clean:
	@rm -f $(TARGET)
