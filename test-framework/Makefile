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
	$(SRC)/mappers/m11_ColorDreams.c \
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

# --- Test ROM suite (blargg's NES tests + nestest) -------------------------
# The ROMs are fetched rather than vendored -- neither suite carries a licence
# (see roms/README.md) -- so `testroms` downloads them first, verifying each
# against the SHA-256 in testroms.json.
#
# Self-checking ROMs need flags a game sweep does not: --test-mode auto reads
# the verdict blargg's ROMs publish at $6000 instead of guessing from the
# screen (a finished test parks the CPU in a `jmp *`, which the heuristics
# read as a hang), and --no-input keeps the menu masher away from ROMs that
# drive themselves. The frame budget only bounds a ROM that never reports:
# a run stops the moment one publishes its verdict.
TESTROM_FLAGS := --rom-list testroms-ci.txt \
	--test-mode auto --no-input \
	--frames 5400 --warmup 30 --interval 30 --timeout 180

.PHONY: all clean sanitize strict fetch-testroms testroms testroms-baseline

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

# Download the test ROMs (idempotent: already-correct files are left alone).
fetch-testroms:
	./fetch-testroms.py

# Run the test ROM suite, gated on testroms-baseline.json. TESTROM_EXTRA is
# how CI adds its reporting flags without restating the run's flags:
#   make testroms TESTROM_EXTRA="--json r.json --junit r.xml --github"
testroms: $(TARGET) fetch-testroms
	python3 run_tests.py $(TESTROM_FLAGS) --baseline testroms-baseline.json \
		$(TESTROM_EXTRA)

# Re-record the expected verdicts. Run this after a change moves a test ROM
# from FAIL to PASS, and commit the updated baseline with the fix.
testroms-baseline: $(TARGET) fetch-testroms
	python3 run_tests.py $(TESTROM_FLAGS) --baseline testroms-baseline.json \
		--update-baseline

clean:
	@rm -f $(TARGET)
