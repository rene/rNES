/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rNES test framework - headless HAL backend
 * Copyright 2021-2026 Renê de Souza Pinto
 *
 * A minimal, SDL-free implementation of the rNES HAL/GUI/audio/clock layer.
 * It replaces src/gui/sdl/ so the emulator core can be linked into a headless
 * test binary. Video is captured into an in-RAM framebuffer and audio samples
 * are discarded by a drain thread (so the APU ring buffer never blocks the
 * emulation loop).
 */
#ifndef __NULL_HAL_H__
#define __NULL_HAL_H__

#include <stdint.h>

/** Native NES screen dimensions (must match src/gui/sdl/gui.c) */
#define NULL_SCREEN_WIDTH 256
#define NULL_SCREEN_HEIGHT 240

/**
 * Return a pointer to the captured framebuffer.
 * Layout is row-major ARGB8888 (0xAARRGGBB), NULL_SCREEN_WIDTH *
 * NULL_SCREEN_HEIGHT pixels, exactly as written by gui_set_pixel().
 */
const uint32_t *null_framebuffer(void);

/**
 * Start the background audio drain thread.
 * Must be called after apu_init()/apu_reset() so the ring buffer is ready.
 * The thread continuously consumes samples via apu_get_samples() so that
 * apu_clock()'s blocking rbuff_put() never stalls the emulation loop.
 */
int null_audio_start_drain(void);

/**
 * Set the raw controller-1 button byte returned by gui_read_joypad().
 * The byte uses the same layout the SDL backend produces (controller_reg_t):
 * this is what the emulated game latches on a $4016 strobe. Controller 2 is
 * always reported as "no buttons".
 */
void null_set_joypad(uint8_t raw);

#endif /* __NULL_HAL_H__ */
