/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rNES - Nintendo Entertainment System Emulator
 * Copyright 2021-2026 Renê de Souza Pinto
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * @file palette.h
 * @see palette.c
 */
#ifndef __PALETTE_H__
#define __PALETTE_H__

#include "hal/hal.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

/** Number of entries in the palette */
#define PALETTE_TAB_SIZE 64

/**
 * NES PPU color palette
 * The palette consists of 64 RGB colors
 */
struct _color_palette {
	union {
		RGB_t color[PALETTE_TAB_SIZE];
		uint8_t raw[PALETTE_TAB_SIZE * sizeof(RGB_t)];
	};
} __attribute__((packed));

/** Palette type */
typedef struct _color_palette palette_t;

/** Default color palette */
extern palette_t *default_palette;
/** Global system's color palette */
extern palette_t *sys_palette;

/**
 * Set the system's palette
 * @param [in] palette Pointer to the new palette
 * @note This function is not thread safe! It just sets the pointer to the
 * new palette
 * @return int 0 on success, error code otherwise
 */
static inline int set_palette(palette_t *palette)
{
	if (!palette)
		return -EINVAL;
	sys_palette = palette;
	return 0;
}

/**
 * Get current system's color palette
 * @return System's color palette
 */
static inline palette_t *get_palette() { return sys_palette; }

/**
 * Get default color palette
 * @return Default color palette
 */
static inline palette_t *get_default_palette() { return default_palette; }

/**
 * Unload a color palette from memory
 * @param [in,out] palette Color palette
 */
static inline void unload_palette(palette_t *palette) { free(palette); }

int load_palette(const char *pathname, palette_t **palette);

#endif /* __PALETTE_H__ */
