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
 * A cartridge whose iNES header does not describe the board it was dumped
 * from. Unlicensed titles are the usual offenders: the header was filled in
 * by hand (or by a tool that guessed) long after the fact, and nothing in the
 * image itself contradicts it, so the only way to tell is to recognise the
 * dump. Entries are keyed by the CRC-32 of the image with the 16 byte header
 * removed, which is the key the No-Intro and NesCartDB databases use.
 */
struct _rom_fixup {
	/** CRC-32 of the ROM image, header excluded */
	uint32_t crc;
	/** Mapper the board really implements */
	uint16_t mapper;
	/** Real PRG ROM size in bytes */
	uint64_t prg_size;
	/** Real CHR ROM size in bytes */
	uint64_t chr_size;
	/** Real nametable arrangement */
	enum _rom_mirroring mirroring;
	/** Title of the dump, for the reader of this table */
	const char *title;
};

/** Known bad headers, and what the cartridge behind them actually is */
static const struct _rom_fixup rom_fixups[] = {
	/* Headed as an MMC1 board with 32 KB of PRG ROM; it is really a Color
	 * Dreams board carrying 64 KB. Color Dreams swaps the whole of
	 * $8000-$ffff on a single write, so under MMC1 rules the bank swap never
	 * happens: the game calls into its second bank, runs whatever bank 0
	 * holds at that address instead, unbalances the stack and ends up in the
	 * IRQ vector, which this game points at its own reset path. It reboots
	 * itself a few frames in, forever, on a black screen.
	 */
	{0xcb53c523, 11, 0x10000, 0x8000, VERTICAL_MIRRORING,
	 "King Neptune's Adventure (USA) (Unl)"},
};

/**
 * Compute the CRC-32 (IEEE 802.3) of a memory block
 * @param [in] data Data block
 * @param [in] len Size of the block in bytes
 * @return uint32_t CRC-32 of the block
 */
static uint32_t crc32_block(const uint8_t *data, size_t len)
{
	uint32_t crc = 0xffffffff;
	size_t i;
	int bit;

	for (i = 0; i < len; i++) {
		crc ^= data[i];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
	}

	return ~crc;
}

/**
 * Replace the header-derived geometry of a ROM known to be mislabelled.
 * Nothing happens unless the image matches an entry of rom_fixups[] byte for
 * byte, so a correctly headed dump is never second-guessed.
 * @param [in,out] romf ROM struct, already filled in from its header
 * @param [in] image ROM image, header included
 * @param [in] size Size of the image in bytes
 * @return const struct _rom_fixup* Applied entry, NULL if none matched
 */
static const struct _rom_fixup *
apply_rom_fixup(rom_t *romf, const uint8_t *image, size_t size)
{
	size_t i, payload, needed;
	uint32_t crc;

	if (size <= sizeof(rom_header_t))
		return NULL;

	payload = size - sizeof(rom_header_t);
	crc = crc32_block(image + sizeof(rom_header_t), payload);

	for (i = 0; i < sizeof(rom_fixups) / sizeof(rom_fixups[0]); i++) {
		const struct _rom_fixup *fixup = &rom_fixups[i];

		if (fixup->crc != crc)
			continue;

		/* Belt and braces: never hand a mapper more memory than was read */
		needed = fixup->prg_size + fixup->chr_size;
		if (romf->trainer != NULL)
			needed += 512;
		if (needed > payload)
			continue;

		romf->mapper = fixup->mapper;
		romf->prg_size = fixup->prg_size;
		romf->chr_size = fixup->chr_size;
		romf->mirroring = fixup->mirroring;
		/* PRG ROM moved the end of the image, CHR ROM follows it */
		romf->chr_rom = romf->prg_rom + romf->prg_size;

		/* Correct the loaded header too, so that the nametable arrangement
		 * reported by get_rom_mirroring() cannot disagree with the one the
		 * mappers read back from the ROM struct.
		 */
		romf->header->flag6.nes.fourscreen =
			(fixup->mirroring == FOUR_SCREEN) ? 1 : 0;
		romf->header->flag6.nes.mirroring =
			(fixup->mirroring == VERTICAL_MIRRORING) ? 1 : 0;

		return fixup;
	}

	return NULL;
}

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
			/* Size in 16 KB units: MSB nibble (flag9) + LSB byte */
			romf->prg_size =
				((romf->header->flag9.nes.prgrom_size << 8) & 0xf00) |
				romf->header->prg_size;
			romf->prg_size *= (16 * 1024);
		} else {
			/* Get exponent and multiplier */
			exp = ((romf->header->prg_size & 0xfc) >> 2);
			mul = ((romf->header->prg_size & 0x3) * 2) + 1;
			/* Calculate total size in bytes */
			romf->prg_size = (1 << exp) * mul;
		}

		if (romf->header->flag9.nes.chrrom_size < 0xf) {
			/* Size in 8 KB units: MSB nibble (flag9) + LSB byte */
			romf->chr_size =
				((romf->header->flag9.nes.chrrom_size << 8) & 0xf00) |
				romf->header->chr_size;
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
		romf->mapper = ((romf->header->flag8.nes.mapper << 8) & 0xf00) |
					   ((romf->header->flag7.nes.mapper << 4) & 0xf0) |
					   (romf->header->flag6.nes.mapper & 0xf);
	}

	/* Last word goes to the fixup table, for the handful of dumps whose
	 * header describes a cartridge that never existed.
	 */
	apply_rom_fixup(romf, rom_file, (size_t)ret);

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
