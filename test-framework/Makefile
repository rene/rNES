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

# Extra checking, kept off the default build so a 790-ROM sweep stays fast.
#   make sanitize  -- rebuild under ASan+UBSan and run ROMs through it
#   make strict    -- compile the harness with warnings as errors
SAN_CFLAGS := -g -fno-omit-frame-pointer -fsanitize=address,undefined
SAN_LDFLAGS := -fsanitize=address,undefined

# -Werror is scoped to the sources this directory owns. The emulator core
# still has warnings of its own (e.g. -Wmaybe-uninitialized in ppu.c), so
# applying it to CORE_SRCS would fail on unrelated pre-existing issues.
STRICT_CFLAGS := -Wall -Wextra -Werror

TARGET := rnes_headless

.PHONY: all clean sanitize strict

all: $(TARGET)

# One-shot build keeps object files out of the source tree (the main build
# owns the .o files under ../src). The core is small; this is fast enough.
# Recursively expanded so the target-specific flags below take effect.
BUILD = $(CC) $(CFLAGS) $(CORE_SRCS) $(HARNESS_SRCS) $(LDFLAGS) -o $(TARGET)

$(TARGET): $(SRCS) harness/null_hal.h
	$(BUILD)

# An instrumented harness: same binary, same CLI, ~2-3x slower. Run a handful
# of ROMs through it (not the full sweep) to catch memory and UB errors in the
# core that a PASS/FAIL verdict cannot see.
sanitize: CFLAGS += $(SAN_CFLAGS)
sanitize: LDFLAGS += $(SAN_LDFLAGS)
sanitize:
	@rm -f $(TARGET)
	$(BUILD)

# Warnings-as-errors over harness/ only. Syntax-only, so it is a fast check
# suitable for every CI run.
strict:
	$(CC) -fsyntax-only $(STRICT_CFLAGS) $(CFLAGS) $(HARNESS_SRCS)

clean:
	@rm -f $(TARGET)
