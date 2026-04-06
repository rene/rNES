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
 * @file palette.c
 *
 * It provides all the functions to handle color palettes.
 */
#include "palette.h"
#include "hal/hal.h"

/**
 * Default palette: 2C02 (NTSC)
 */
palette_t _default_palette = {
	.color =
		{
			{0x55, 0x55, 0x55}, {0x00, 0x11, 0x6d}, {0x03, 0x00, 0x7e},
			{0x25, 0x00, 0x71}, {0x41, 0x00, 0x49}, {0x4f, 0x00, 0x12},
			{0x4b, 0x00, 0x00}, {0x37, 0x0a, 0x00}, {0x17, 0x1d, 0x00},
			{0x00, 0x2c, 0x00}, {0x00, 0x33, 0x00}, {0x00, 0x2f, 0x09},
			{0x00, 0x23, 0x42}, {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00},
			{0x00, 0x00, 0x00}, {0xa5, 0xa5, 0xa5}, {0x09, 0x4e, 0xbc},
			{0x31, 0x34, 0xda}, {0x5e, 0x1f, 0xcf}, {0x85, 0x15, 0xa0},
			{0x9b, 0x17, 0x58}, {0x9a, 0x26, 0x0c}, {0x82, 0x3d, 0x00},
			{0x5a, 0x57, 0x00}, {0x2d, 0x6c, 0x00}, {0x06, 0x77, 0x00},
			{0x00, 0x74, 0x33}, {0x00, 0x65, 0x7f}, {0x00, 0x00, 0x00},
			{0x00, 0x00, 0x00}, {0x00, 0x00, 0x00}, {0xfe, 0xff, 0xff},
			{0x55, 0xa1, 0xff}, {0x6b, 0x78, 0xff}, {0x93, 0x67, 0xff},
			{0xd4, 0x6b, 0xff}, {0xf2, 0x6d, 0xbe}, {0xf5, 0x7a, 0x6f},
			{0xe0, 0x90, 0x2d}, {0xba, 0xab, 0x09}, {0x8c, 0xc1, 0x0d},
			{0x63, 0xce, 0x38}, {0x49, 0xce, 0x7d}, {0x46, 0xc1, 0xcc},
			{0x3c, 0x3c, 0x3c}, {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00},
			{0xfe, 0xff, 0xff}, {0xb7, 0xdb, 0xff}, {0xb9, 0xc4, 0xff},
			{0xca, 0xba, 0xff}, {0xe7, 0xbf, 0xff}, {0xf8, 0xc2, 0xe9},
			{0xfb, 0xc7, 0xc9}, {0xf4, 0xcf, 0xac}, {0xe5, 0xda, 0x9b},
			{0xd2, 0xe4, 0x9a}, {0xc1, 0xea, 0xa9}, {0xb5, 0xeb, 0xc4},
			{0xb2, 0xe6, 0xe4}, {0xaf, 0xaf, 0xaf}, {0x00, 0x00, 0x00},
			{0x00, 0x00, 0x00},
		},
};
palette_t *default_palette = &_default_palette;

/** Global system's color palette */
palette_t *sys_palette = &_default_palette;

/**
 * Load palette from a file
 * @param [in] pathname Pathname to the palette file
 * @param [out] palette Palette struct
 * @return int 0 on success, error code otherwise
 */
int load_palette(const char *pathname, palette_t **palette)
{
	ssize_t fsize;
	uint8_t *pal_file = NULL;

	/* Load the file */
	fsize = load_file(pathname, &pal_file);
	if (fsize < 0)
		return fsize;

	/* Now, check the size */
	if (fsize < sizeof(_default_palette)) {
		free(pal_file);
		return -EINVAL;
	}

	/* We don't care if the file is bigger than the palette struct, so
	 * let's just cast it
	 */
	*palette = (palette_t *)pal_file;

	return 0;
}
