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
 * @file ppu.c
 *
 * This module implements the Picture Processing Unit (PPU). This
 * implementation follows many approaches detailed in [1] and implemented in
 * [2].
 *
 * 1: https://www.nesdev.org/wiki/PPU_rendering
 * 2: https://github.com/OneLoneCoder/olcNES
 */
#include "ppu.h"
#include "cpu650x.h"
#include "hal/hal.h"

/** Global system's PPU */
ppu_t ppu;

/**
 * Emphasis color proportions taken from:
 * https://www.nesdev.org/NinTech.txt
 */
static int color_emp[][3] = {
	{1000, 1000, 1000}, /* 000 */
	{1239, 915, 743},	/* 001 */
	{794, 1086, 882},	/* 010 */
	{1019, 980, 653},	/* 011 */
	{905, 1026, 1277},	/* 100 */
	{1023, 980, 979},	/* 101 */
	{741, 987, 1001},	/* 110 */
	{750, 750, 750},	/* 111 */
};

/**
 * Extract a bit from a value
 * @param [in] value Source value to extract the bit
 * @param [in] mask Bit mask to test against the value
 * @param [in] pos Position of the returning bit
 * @return bit
 */
static inline uint8_t get_bit(uint16_t value, uint16_t mask, uint8_t pos)
{
	uint8_t bit;

	if ((value & mask) > 0)
		bit = 1;
	else
		bit = 0;

	return (bit << pos);
}

/**
 * Returns sprite's height (in pixels)
 * @return Sprite's height in pixels
 */
static inline uint8_t get_sprite_height(void)
{
	if (ppu.PPUCTRL.reg.sprite_size)
		return 16;
	return 8;
}

/**
 * Check if PPU rendering is enabled
 * @return uint8_t
 */
static inline uint8_t is_rendering(void)
{
	if (ppu.PPUMASK.reg.show_background == 1 ||
		ppu.PPUMASK.reg.show_sprites == 1)
		return 1;
	else
		return 0;
}

/**
 * Flip a byte
 * @param [in] value Byte to be flipped
 * @return uint8_t
 * @note This function is not optimized. For slower systems, consider use a
 * lookup table
 */
static inline uint8_t flip_byte(uint8_t value)
{
	uint8_t mask = 0x80;
	uint8_t i, flipped = 0;

	for (i = 0; i < 8; i++, mask >>= 1) {
		if ((value & mask) > 0)
			flipped |= (1 << i);
	}
	return flipped;
}

/**
 * Check (and apply when needed) for color emphasis on a pixel
 * @param [in] pixel Pixel
 * @return RGB_t filtered pixel
 */
static inline RGB_t filter_pixel(RGB_t pixel)
{
	int r, g, b, idx;
	RGB_t p;

	idx = (ppu.PPUMASK.raw >> 5) & 0x7;
	r = (pixel.R * color_emp[idx][0]) / 1000;
	g = (pixel.G * color_emp[idx][1]) / 1000;
	b = (pixel.B * color_emp[idx][2]) / 1000;

	if (r > 255)
		r = 255;

	if (g > 255)
		g = 255;

	if (b > 255)
		b = 255;

	p.R = r;
	p.G = g;
	p.B = b;
	return p;
}

/**
 * Increment horizontal position
 */
static void inc_horiz(void)
{
	if (!is_rendering())
		return;

	if (ppu.iregs.v.reg.bits.coarse_X == 31) {
		ppu.iregs.v.reg.bits.coarse_X = 0;
		ppu.iregs.v.reg.bits.nametable_X = ~ppu.iregs.v.reg.bits.nametable_X;
	} else {
		ppu.iregs.v.reg.bits.coarse_X++;
	}
}

/**
 * Increment vertical position
 */
static void inc_vert(void)
{
	if (!is_rendering())
		return;

	if (ppu.iregs.v.reg.bits.fine_Y < 7) {
		ppu.iregs.v.reg.bits.fine_Y++;
	} else {
		ppu.iregs.v.reg.bits.fine_Y = 0;
		if (ppu.iregs.v.reg.bits.coarse_Y == 29) {
			ppu.iregs.v.reg.bits.coarse_Y = 0;
			ppu.iregs.v.reg.bits.nametable_Y =
				~ppu.iregs.v.reg.bits.nametable_Y;
		} else if (ppu.iregs.v.reg.bits.coarse_Y == 31) {
			ppu.iregs.v.reg.bits.coarse_Y = 0;
		} else {
			ppu.iregs.v.reg.bits.coarse_Y++;
		}
	}
}

/**
 * Copy horizontal position: horiz(v) = horiz(t)
 */
static void copy_horiz(void)
{
	if (!is_rendering())
		return;

	ppu.iregs.v.reg.bits.coarse_X = ppu.iregs.t.reg.bits.coarse_X;
	ppu.iregs.v.reg.bits.nametable_X = ppu.iregs.t.reg.bits.nametable_X;
}

/**
 * Copy vertical position: vert(v) = vert(t)
 */
static void copy_vert(void)
{
	if (!is_rendering())
		return;

	ppu.iregs.v.reg.bits.coarse_Y = ppu.iregs.t.reg.bits.coarse_Y;
	ppu.iregs.v.reg.bits.nametable_Y = ppu.iregs.t.reg.bits.nametable_Y;
	ppu.iregs.v.reg.bits.fine_Y = ppu.iregs.t.reg.bits.fine_Y;
}

/**
 * Initialize PPU’s internal structures
 * @return 0 on success, -1 otherwise
 */
int ppu_init(void)
{
	ppu.cartridge = NULL;
	ppu.state = PPU_STOPPED;
	ppu_set_video(PPU_NTSC);
	ppu_reset();
	return 0;
}

/**
 * Finalize PPU’s internal structures
 * @return 0 on success, -1 otherwise
 */
int ppu_destroy(void)
{
	/* Nothing to do */
	return 0;
}

/**
 * Resets PPU’s to initial state
 */
void ppu_reset(void)
{
	static uint8_t power_up;

	power_up = 0;
	ppu.scanline = 0;
	ppu.cycle = 0;
	ppu.iregs.w = 0;
	ppu.iregs.v.reg.raw = 0;
	ppu.iregs.t.reg.raw = 0;

	ppu.PPUCTRL.raw = 0;
	ppu.PPUMASK.raw = 0;
	ppu.PPUSTATUS.raw = 0;
	ppu.PPUSCROLL = 0;
	ppu.PPUDATA = 0;
	ppu.gen_latch = 0;
	ppu.age = 0;
	ppu.sprite_cnt = 0;

	if (power_up == 0) {
		power_up = 1;
		ppu.OAMADDR = 0;
		ppu.PPUADDR = 0;
	}
}

/**
 * Select PPU’s video standard: NTSC or PAL
 * @param [in] video PPU_NTSC or PPU_PAL
 * @note This function will not reset the PPU, when changing between modes,
 * the proper reset must be performed after the video selection.
 */
void ppu_set_video(ppu_video_t video)
{
	/* TODO: Implement PAL PPU */
	ppu.video = video;
}

/**
 * Return the current video standard of the PPU
 * @return ppu_video_t PPU_NTSC or PPU_PAL
 */
ppu_video_t ppu_get_video(void) { return ppu.video; }

/**
 * Read one byte (8 bits) from the specified register
 * @param [in] reg Register address
 * @return Register value
 */
uint8_t ppu_reg_read(uint16_t reg)
{
	uint8_t value;

	switch (reg) {
	case REG_PPUCTRL: /* PPUCTRL: Write-Only */
		value = ppu.gen_latch | ppu.PPUSTATUS.reg.open_bus;
		break;

	case REG_PPUMASK: /* PPUMASK: Write-Only */
		value = ppu.gen_latch | ppu.PPUSTATUS.reg.open_bus;
		break;

	case REG_PPUSTATUS: /* PPUSTATUS: Read-Only */
		value = (ppu.PPUSTATUS.raw & 0xe0) | (ppu.gen_latch & 0x1f);
		ppu.gen_latch = value;
		ppu.iregs.w = 0;
		ppu.PPUSTATUS.reg.vblank = 0;
		break;

	case REG_OAMADDR: /* OAMADDR: Write-Only */
		value = ppu.gen_latch | ppu.PPUSTATUS.reg.open_bus;
		break;

	case REG_OAMDATA: /* OAMDATA: Read/Write */
		value = ppu.OAM.mem[ppu.OAMADDR];
		ppu.gen_latch = value;
		break;

	case REG_PPUSCROLL: /* PPUSCROLL: Write twice */
		value = ppu.gen_latch | ppu.PPUSTATUS.reg.open_bus;
		break;

	case REG_PPUADDR: /* PPUADDR: Write twice */
		value = ppu.gen_latch | ppu.PPUSTATUS.reg.open_bus;
		break;

	case REG_PPUDATA: /* PPUDATA: Read/Write */
		value = ppu.gen_latch;
		ppu.gen_latch =
			cartridge_mem_read(ppu.cartridge, ppu.iregs.v.reg.raw, MEM_PPU);

		/* For palette memory, return the actual value */
		if (ppu.iregs.v.reg.raw >= 0x3f00) {
			value = ppu.gen_latch;
			ppu.gen_latch = cartridge_mem_read(
				ppu.cartridge, ppu.iregs.v.reg.raw & 0x2fff, MEM_PPU);
		}

		if (ppu.PPUCTRL.reg.vram_inc == 0)
			ppu.iregs.v.reg.raw += 1;
		else
			ppu.iregs.v.reg.raw += 32;
		break;

	case REG_OAMDMA: /* OAMDMA: Write-Only */
		value = ppu.gen_latch | ppu.PPUSTATUS.reg.open_bus;
		break;
	}

	return value;
}

/**
 * Write one byte (8 bits) to the specified register
 * @param [in] reg Register address
 * @param [in] value Value
 */
void ppu_reg_write(uint16_t reg, uint8_t value)
{
	/* Save vblank configuration */
	uint8_t old_vblank = ppu.PPUCTRL.reg.nmi;

	switch (reg) {
	case REG_PPUCTRL: /* PPUCTRL: Write-Only */
		ppu.PPUCTRL.raw = value;
		ppu.iregs.t.reg.bits.nametable_X = ppu.PPUCTRL.reg.nametable_X;
		ppu.iregs.t.reg.bits.nametable_Y = ppu.PPUCTRL.reg.nametable_Y;
		/* Check for NMI */
		if (old_vblank == 0 && ppu.PPUCTRL.reg.nmi == 1 &&
			ppu.PPUSTATUS.reg.vblank == 1)
			cpu_trigger_nmi();
		break;

	case REG_PPUMASK: /* PPUMASK: Write-Only */
		ppu.PPUMASK.raw = value;
		break;

	case REG_PPUSTATUS: /* PPUSTATUS: Read-Only */
		break;

	case REG_OAMADDR: /* OAMADDR: Write-Only */
		ppu.OAMADDR = value;
		break;

	case REG_OAMDATA: /* OAMDATA: Read/Write */
		/* Ignore writes during rendering or pre-rendering */
		if (ppu.scanline < 240 || ppu.scanline == 261)
			break;
		ppu.OAM.mem[ppu.OAMADDR] = value;
		ppu.OAMADDR++;
		break;

	case REG_PPUSCROLL: /* PPUSCROLL: Write twice */
		if (ppu.iregs.w == 0) {
			ppu.iregs.t.reg.bits.coarse_X = (value >> 3);
			ppu.fine_X = (value & 0x07);
			ppu.iregs.w = 1;
		} else {
			ppu.iregs.t.reg.bits.fine_Y = (value & 0x07);
			ppu.iregs.t.reg.bits.coarse_Y = (value >> 3);
			ppu.iregs.w = 0;
		}
		break;

	case REG_PPUADDR: /* PPUADDR: Write twice */
		if (ppu.iregs.w == 0) {
			ppu.iregs.t.reg.raw = ((uint16_t)(value & 0x3f) << 8) |
								  (ppu.iregs.t.reg.raw & 0x00ff);
			ppu.iregs.w = 1;
		} else {
			ppu.iregs.t.reg.raw = (ppu.iregs.t.reg.raw & 0xff00) | value;
			ppu.iregs.v.reg.raw = ppu.iregs.t.reg.raw;
			ppu.iregs.w = 0;
		}
		break;

	case REG_PPUDATA: /* PPUDATA: Read/Write */
		cartridge_mem_write(ppu.cartridge, ppu.iregs.v.reg.raw, value, MEM_PPU);
		if (ppu.PPUCTRL.reg.vram_inc == 0)
			ppu.iregs.v.reg.raw += 1;
		else
			ppu.iregs.v.reg.raw += 32;
		break;

	case REG_OAMDMA: /* OAMDMA: Write-Only */
		/* DMA is handled by the sbus module */
		break;
	}
}

/**
 * Enable the PPU rendering process
 * @return 0 on success, -1 if the rendering process it's already enabled
 */
int ppu_run(void)
{
	if (ppu.state == PPU_RUNNING)
		return -1;

	ppu.state = PPU_RUNNING;
	return 0;
}

/**
 * Disable the PPU rendering process
 * @return 0 on success, -1 if the rendering process it's already stopped
 */
int ppu_stop(void)
{
	if (ppu.state == PPU_STOPPED)
		return -1;

	ppu.state = PPU_STOPPED;
	return 0;
}

/**
 * Set the game cartridge to the PPU
 * @param [in,out] cartridge Game cartridge
 * @return 0 on success, -1 otherwise
 */
int ppu_set_cartridge(cartridge_t *cartridge)
{
	if (cartridge == NULL)
		return -1;

	ppu.cartridge = cartridge;
	return 0;
}

/**
 * Set mapper callback to count scanlines
 * Some mappers, notable the MMC3, needs to count PPU scanlines, this
 * function will setup a callback so we can let the PPU inform the mapper
 * when it finishes the visible part of a scanline (if the rendering is
 * enabled)
 * @param [in] m Mapper
 * @param [in] trigger_cb Function callback
 * @return 0 on success, -1 otherwise
 */
int ppu_set_mapper_trigger_cb(mapper_t *m, void (*trigger_cb)(mapper_t *))
{
	if (m == NULL || trigger_cb == NULL)
		return -1;

	ppu.trigger_cb = trigger_cb;
	ppu.mapper_cb = m;
	return 0;
}

/**
 * Performs one clock tick in the PPU (rendering process)
 */
void ppu_clock(void)
{
	uint16_t i, tile_addr, tile_id, tile_attr, aux;
	uint16_t ida_bg, idx_bg, ida_sp, idx_sp, cur_x;
	uint8_t attr, bpal, spal, p[2], a[2], sp[2];
	uint8_t sp_lsb, sp_msb, sp_row, sp_prio, draw_sp, is_sp0;
	int nf;
	RGB_t bg_pixel, sp_pixel, draw_pixel;
	palette_t *sys_pal = get_palette();

	/* Powering up time */
	if (ppu.age < 30000) {
		ppu.age++;
		return;
	}

	/* Idle cycle */
	if (ppu.cycle == 0) {
		ppu.cycle++;
		return;
	}

	/* Visible frame or Pre-Render line */
	if ((ppu.scanline >= 0 && ppu.scanline <= 239) || ppu.scanline == 261) {

		if (ppu.scanline == 261) {
			if (ppu.cycle == 1) {
				ppu.PPUSTATUS.reg.vblank = 0;
				ppu.PPUSTATUS.reg.sprite_hit = 0;
				ppu.PPUSTATUS.reg.sprite_overflow = 0;
				cpu_clear_nmi();
			} else if (ppu.cycle >= 280 && ppu.cycle <= 304) {
				copy_vert();
			}
		}

		if ((ppu.cycle >= 1 && ppu.cycle < 258) ||
			(ppu.cycle >= 321 && ppu.cycle <= 336)) {

			if (is_rendering()) {
				ppu.bg_shift_msb <<= 1;
				ppu.bg_shift_lsb <<= 1;
				ppu.bg_attr_msb <<= 1;
				ppu.bg_attr_lsb <<= 1;
			}

			switch ((ppu.cycle % 8)) {
			case 0:
				/* Increment horizontal position */
				inc_horiz();
				/* Increment vertical position */
				if (ppu.cycle == 256)
					inc_vert();
				break;

			case 1:
				if (!is_rendering())
					break;

				/* Load shift registers */
				ppu.bg_shift_msb =
					(ppu.bg_shift_msb & 0xff00) | ppu.bg_tile_msb;
				ppu.bg_shift_lsb =
					(ppu.bg_shift_lsb & 0xff00) | ppu.bg_tile_lsb;

				/* Break the attribute byte into MSB+LSB and duplicate it
				 * to a full byte. Then, insert into the attribute shift
				 * register.
				 */
				if ((ppu.bg_attr & 0x2))
					ppu.bg_attr_msb |= 0x00ff;
				else
					ppu.bg_attr_msb &= 0xff00;

				if ((ppu.bg_attr & 0x1))
					ppu.bg_attr_lsb |= 0x00ff;
				else
					ppu.bg_attr_lsb &= 0xff00;

				/* Fetch the next background tile ID */
				tile_addr = 0x2000 | (ppu.iregs.v.reg.raw & 0x0fff);
				ppu.bg_tile_id =
					cartridge_mem_read(ppu.cartridge, tile_addr, MEM_PPU);

				if (ppu.cycle == 257)
					copy_horiz();
				break;

			case 3:
				if (!is_rendering())
					break;

				/* Fetch attribute value
				 * Each attribute byte controls the palette of a 4x4 tile
				 * from the nametable. The attributes for each part of the
				 * 4x4 tile are placed in the attribute byte as the
				 * following (2 bits each):
				 * Bottom Right | Bottom Left | Top Right | Top Left
				 */
				tile_attr = 0x23c0 | (ppu.iregs.v.reg.bits.nametable_Y << 11) |
							(ppu.iregs.v.reg.bits.nametable_X << 10) |
							((ppu.iregs.v.reg.bits.coarse_Y >> 2) << 3) |
							(ppu.iregs.v.reg.bits.coarse_X >> 2);
				attr = cartridge_mem_read(ppu.cartridge, tile_attr, MEM_PPU);
				if ((ppu.iregs.v.reg.bits.coarse_Y % 4) >= 2)
					attr >>= 4;
				if ((ppu.iregs.v.reg.bits.coarse_X % 4) >= 2)
					attr >>= 2;
				attr &= 0x3;
				ppu.bg_attr = attr;
				break;

			case 5:
				aux = (ppu.PPUCTRL.reg.background << 12) +
					  ((uint16_t)ppu.bg_tile_id << 4) +
					  ppu.iregs.v.reg.bits.fine_Y;
				ppu.bg_tile_lsb =
					cartridge_mem_read(ppu.cartridge, aux, MEM_PPU);
				break;

			case 7:
				aux = (ppu.PPUCTRL.reg.background << 12) +
					  ((uint16_t)ppu.bg_tile_id << 4) +
					  ppu.iregs.v.reg.bits.fine_Y;
				ppu.bg_tile_msb =
					cartridge_mem_read(ppu.cartridge, aux + 8, MEM_PPU);
				break;
			}
		}

		/* Clear OAMADDR */
		if (ppu.cycle >= 257 && ppu.cycle <= 320)
			ppu.OAMADDR = 0;

		/* Evaluate sprites to the next scanline */
		if (ppu.cycle == 256) {
			/* Clear secondary OAM */
			for (i = 0; i < sizeof(ppu.OAM_aux); i++)
				ppu.OAM_aux.mem[i] = 0xff;
			ppu.sprite_cnt = 0;
			ppu.draw_sp0 = 0;
			for (i = 0; i < TOTAL_SPRITES && ppu.sprite_cnt <= 8; i++) {
				/* Check if we have valid date in the sprite */
				if (ppu.OAM.sprites[i].Y == 0 || ppu.OAM.sprites[i].Y == 0xff)
					continue;
				/* Check Y coordinate */
				if ((ppu.scanline >= ppu.OAM.sprites[i].Y) &&
					(ppu.scanline <
					 (ppu.OAM.sprites[i].Y + get_sprite_height()))) {
					if (ppu.sprite_cnt < 8) {
						/* Copy sprite to secondary OAM */
						ppu.OAM_aux.sprites[ppu.sprite_cnt] =
							ppu.OAM.sprites[i];
						ppu.sprite_cnt++;
						/* Is this the sprite zero? If so, it might have sprite
						 * collision */
						if (i == 0)
							ppu.draw_sp0 = 1;
					} else if (ppu.sprite_cnt++ == 8) {
						if (is_rendering())
							ppu.PPUSTATUS.reg.sprite_overflow = 1;
					}
				}
			}
		}

		/* Populate sprite shift register with sprite data */
		if (ppu.cycle == 261) {
			for (i = 0; i < ppu.sprite_cnt; i++) {
				/* First, let's find the corresponding row of the sprites
				 * to be plotted
				 */
				if (ppu.PPUCTRL.reg.sprite_size) {
					/* 8x16 sprite */
					sp_row = ppu.scanline - ppu.OAM_aux.sprites[i].Y;
					tile_id = (ppu.OAM_aux.sprites[i].tile.number << 1) & 0xfe;

					if (!ppu.OAM_aux.sprites[i].attr.flip_vert) {
						/* Sprite is not vertically flipped */
						if (sp_row < 8) {
							/* Top 8x8 tile */
							aux = (ppu.OAM_aux.sprites[i].tile.bank << 12) |
								  (tile_id << 4) | sp_row;
						} else {
							/* Bottom 8x8 tile */
							aux = (ppu.OAM_aux.sprites[i].tile.bank << 12) |
								  ((tile_id + 1) << 4) | (sp_row & 0x07);
						}
					} else {
						/* Sprite is vertically flipped */
						if (sp_row < 8) {
							/* Bottom 8x8 tile */
							aux = (ppu.OAM_aux.sprites[i].tile.bank << 12) |
								  ((tile_id + 1) << 4) | ((7 - sp_row) & 0x07);
						} else {
							/* Top 8x8 tile */
							aux = (ppu.OAM_aux.sprites[i].tile.bank << 12) |
								  (tile_id << 4) | ((7 - sp_row) & 0x07);
						}
					}
				} else {
					/* 8x8 sprite */
					if (ppu.OAM_aux.sprites[i].attr.flip_vert)
						/* Sprite is flipped vertically */
						sp_row =
							(7 - (ppu.scanline - ppu.OAM_aux.sprites[i].Y));
					else
						/* Normal vertical orientation */
						sp_row = ppu.scanline - ppu.OAM_aux.sprites[i].Y;

					aux = (ppu.PPUCTRL.reg.sprite << 12) |
						  (((ppu.OAM_aux.sprites[i].tile.number << 1) |
							ppu.OAM_aux.sprites[i].tile.bank)
						   << 4) |
						  sp_row;
				}

				sp_lsb = cartridge_mem_read(ppu.cartridge, aux, MEM_PPU);
				sp_msb = cartridge_mem_read(ppu.cartridge, aux + 8, MEM_PPU);

				/* Check horizontal flip */
				if (ppu.OAM_aux.sprites[i].attr.flip_horiz) {
					sp_lsb = flip_byte(sp_lsb);
					sp_msb = flip_byte(sp_msb);
				}

				ppu.sp_lsb[i] = sp_lsb;
				ppu.sp_msb[i] = sp_msb;
			}
		}

		/* Draw pixel */
		if (ppu.cycle < 256 && ppu.scanline < 240 && is_rendering()) {
			/* Look for sprites */
			draw_sp = 0;
			is_sp0 = 0;
			ida_sp = 0;
			idx_sp = 0;
			sp_prio = 0;
			cur_x = ppu.cycle - 1;
			for (i = 0; i < ppu.sprite_cnt; i++) {
				if (cur_x >= ppu.OAM_aux.sprites[i].X &&
					cur_x < (ppu.OAM_aux.sprites[i].X + 8)) {
					sp[0] = get_bit(ppu.sp_lsb[i], 0x80, 0);
					sp[1] = get_bit(ppu.sp_msb[i], 0x80, 1);
					ppu.sp_lsb[i] <<= 1;
					ppu.sp_msb[i] <<= 1;

					ida_sp = sp[1] | sp[0];
					if (ida_sp != 0 && !draw_sp) {
						idx_sp = ida_sp;
						aux =
							(0x3f00 +
							 ((ppu.OAM_aux.sprites[i].attr.palette + 4) << 2) +
							 idx_sp);
						spal = cartridge_mem_read(ppu.cartridge, aux, MEM_PPU);
						/* Check for greyscale mode */
						if (ppu.PPUMASK.reg.greyscale)
							spal &= 0x30;

						sp_pixel = sys_pal->color[spal];
						sp_prio = ppu.OAM_aux.sprites[i].attr.priority;

						draw_sp = 1;
						if (i == 0)
							is_sp0 = 1;
					}
				}
			}

			/* Sprites are not drawn on the first scanline
			 * PS: Notice that we still had to process the pixel because
			 * PPU keeps processing (shifting registers, etc) anyways.
			 */
			if (!ppu.PPUMASK.reg.show_sprites || ppu.scanline == 0 ||
				(!ppu.PPUMASK.reg.show_sp_msb && cur_x < 8)) {
				draw_sp = 0;
				idx_sp = 0;
			}

			/* Pixel value for background */
			idx_bg = 0;
			aux = 0x8000 >> ppu.fine_X;
			p[0] = get_bit(ppu.bg_shift_lsb, aux, 0);
			p[1] = get_bit(ppu.bg_shift_msb, aux, 1);
			a[0] = get_bit(ppu.bg_attr_lsb, aux, 0);
			a[1] = get_bit(ppu.bg_attr_msb, aux, 1);
			idx_bg = p[1] | p[0];
			ida_bg = a[1] | a[0];

			/* Check if background is enabled */
			if (!ppu.PPUMASK.reg.show_background ||
				(!ppu.PPUMASK.reg.show_bg_msb && cur_x < 8))
				idx_bg = 0;

			aux = (0x3f00 + (ida_bg << 2) + idx_bg);
			bpal = cartridge_mem_read(ppu.cartridge, aux, MEM_PPU);
			/* Check for greyscale mode */
			if (ppu.PPUMASK.reg.greyscale)
				bpal &= 0x30;
			bg_pixel = sys_pal->color[bpal];

			/* Process background and foreground pixels in order to define the
			 * final pixel to draw.
			 */
			if (draw_sp) {
				if (idx_bg == 0 && idx_sp == 0) {
					/* Both are transparent pixels, choose background */
					draw_pixel = bg_pixel;
				} else if (idx_bg == 0 && idx_sp > 0) {
					/* Background is transparent, sprite it's not, draw sprite
					 */
					draw_pixel = sp_pixel;
				} else if (idx_bg > 0 && idx_sp == 0) {
					/* Sprite is transparent, background it's not, draw
					 * background */
					draw_pixel = bg_pixel;
				} else {
					if (sp_prio == 0)
						draw_pixel = sp_pixel;
					else
						draw_pixel = bg_pixel;
				}
			} else {
				draw_pixel = bg_pixel;
			}

			/* Check for sprite 0 collision */
			if (ppu.draw_sp0 && is_sp0) {
				if (idx_bg != 0 && idx_sp != 0) {
					/* Sprite 0 hit cannot happen at pos. 255 */
					if (cur_x < 255)
						ppu.PPUSTATUS.reg.sprite_hit = 1;
				}
			}

			/* Check for color emphasis and draw the pixel */
			gui_set_pixel((ppu.cycle - 1), ppu.scanline,
						  filter_pixel(draw_pixel));
		}
	} else {
		/* Post-Render line */
		if (ppu.scanline == 241 && ppu.cycle == 1) {
			ppu.PPUSTATUS.reg.vblank = 1;
			if (ppu.PPUCTRL.reg.nmi == 1)
				cpu_trigger_nmi();
		}
	}

	if (ppu.cycle == 261 && is_rendering()) {
		if ((ppu.scanline >= 0 && ppu.scanline <= 239) || ppu.scanline == 261) {
			if (ppu.trigger_cb)
				ppu.trigger_cb(ppu.mapper_cb);
		}
	}

	/* Advance one cycle */
	ppu.cycle++;
	/* Odd frame skip. Cycles per scanline for:
	 * Even frames: 341
	 * Odd frames: 340
	 */
	if ((ppu.scanline & 1) == 0)
		nf = 341;
	else
		nf = 340;
	if (ppu.cycle >= nf) {
		ppu.cycle = 0;
		ppu.scanline++;
		if (ppu.scanline > 261)
			ppu.scanline = 0;
	}
}
