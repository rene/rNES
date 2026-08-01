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
 * @file m1_SxROM.c
 *
 * Implements the mapper 1 (SxROM).
 */
#include <errno.h>
#include <mappers/mapper.h>
#include <stdlib.h>

/** MMC1 S?ROM variant */
enum _mmc1_variant {
	/** SEROM, SHROM and SH1ROM */
	MMC1_NORAM,
	/** SNROM: 8KB CHR-ROM/RAM */
	MMC1_SNROM,
	/** SOROM: 16 KB PRG-RAM */
	MMC1_SOROM,
	/** SUROM: 512 KB PRG (A18 = CHR reg bit 4), 8 KB PRG-RAM */
	MMC1_SUROM,
	/** SXROM: 512 KB PRG (A18 = CHR reg bit 4), 32 KB PRG-RAM */
	MMC1_SXROM
};

/** MMC1 control register */
struct _mmc1_ctrl_reg {
	union _ctrl_reg_bs {
		struct _bs {
			uint8_t nametable : 2;
			uint8_t prg_mode : 2;
			uint8_t chr_mode : 1;
			uint8_t reserved : 3;
		} bits;
		uint8_t raw;
	} reg;
};

/** SxROM custom data */
struct _m1_mapper {
	/** Mapper variant */
	enum _mmc1_variant variant;
	/** PRG RAM */
	uint8_t *prg_ram;
	/** PRG RAM size */
	uint32_t prg_ram_size;
	/** Shift register */
	uint8_t shift_reg;
	/** Control register */
	struct _mmc1_ctrl_reg ctrl_reg;
	/** CHR bank 0 */
	uint64_t chr0_reg;
	/** CHR bank 1 */
	uint64_t chr1_reg;
	/** PRG bank */
	uint64_t prg_reg;
	/** Mirroring mode */
	enum _rom_mirroring mirroring;
	/** Indices of the PRG ROM current banks */
	uint64_t prg_bank[2];
	/** Indices of the CHR ROM current banks */
	uint64_t chr_bank[2];
};

/**
 * Identify the MMC1 board variant
 * @param [in] c Cartridge
 * @param [in] ram_size PRG-RAM size in bytes
 * @return enum _mmc1_variant The identified variant
 */
static enum _mmc1_variant get_m1_variant(cartridge_t *c, uint32_t ram_size)
{
	if (c == NULL)
		return MMC1_NORAM;

	if (c->rom->prg_size >= 0x80000) {
		if (ram_size >= 0x8000)
			return MMC1_SXROM;
		else
			return MMC1_SUROM;
	}

	if (ram_size >= 0x4000)
		return MMC1_SOROM;
	if (ram_size >= 0x2000)
		return MMC1_SNROM;

	return MMC1_NORAM;
}

/**
 * Get the total PRG RAM size
 * @param [in] c Cartridge
 * @return uint32_t Total PRG RAM size
 */
static uint32_t get_prg_ram_size(cartridge_t *c)
{
	uint32_t size = 0;
	rom_header_t *h;

	if (c == NULL)
		return 0;
	else
		h = c->rom->header;

	if (c->rom->rom_fmt == NES_FMT) {
		if (h->flag10.nes.prgram_shift)
			size += 64u << h->flag10.nes.prgram_shift;
		if (h->flag10.nes.nvram_shift)
			size += 64u << h->flag10.nes.nvram_shift;
		return size;
	}

	/* iNES: PRG-RAM size field is in 8 KB units */
	if (h->flag8.ines.prgram_size)
		return h->flag8.ines.prgram_size * 0x2000;

	/* prgram_size == 0: no PRG-RAM unless battery-backed is signalled */
	if (!h->flag6.nes.persistmem)
		return 0;

	/* Battery-backed but size unspecified: assume 8 KB (SNROM) */
	return 0x2000;
}

/**
 * Compute the PRG-RAM index for an address in the $6000-$7FFF range.
 * The bank comes from the CHR bank 0 register, with a variant-specific layout.
 * @param [in] m Mapper's struct
 * @param [in] address Memory address
 * @return uint32_t Index into the PRG-RAM
 */
static uint32_t get_prg_ram_index(struct _m1_mapper *m1, uint16_t address)
{
	uint32_t bank = 0;

	switch (m1->variant) {
	case MMC1_SOROM:
		/* bit 3 selects one of two 8 KB banks (16 KB total) */
		bank = (m1->chr0_reg >> 3) & 0x1;
		break;
	case MMC1_SXROM:
		/* bits 2-3 select one of four 8 KB banks (32 KB total) */
		bank = (m1->chr0_reg >> 2) & 0x3;
		break;
	default:
		/* Single 8 KB bank */
		bank = 0;
		break;
	}

	return (bank * 0x2000) + (address & 0x1fff);
}

/**
 * Translate mirror addresses of VRAM
 * @param [in] m Mapper's struct
 * @param [in] op Memory operation (R/W)
 * @param [in] source Source address
 * @return uint16_t Physical address
 */
static uint16_t vram_addr_tr(struct _mapper_t *m, enum mem_op op,
							 uint16_t source)
{
	uint16_t va = source;
	struct _m1_mapper *m1 = (struct _m1_mapper *)m->data;

	/* VRAM: Name tables */
	if (source >= 0x2000 && source <= 0x2fff) {
		/* Translate according to the mirroring mode */
		if (m1->mirroring == VERTICAL_MIRRORING) {
			if (source >= 0x2000 && source <= 0x23ff)
				va = source - 0x2000;
			else if (source >= 0x2400 && source <= 0x27ff)
				va = (source - 0x2400) | 0x400;
			else if (source >= 0x2800 && source <= 0x2bff)
				va = source - 0x2800;
			else if (source >= 0x2c00 && source <= 0x2fff)
				va = (source - 0x2c00) | 0x400;
		} else if (m1->mirroring == HORIZONTAL_MIRRORING) {
			if (source >= 0x2000 && source <= 0x23ff)
				va = source - 0x2000;
			else if (source >= 0x2400 && source <= 0x27ff)
				va = source - 0x2400;
			else if (source >= 0x2800 && source <= 0x2bff)
				va = (source - 0x2800) | 0x400;
			else if (source >= 0x2c00 && source <= 0x2fff)
				va = (source - 0x2c00) | 0x400;
		} else if (m1->mirroring == ONE_SCREEN_LOW) {
			/* All four banks point to the first 1KB of VRAM */
			va = source & 0x3ff;
		} else {
			/* All four banks point to the second 1KB of VRAM */
			va = (source & 0x3ff) | 0x400;
		}
	} else if (source >= 0x3000 && source <= 0x3eff) {
		/* Mirrors of 0x2000 - 0x2eff */
		return vram_addr_tr(m, op, (source & 0x2fff));
	}

	return va;
}

/**
 * Update the bank layout of the mapper
 * @param [in,out] Mapper struct
 */
static void m1_update(struct _mapper_t *m)
{
	cartridge_t *cartridge = m->cartridge;
	struct _m1_mapper *m1 = (struct _m1_mapper *)m->data;
	uint64_t prg_bank, prg_A18, last_bank;

	/* Mirroring mode */
	switch (m1->ctrl_reg.reg.bits.nametable) {
	case 0:
		m1->mirroring = ONE_SCREEN_LOW;
		break;
	case 1:
		m1->mirroring = ONE_SCREEN_HIGH;
		break;
	case 2:
		m1->mirroring = VERTICAL_MIRRORING;
		break;
	case 3:
		m1->mirroring = HORIZONTAL_MIRRORING;
		break;
	}

	/* PRG ROM bank mode
	 * prg_bank[0] -> 0x8000 - 0xbfff
	 * prg_bank[1] -> 0xc000 - 0xffff
	 *
	 * On 512 KB boards (SUROM/SXROM) CHR register bit 4 acts as PRG A18,
	 * selecting one of two 256 KB halves.
	 */
	if (m1->variant == MMC1_SUROM || m1->variant == MMC1_SXROM) {
		prg_A18 = (m1->chr0_reg & 0x10);
		last_bank = prg_A18 | 0x0f;
	} else {
		prg_A18 = 0;
		last_bank = (cartridge->rom->prg_size / 0x4000) - 1;
	}
	prg_bank = (m1->prg_reg & 0x0f) | prg_A18;

	switch (m1->ctrl_reg.reg.bits.prg_mode) {
	case 0:
	case 1:
		m1->prg_bank[0] = (prg_bank & 0x1e) * 0x4000;
		m1->prg_bank[1] = ((prg_bank & 0x1e) | 0x1) * 0x4000;
		break;
	case 2:
		m1->prg_bank[0] = prg_A18 * 0x4000;
		m1->prg_bank[1] = prg_bank * 0x4000;
		break;
	case 3:
		m1->prg_bank[0] = prg_bank * 0x4000;
		m1->prg_bank[1] = last_bank * 0x4000;
		break;
	}

	/* CHR ROM bank mode
	 * chr_bank[0] -> 0x0000 - 0x0fff
	 * chr_bank[1] -> 0x1000 - 0x1fff
	 */
	switch (m1->ctrl_reg.reg.bits.chr_mode) {
	case 0:
		m1->chr_bank[0] = (m1->chr0_reg & 0x1e) * 0x1000;
		m1->chr_bank[1] = ((m1->chr0_reg & 0x1e) | 0x1) * 0x1000;
		break;
	case 1:
		m1->chr_bank[0] = m1->chr0_reg * 0x1000;
		m1->chr_bank[1] = m1->chr1_reg * 0x1000;
		break;
	}
}

/**
 * Mapper initialization function
 * @param [in,out] m Mapper
 * @param [in,out] c Cartridge
 * @return int 0 on success, error code otherwise
 */
static int m1_mapper_init(struct _mapper_t *m, cartridge_t *c)
{
	struct _m1_mapper *m1;

	if (m) {
		m->cartridge = c;
		m->data = calloc(sizeof(struct _m1_mapper), 1);
		if (m->data == NULL)
			return -ENOMEM;

		m1 = (struct _m1_mapper *)m->data;
	} else {
		return -EINVAL;
	}

	/* Detect the board variant and allocate the corresponding PRG RAM size */
	m1->prg_ram_size = get_prg_ram_size(c);
	m1->variant = get_m1_variant(c, m1->prg_ram_size);
	if (m1->prg_ram_size > 0) {
		m1->prg_ram = calloc(m1->prg_ram_size, sizeof(uint8_t));
		if (!m1->prg_ram) {
			free(m->data);
			return -ENOMEM;
		}
	} else {
		m1->prg_ram = NULL;
	}

	/* Reset values */
	m1->shift_reg = 0x10;
	m1->ctrl_reg.reg.raw = 0x0c;
	m1->chr0_reg = 0;
	m1->chr1_reg = 0;
	m1->prg_reg = 0;
	m1_update(m);
	return 0;
}

/**
 * Mapper finalization function
 * @param [in,out] m Mapper
 * @return int 0 on success, error code otherwise
 */
static int m1_mapper_finalize(struct _mapper_t *m)
{
	struct _m1_mapper *m1 = (struct _m1_mapper *)m->data;

	free(m1->prg_ram);
	free(m1);
	m->data = NULL;
	return 0;
}

/**
 * PRG ROM handler
 * @param [in,out] Mapper struct
 * @param [in] op Memory operation (READ/WRITE)
 * @param [in] address Memory address
 * @param [in] value Value to write (when op is WRITE)
 * @uint8_t Value read/written from/to memory
 */
uint8_t m1_prg_mem_handler(struct _mapper_t *m, enum mem_op op,
						   uint16_t address, uint8_t value)
{
	cartridge_t *cartridge = m->cartridge;
	struct _m1_mapper *m1 = (struct _m1_mapper *)m->data;
	uint8_t write_done, reg_val;
	uint64_t idx;

	switch (op) {
	case CMEM_WRITE:
		if (address >= 0x6000 && address <= 0x7fff) {
			/* PRG RAM */
			if (m1->prg_ram) {
				idx = get_prg_ram_index(m1, address);
				if (idx < m1->prg_ram_size)
					m1->prg_ram[idx] = value;
			}
		} else if (address >= 0x8000 && address <= 0xffff) {
			if ((value & 0x80)) {
				/* Clear shift register */
				m1->shift_reg = 0x10;
				m1->ctrl_reg.reg.raw |= 0x0c;
				m1_update(m);
			} else {
				/* Push bit 0 of value into bit 4 of shift register */
				write_done = (m1->shift_reg & 0x1);
				m1->shift_reg = (m1->shift_reg >> 1) | ((value & 0x01) << 4);
				if (write_done) {
					/* We have reached the 5th write */
					reg_val = (m1->shift_reg & 0x1f);
					if (address >= 0x8000 && address <= 0x9fff)
						m1->ctrl_reg.reg.raw = reg_val;
					else if (address >= 0xa000 && address <= 0xbfff)
						m1->chr0_reg = reg_val;
					else if (address >= 0xc000 && address <= 0xdfff)
						m1->chr1_reg = reg_val;
					else if (address >= 0xe000 && address <= 0xffff)
						m1->prg_reg = reg_val;
					/* Reset shift register */
					m1->shift_reg = 0x10;
					/* Update current banks */
					m1_update(m);
				}
			}
		}
		break;

	case CMEM_READ:
		if (address >= 0x6000 && address <= 0x7fff) {
			if (m1->prg_ram) {
				idx = get_prg_ram_index(m1, address);
				if (idx < m1->prg_ram_size)
					return m1->prg_ram[idx];
			}
			return 0;
		} else if (address >= 0x8000 && address <= 0xbfff) {
			return cartridge->rom
				->prg_rom[m1->prg_bank[0] + (address & 0x3fff)];
		} else if (address >= 0xc000 && address <= 0xffff) {
			return cartridge->rom
				->prg_rom[m1->prg_bank[1] + (address & 0x3fff)];
		}
		break;
	}

	return 0;
}

/**
 * CHR ROM handler
 * @param [in,out] Mapper struct
 * @param [in] op Memory operation (READ/WRITE)
 * @param [in] address Memory address
 * @param [in] value Value to write (when op is WRITE)
 * @uint8_t Value read/written from/to memory
 */
uint8_t m1_chr_mem_handler(struct _mapper_t *m, enum mem_op op,
						   uint16_t address, uint8_t value)
{
	cartridge_t *cartridge = m->cartridge;
	struct _m1_mapper *m1 = (struct _m1_mapper *)m->data;
	uint32_t idx;

	if (address >= 0 && address <= 0x0fff) {
		idx = m1->chr_bank[0] + address;
	} else if (address >= 0x1000 && address <= 0x1fff) {
		idx = m1->chr_bank[1] + (address & 0x0fff);
	}

	switch (op) {
	case CMEM_READ:
		return cartridge->rom->chr_rom[idx];

	case CMEM_WRITE:
		if (cartridge->chr_ram)
			cartridge->rom->chr_rom[idx] = value;
		return value;
	}

	return 0;
}

/**
 * VRAM's memory handler
 * @param [in,out] Mapper struct
 * @param [in] op Memory operation (READ/WRITE)
 * @param [in] address Memory address
 * @param [in] value Value to write (when op is WRITE)
 * @uint8_t Value read/written from/to memory
 */
uint8_t m1_vram_mem_handler(struct _mapper_t *m, enum mem_op op,
							uint16_t address, uint8_t value)
{
	uint16_t idx;
	cartridge_t *cartridge = m->cartridge;

	idx = vram_addr_tr(m, op, address);
	if (op == CMEM_READ)
		return cartridge->vram[idx];

	cartridge->vram[idx] = value;
	return value;
}

/** Mapper 1 struct */
mapper_t m1_SxROM = {
	.init = m1_mapper_init,
	.reset = mapper_reset,
	.finalize = m1_mapper_finalize,
	.prg_mem_handler = m1_prg_mem_handler,
	.chr_mem_handler = m1_chr_mem_handler,
	.vram_mem_handler = m1_vram_mem_handler,
	.palette_mem_handler = m0_palette_mem_handler,
};
