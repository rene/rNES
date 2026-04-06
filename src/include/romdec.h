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
 * @file romdec.h
 * @see romdec.c
 */
#ifndef __ROMDEC_H__
#define __ROMDEC_H__

#include <stdint.h>

/** ROM file formats */
enum _rom_format { INES_FMT = 0, NES_FMT, UNKNOWN_FMT };

/** Mirroring mode */
enum _rom_mirroring {
	HORIZONTAL_MIRRORING = 0,
	VERTICAL_MIRRORING,
	FOUR_SCREEN,
	/* Used by mapper MMC1 */
	ONE_SCREEN_LOW,
	ONE_SCREEN_HIGH
};

/** Video standard */
enum _rom_video_standard { ROM_NTSC = 0, ROM_PAL_M, ROM_MULTIPLE, ROM_DENDY };

/**
 * iNES/NES ROM header
 * This header supports both iNES and NES 2.0 ROM file formats. Flag fields
 * can be accessed using ines/nes structs or through raw field (for raw
 * value). For instance:
 *     header.flag7.nes.console_type
 *     header.flag7.ines.vs_unisys
 *     header.flag7.raw
 */
struct _rom_header {
	/** Header signature, must be "NES" + 0x1a */
	char magic[4];
	/** Size of PRG ROM in 16KB units (LSB in NES 2.0) */
	uint8_t prg_size;
	/** Size of CHR ROM in 8KB units (LSB in NES 2.0) */
	uint8_t chr_size;
	/** Flag 6 */
	union _rom_flag6 {
		/** iNES and NES 2.0 fields */
		struct _flag6_nes {
			/** Mirroring type */
			uint8_t mirroring : 1;
			/** Persistent memory (battery packed, for instance) */
			uint8_t persistmem : 1;
			/** Trainer presence */
			uint8_t trainer : 1;
			/** Ignore mirroring control, provide a four-screen VRAM */
			uint8_t fourscreen : 1;
			/** Mapper number D0..D3 */
			uint8_t mapper : 4;
		} nes;
		/** RAW value */
		uint8_t raw;
	} flag6;
	/** Flag 7 */
	union _rom_flag7 {
		/** iNES fields */
		struct _flag7_ines {
			/** VS Unisystem */
			uint8_t vs_unisys : 1;
			/** Playchoice */
			uint8_t playchoice : 1;
			/** If equal to 2, flags 8-15 are in NES 2.0 format */
			uint8_t nes2 : 2;
			/** Mapper number D4..D7 */
			uint8_t mapper : 4;
		} ines;
		/** NES 2.0 fields */
		struct _flag7_nes {
			/** Console type:
			 * 0: Nintendo Entertainment System/Family Computer
			 * 1: Nintendo Vs. System
			 * 2: Nintendo Playchoice 10
			 * 3: Extended Console Type
			 */
			uint8_t console_type : 2;
			/** If equal to 2, flags 8-15 are in NES 2.0 format */
			uint8_t nes2 : 2;
			/** Mapper number D4..D7 */
			uint8_t mapper : 4;
		} nes;
		/** RAW value */
		uint8_t raw;
	} flag7;
	/** Flag 8 */
	union _rom_flag8 {
		/** iNES fields */
		struct _flag8_ines {
			/** PRG RAM size */
			uint8_t prgram_size;
		} ines;
		/** NES 2.0 fields */
		struct _flag8_nes {
			/** Mapper number D8..D11 */
			uint8_t mapper : 4;
			/** Sub-mapper number */
			uint8_t submapper : 4;
		} nes;
		/** RAW value */
		uint8_t raw;
	} flag8;
	/** Flag 9 */
	union _rom_flag9 {
		/** iNES fields */
		struct _flag9_ines {
			/** TV System (0: NTSC, 1: PAL) */
			uint8_t TV_system : 1;
			/** Reserved */
			uint8_t reserved : 7;
		} ines;
		/** NES 2.0 fields */
		struct _flag9_nes {
			/** PRG-ROM size MSB */
			uint8_t prgrom_size : 4;
			/** CHR-ROM size MSB */
			uint8_t chrrom_size : 4;
		} nes;
		/** RAW value */
		uint8_t raw;
	} flag9;
	/** Flag 10 */
	union _rom_flag10 {
		/** iNES fields */
		struct _flag10_ines {
			/** TV System (0: NTSC, 2: PAL, 1/3: Dual compatible) */
			uint8_t TV_system : 2;
			/** Not used */
			uint8_t reserved1 : 2;
			/** PRG RAM (0: Present, 1: Not present) */
			uint8_t prg_ram : 1;
			/** Board has bus conflicts (0: no, 1: yes) */
			uint8_t bus_conflicts : 1;
			/** Not used */
			uint8_t reserved2 : 2;
		} ines;
		/** NES 2.0 fields */
		struct _flag10_nes {
			/** PRG-RAM (volatile) shift count */
			uint8_t prgram_shift : 4;
			/** PRG-NVRAM/EEPROM (non-volatile) shift count */
			uint8_t nvram_shift : 4;
		} nes;
		/** RAW value */
		uint8_t raw;
	} flag10;
	/** Flag 11 */
	union _rom_flag11 {
		/** iNES fields */
		struct _flag11_ines {
			/** Unused */
			uint8_t unused;
		} ines;
		/** NES 2.0 fields */
		struct _flag11_nes {
			/** CHR-RAM size (volatile) shift count */
			uint8_t chrram_shift : 4;
			/** CHR-NVRAM size (non-volatile) shift count */
			uint8_t chrnv_shift : 4;
		} nes;
		/** RAW value */
		uint8_t raw;
	} flag11;
	/** Flag 12 */
	union _rom_flag12 {
		/** iNES fields */
		struct _flag12_ines {
			/** Unused */
			uint8_t unused;
		} ines;
		/** NES 2.0 fields */
		struct _flag12_nes {
			/** CPU/PPU timing mode */
			uint8_t cpu_ppu_timing : 2;
			/** Unused */
			uint8_t reserved : 6;
		} nes;
		/** RAW value */
		uint8_t raw;
	} flag12;
	/** Flag 13 */
	union _rom_flag13 {
		/** iNES fields */
		struct _flag13_ines {
			/** Unused */
			uint8_t unused;
		} ines;
		/** NES 2.0 fields */
		struct _flag13_nes {
			/** PPU Type */
			uint8_t ppu_type : 4;
			/** Hardware Type */
			uint8_t hw_type : 4;
		} nes;
		/** RAW value */
		uint8_t raw;
	} flag13;
	/** Flag 14 */
	union _rom_flag14 {
		/** iNES fields */
		struct _flag14_ines {
			/** Unused */
			uint8_t unused;
		} ines;
		/** NES 2.0 fields */
		struct _flag14_nes {
			/** Number of miscellaneous ROMs present */
			uint8_t misc_roms : 2;
			/** Unused */
			uint8_t reserved : 6;
		} nes;
		/** RAW value */
		uint8_t raw;
	} flag14;
	/** Flag 15 */
	union _rom_flag15 {
		/** iNES fields */
		struct _flag15_ines {
			/** Unused */
			uint8_t unused;
		} ines;
		/** NES 2.0 fields */
		struct _flag15_nes {
			/** Default Expansion Device */
			uint8_t exp_device : 6;
			/** Unused */
			uint8_t reserved : 2;
		} nes;
		/** RAW value */
		uint8_t raw;
	} flag15;
} __attribute__((packed));

/**
 * ROM information struct
 * This struct aggregates main information from the header in order to
 * facilitate the access to them regardless the ROM file format.
 */
struct _rom {
	/** ROM file format */
	enum _rom_format rom_fmt;
	/** Size of PRG ROM in bytes */
	uint64_t prg_size;
	/** Size of CHR ROM in bytes */
	uint64_t chr_size;
	/** Mapper implemented by the ROM */
	uint16_t mapper;
	/** Mirroring mode */
	enum _rom_mirroring mirroring;
	/** Pointer to the ROM file header */
	struct _rom_header *header;
	/** Pointer to the PRG ROM */
	uint8_t *prg_rom;
	/** Pointer to the CHR ROM */
	uint8_t *chr_rom;
	/** Pointer to Trainer data */
	uint8_t *trainer;
};

/** ROM file formats */
typedef enum _rom_format rom_fmt_t;
/** Mirroring mode type */
typedef enum _rom_mirroring rom_mirroring_t;
/** ROM video standard */
typedef enum _rom_video_standard rom_video_t;
/** iNES/NES ROM header type */
typedef struct _rom_header rom_header_t;
/** ROM struct */
typedef struct _rom rom_t;

int load_rom(const char *pathname, rom_t **rom);
int unload_rom(rom_t *rom);
rom_fmt_t get_rom_format(rom_t *rom);
rom_mirroring_t get_rom_mirroring(rom_t *rom);
rom_video_t get_rom_video_std(rom_t *rom);

#endif /* __ROMDEC_H__ */
