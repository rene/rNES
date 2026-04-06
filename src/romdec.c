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
 * @file romdec.c
 *
 * It provides all the functions to handle ROM files of iNES and NES 2.0
 * format.
 */
#include "romdec.h"
#include "hal/hal.h"
#include <errno.h>
#include <stdlib.h>

/**
 * Load ROM file
 * @param [in] pathname Pathname to the ROM file
 * @param [out] rom ROM struct
 * @return int 0 on success, error code otherwise
 */
int load_rom(const char *pathname, rom_t **rom)
{
	ssize_t ret;
	int sflags, exp, mul;
	uint8_t *rom_file = NULL;
	rom_t *romf = NULL;

	/* Allocate ROM struct */
	romf = malloc(sizeof(rom_t));
	if (romf == NULL)
		return -ENOMEM;

	/* Load the ROM file */
	ret = load_file(pathname, &rom_file);
	if (ret < 0) {
		free(romf);
		*rom = NULL;
		return ret;
	}

	/* ROM header */
	romf->header = (rom_header_t *)rom_file;

	/* Detect ROM file */
	if (!(romf->header->magic[0] == 'N' && romf->header->magic[1] == 'E' &&
		  romf->header->magic[2] == 'S' && romf->header->magic[3] == 0x1a)) {
		/* Not a ROM file */
		free(romf);
		free(rom_file);
		*rom = NULL;
		return -EINVAL;
	}

	/* Detect ROM file format */
	sflags = romf->header->flag12.raw + romf->header->flag13.raw +
			 romf->header->flag14.raw + romf->header->flag15.raw;
	if ((romf->header->flag7.raw & 0xc) == 0 && sflags == 0)
		romf->rom_fmt = INES_FMT;
	else if ((romf->header->flag7.raw & 0xc) == 0xc)
		romf->rom_fmt = NES_FMT;
	else
		romf->rom_fmt = UNKNOWN_FMT;

	/* ROM Mirroring */
	romf->mirroring = romf->header->flag6.nes.mirroring;

	/* Calculate PRG ROM and CHR ROM size */
	if (romf->rom_fmt == INES_FMT) {
		romf->prg_size = romf->header->prg_size * (16 * 1024);
		romf->chr_size = romf->header->chr_size * (8 * 1024);
	} else {
		if (romf->header->flag9.nes.prgrom_size < 0xf) {
			romf->prg_size =
				((romf->header->flag9.nes.prgrom_size << 4) & 0xf0) |
				(romf->header->prg_size & 0x0f);
			romf->prg_size *= (16 * 1024);
		} else {
			/* Get exponent and multiplier */
			exp = ((romf->header->prg_size & 0xfc) >> 2);
			mul = ((romf->header->prg_size & 0x3) * 2) + 1;
			/* Calculate total size in bytes */
			romf->prg_size = (1 << exp) * mul;
		}

		if (romf->header->flag9.nes.chrrom_size < 0xf) {
			romf->chr_size =
				((romf->header->flag9.nes.chrrom_size << 4) & 0xf0) |
				(romf->header->prg_size & 0x0f);
			romf->chr_size *= (8 * 1024);
		} else {
			/* Get exponent and multiplier */
			exp = ((romf->header->chr_size & 0xfc) >> 2);
			mul = ((romf->header->chr_size & 0x3) * 2) + 1;
			/* Calculate total size in bytes */
			romf->chr_size = (1 << exp) * mul;
		}
	}

	/* Find pointers for PRG ROM, CHR ROM and Trainer (if present). When
	 * Trainer area is present, it follows the ROM header and it's always
	 * 512 bytes in size.
	 */
	if (romf->header->flag6.nes.trainer == 1) {
		romf->trainer = (uint8_t *)romf->header + sizeof(rom_header_t);
		romf->prg_rom = romf->trainer + 512;
	} else {
		romf->trainer = NULL;
		romf->prg_rom = (uint8_t *)romf->header + sizeof(rom_header_t);
	}
	romf->chr_rom = romf->prg_rom + romf->prg_size;

	/* Check mapper number (if present) */
	if (romf->rom_fmt == INES_FMT) {
		romf->mapper = ((romf->header->flag7.ines.mapper << 4) & 0xf0) |
					   (romf->header->flag6.nes.mapper & 0xf);
	} else {
		romf->mapper = ((romf->header->flag8.raw << 8) & 0xff00) |
					   ((romf->header->flag7.nes.mapper << 4) & 0xf0) |
					   (romf->header->flag6.nes.mapper & 0xf);
	}

	*rom = romf;
	return 0;
}

/**
 * Unload ROM from the memory
 * @param [in,out] rom ROM struct
 * @return in 0 on success, error code otherwise
 */
int unload_rom(rom_t *rom)
{
	free(rom->header);
	free(rom);
	rom = NULL;
	return 0;
}

/**
 * Return ROM file format
 * @param [in] rom ROM struct
 * @return INES_FMT, NES_FMT or UNKNOWN_FMT
 */
rom_fmt_t get_rom_format(rom_t *rom)
{
	if (rom == NULL)
		return UNKNOWN_FMT;
	else
		return rom->rom_fmt;
}

/**
 * Return the default ROM mirroring mode
 * @param [in] rom ROM struct
 * @return HORIZONTAL_MIRRORING, VERTICAL_MIRRORING or FOUR_SCREEN
 */
rom_mirroring_t get_rom_mirroring(rom_t *rom)
{
	rom_mirroring_t m = HORIZONTAL_MIRRORING;

	if (rom == NULL)
		return m;

	/* First we check the Nametable arrangement */
	if (rom->header->flag6.nes.mirroring == 1)
		m = VERTICAL_MIRRORING;

	/* It might be a 4 screen game */
	if (rom->header->flag6.nes.fourscreen == 1)
		m = FOUR_SCREEN;

	return m;
}

/**
 * Return ROM video standard
 * @param [in] rom ROM struct
 * @return ROM_NTSC, ROM_PAL_M, ROM_MULTIPLE or ROM_DENDY
 */
rom_video_t get_rom_video_std(rom_t *rom)
{
	if (rom == NULL)
		return ROM_NTSC;

	if (rom->rom_fmt == INES_FMT) {
		if (rom->header->flag9.ines.TV_system == 0)
			return ROM_NTSC;
		else
			return ROM_PAL_M;
	} else {
		switch (rom->header->flag12.nes.cpu_ppu_timing) {
		case 0:
			return ROM_NTSC;
		case 1:
			return ROM_PAL_M;
		case 2:
			return ROM_MULTIPLE;
		case 3:
			return ROM_DENDY;
		default:
			return ROM_NTSC;
		}
	}
}
