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
 * @file ppu.h
 * @see ppu.c
 */
#ifndef __PPU_H__
#define __PPU_H__

#include "cartridge.h"
#include "mappers/mapper.h"
#include "palette.h"
#include <stdint.h>

/* PPU's registers */
#define REG_PPUCTRL 0x2000
#define REG_PPUMASK 0x2001
#define REG_PPUSTATUS 0x2002
#define REG_OAMADDR 0x2003
#define REG_OAMDATA 0x2004
#define REG_PPUSCROLL 0x2005
#define REG_PPUADDR 0x2006
#define REG_PPUDATA 0x2007
#define REG_OAMDMA 0x4014

/** Number of sprites in OAM memory */
#define TOTAL_SPRITES 64
/** Number of sprints in the OAM secondary memory */
#define TOTAL_SPRITES_AUX 8

/** PPU's video standard */
enum _PPU_VIDEO {
	PPU_NTSC,
	PPU_PAL,
};

/** PPU's rendering state */
enum _PPU_STATE {
	PPU_STOPPED,
	PPU_RUNNING,
};

/** PPU's internal VRAM register */
struct _ppu_vram {
	union _reg_vram {
		struct _vram {
			uint16_t coarse_X : 5;
			uint16_t coarse_Y : 5;
			uint16_t nametable_X : 1;
			uint16_t nametable_Y : 1;
			uint16_t fine_Y : 3;
			uint16_t reserved : 1;
		} bits;
		uint16_t raw;
	} reg;
};

/** Sprite's struct */
struct _OAM_sprite {
	/** Sprite Y coordinate */
	uint8_t Y;
	/** Sprite tile # */
	struct _tile {
		/** Bank */
		uint8_t bank : 1;
		/** Tile index number */
		uint8_t number : 7;
	} tile;
	/** Sprite attribute */
	struct _attr {
		/** Pallete of sprite */
		uint8_t palette : 2;
		/** Unimplemented */
		uint8_t reserved : 3;
		/** Priority (0 = in front of background; 1 = behind)*/
		uint8_t priority : 1;
		/** Flip sprite horizontally */
		uint8_t flip_horiz : 1;
		/** Flip sprite vertically */
		uint8_t flip_vert : 1;
	} attr;
	/** Sprite X coordinate */
	uint8_t X;
};

/** PPU struct */
struct _ppu {
	/** PPUCTRL register */
	union _reg_PPUCTRL {
		struct _reg0 {
			/** Base nametable address */
			uint8_t nametable_X : 1;
			/** Base nametable address */
			uint8_t nametable_Y : 1;
			/** VRAM address increment */
			uint8_t vram_inc : 1;
			/** Sprite pattern table address for 8x8 sprites */
			uint8_t sprite : 1;
			/** Background pattern table address */
			uint8_t background : 1;
			/** Sprite size */
			uint8_t sprite_size : 1;
			/** PPU master/slave select */
			uint8_t select : 1;
			/** Generate an NMI at the start of vertical blank */
			uint8_t nmi : 1;
		} reg;
		uint8_t raw;
	} PPUCTRL;
	/** PPUMASK register */
	union _reg_PPUMASK {
		struct _reg1 {
			/** Greyscale */
			uint8_t greyscale : 1;
			/** Show/Hide background in leftmost 8 pixels of the screen */
			uint8_t show_bg_msb : 1;
			/** Show/Hide sprites in leftmost 8 pixels of the screen */
			uint8_t show_sp_msb : 1;
			/** Show background */
			uint8_t show_background : 1;
			/** Show sprites */
			uint8_t show_sprites : 1;
			/** Emphasize RED */
			uint8_t emph_red : 1;
			/** Emphasize GREEN */
			uint8_t emph_green : 1;
			/** Emphasize BLUE */
			uint8_t emph_blue : 1;
		} reg;
		uint8_t raw;
	} PPUMASK;
	/** PPUSTATUS register */
	union _reg_PPUSTATUS {
		struct _reg2 {
			/** PPU open bus */
			uint8_t open_bus : 5;
			/** Sprite overflow */
			uint8_t sprite_overflow : 1;
			/** Sprite 0 hit */
			uint8_t sprite_hit : 1;
			/** Vertical blank status */
			uint8_t vblank : 1;
		} reg;
		uint8_t raw;
	} PPUSTATUS;
	/** OAMADDR register */
	uint8_t OAMADDR;
	/** OAMDATA register */
	uint8_t OAMDATA;
	/** PPUSCROLL */
	uint8_t PPUSCROLL;
	/** PPUADDR */
	uint8_t PPUADDR;
	/** PPUDATA */
	uint8_t PPUDATA;
	/** OAMDMA */
	uint8_t OAMDMA;
	/** Internal registers */
	struct _iregs {
		struct _ppu_vram v;
		struct _ppu_vram t;
		uint16_t x;
		uint16_t w;
	} iregs;
	/** OAM memory */
	union _OAM {
		struct _OAM_sprite sprites[TOTAL_SPRITES];
		uint8_t mem[TOTAL_SPRITES * sizeof(struct _OAM_sprite)];
	} OAM;
	/** Secondary OAM: List of sprites to draw */
	union _OAM_aux {
		struct _OAM_sprite sprites[TOTAL_SPRITES_AUX];
		uint8_t mem[TOTAL_SPRITES_AUX * sizeof(struct _OAM_sprite)];
	} OAM_aux;
	/** Dynamic latch */
	uint8_t gen_latch;
	/** Game cartridge */
	cartridge_t *cartridge;
	/** Video standard */
	enum _PPU_VIDEO video;
	/** Rendering state */
	enum _PPU_STATE state;
	/** Powering up time */
	int32_t age;
	/** Current scanline */
	uint16_t scanline;
	/** Current rendering cycle */
	uint16_t cycle;
	/** Next background tile ID */
	uint8_t bg_tile_id;
	/** Next background tile pattern data (MSB) */
	uint8_t bg_tile_msb;
	/** Next background tile pattern data (LSB) */
	uint8_t bg_tile_lsb;
	/** Background shift registers (16 bits): pattern data (MSB) */
	uint16_t bg_shift_msb;
	/** Background shift registers (16 bits): pattern data (LSB) */
	uint16_t bg_shift_lsb;
	/** Attribute value for next 4x4 tiles */
	uint8_t bg_attr;
	/** Background shift registers (16 bits): attribute data (MSB) */
	uint16_t bg_attr_msb;
	/** Background shift registers (16 bits): attribute data (LSB) */
	uint16_t bg_attr_lsb;
	/** Sprites shift register (8 bits) data (MSB) */
	uint8_t sp_msb[TOTAL_SPRITES_AUX];
	/** Sprites shift register (8 bits) data (LSB) */
	uint8_t sp_lsb[TOTAL_SPRITES_AUX];
	/** Is sprite 0 being drawing? */
	uint8_t draw_sp0;
	/** Fine X */
	uint8_t fine_X;
	/** Number of sprites being drawing on current frame */
	uint8_t sprite_cnt;
	/** Callback to trigger mapper clock */
	void (*trigger_cb)(mapper_t *);
	/** Mapper for the trigger callback */
	mapper_t *mapper_cb;
} __attribute__((packed));

/** PPU */
typedef struct _ppu ppu_t;

/** PPU's register access type */
typedef enum _PPU_REG_TYPE ppu_reg_t;

/** PPU's video standard type */
typedef enum _PPU_VIDEO ppu_video_t;

/** PPU's state */
typedef enum _PPU_STATE ppu_state_t;

/** Global system's PPU */
extern ppu_t ppu;

int ppu_init(void);
int ppu_destroy(void);
void ppu_reset(void);
void ppu_set_video(ppu_video_t video);
ppu_video_t ppu_get_video(void);
uint8_t ppu_reg_read(uint16_t reg);
void ppu_reg_write(uint16_t reg, uint8_t value);
int ppu_run(void);
int ppu_stop(void);
void ppu_clock(void);
int ppu_set_cartridge(cartridge_t *cartridge);
int ppu_set_mapper_trigger_cb(mapper_t *m, void (*trigger_cb)(mapper_t *));

#endif /* __PPU_H__ */
