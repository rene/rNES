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
 * @file m4_MMC3.c
 *
 * Implements the mappers 4 and 6 (MMC3 and MMC6).
 */
#include "cpu650x.h"
#include "ppu.h"
#include <errno.h>
#include <mappers/mapper.h>
#include <stdlib.h>

/** MMC3 bank select register */
struct _mmc3_reg {
	union _reg_bs {
		struct _bs {
			uint8_t bank_reg : 3;
			uint8_t reserved : 2;
			uint8_t ram_en : 1;
			uint8_t prg_mode : 1;
			uint8_t chr_mode : 1;
		} bits;
		uint8_t raw;
	} reg;
};

/* MMC3 PRG RAM protection register */
struct _mmc3_ram_reg {
	union _reg_ram1 {
		struct _ram1 {
			/** Reserved */
			uint8_t reserved : 6;
			/** Write protection (0: allow writes; 1: deny writes) */
			uint8_t write_prot : 1;
			/** PRG RAM chip enable (0: disabled; 1: enabled) */
			uint8_t ram_en : 1;
		} bits;
		uint8_t raw;
	} reg;
};

/* MMC6 PRG RAM protection register */
struct _mmc6_ram_reg {
	union _reg_ram2 {
		struct _ram2 {
			uint8_t reserved : 4;
			uint8_t write_7000 : 1;
			uint8_t read_7000 : 1;
			uint8_t write_7200 : 1;
			uint8_t read_7200 : 1;
		} bits;
		uint8_t raw;
	} reg;
};

/* MMC3/MMC6 RAM control register */
union _ram_reg {
	struct _mmc3_ram_reg mmc3;
	struct _mmc6_ram_reg mmc6;
};

/** MMC3 custom data */
struct _mmc3_mapper {
	/** Mirroring selection */
	enum _rom_mirroring mirroring;
	/** Mirroring enabled */
	uint8_t mirroring_en;
	/** IRQ enabled */
	uint8_t IRQ_enabled;
	/** IRQ counter */
	uint8_t IRQ_counter;
	/** IRQ reload flag */
	uint8_t IRQ_reload;
	/** IRQ latch */
	uint8_t IRQ_latch;
	/** Bank selector register */
	struct _mmc3_reg bank_sel;
	/** CHR and PRG bank registers */
	uint8_t reg_bank[8];
	/** Address of the last two PRG ROM banks */
	uint64_t last_bank[2];
	/** PRG RAM */
	uint8_t *prg_ram;
	/** RAM control register */
	union _ram_reg ram_ctrl;
};

/**
 * Handle IRQ counter
 * @param [in,out] m Mapper
 */
static void m4_mapper_IRQ_count(mapper_t *m)
{
	struct _mmc3_mapper *mmc3 = (struct _mmc3_mapper *)m;

	/* If the reload flag was set or the counter hit 0 */
	if (mmc3->IRQ_counter == 0 || mmc3->IRQ_reload) {
		mmc3->IRQ_counter = mmc3->IRQ_latch;
		mmc3->IRQ_reload = 0;
	} else {
		mmc3->IRQ_counter--;
	}

	/* Only fire if the counter is 0 and we are enabled */
	if (mmc3->IRQ_counter == 0 && mmc3->IRQ_enabled) {
		cpu_trigger_irq();
	}
}

/**
 * Mapper initialization function
 * @param [in,out] m Mapper
 * @param [in,out] c Cartridge
 * @return int 0 on success, error code otherwise
 */
static int m4_mapper_init(struct _mapper_t *m, cartridge_t *c)
{
	struct _mmc3_mapper *mmc3;
	int i;

	if (m) {
		m->cartridge = c;
		m->data = calloc(1, sizeof(struct _mmc3_mapper));
		if (m->data == NULL)
			return -ENOMEM;

		mmc3 = (struct _mmc3_mapper *)m->data;
	} else {
		return 0;
	}

	/* Is this a 4 screen game? */
	mmc3->mirroring = get_rom_mirroring(c->rom);
	if (mmc3->mirroring == FOUR_SCREEN) {
		/* Disable screen mirroring */
		mmc3->mirroring_en = 0;
		/* We need to allocate more VRAM */
		free(m->cartridge->vram);
		m->cartridge->vram = calloc(0x1000, sizeof(uint8_t)); /* 4KB */
		if (m->cartridge->vram == NULL) {
			free(m->data);
			return -ENOMEM;
		}
	} else {
		/* Enable screen mirroring */
		mmc3->mirroring_en = 1;
	}

	/* Allocates 8 KB of PRG RAM */
	mmc3->prg_ram = calloc(0x2000, sizeof(uint8_t));
	if (!mmc3->prg_ram) {
		free(m->data);
		return -ENOMEM;
	}

	/* Initialize all bank registers */
	for (i = 0; i < 8; i++)
		mmc3->reg_bank[i] = 0;

	mmc3->bank_sel.reg.raw = 0;
	mmc3->ram_ctrl.mmc3.reg.raw = 0;

	/* Initialize IRQ registers */
	mmc3->IRQ_counter = 0;
	mmc3->IRQ_latch = 0;
	mmc3->IRQ_enabled = 0;
	mmc3->IRQ_reload = 0;

	/* Set last PRG ROM banks address */
	mmc3->last_bank[0] = c->rom->prg_size - 0x2000;
	mmc3->last_bank[1] = c->rom->prg_size - 0x4000;

	/* Set PPU callback */
	ppu_set_mapper_trigger_cb((mapper_t *)mmc3, m4_mapper_IRQ_count);
	return 0;
}

/**
 * Mapper finalization function
 * @param [in,out] m Mapper
 * @return int 0 on success, error code otherwise
 */
static int m4_mapper_finalize(struct _mapper_t *m)
{
	struct _mmc3_mapper *mmc3;
	cartridge_t *c = m->cartridge;

	if (!m)
		return 0;

	mmc3 = (struct _mmc3_mapper *)m->data;
	free(mmc3->prg_ram);
	free(m->data);
	m->data = NULL;

	if (mmc3->mirroring_en == 0) {
		/* Back VRAM to 2KB */
		free(c->vram);
		c->vram = calloc(sizeof(uint8_t), 0x800); /* 2KB */
		if (c->vram == NULL)
			return -ENOMEM;
	}

	return 0;
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
	struct _mmc3_mapper *mmc3 = (struct _mmc3_mapper *)m->data;

	/* Check for a 4 screen game */
	if (mmc3->mirroring_en == 0) {
		return ((source - 0x2000) & 0xfff);
	}

	/* VRAM: Name tables */
	if (source >= 0x2000 && source <= 0x2fff) {
		/* Translate according to the mirroring mode */
		if (mmc3->mirroring == VERTICAL_MIRRORING) {
			if (source >= 0x2000 && source <= 0x23ff)
				va = source - 0x2000;
			else if (source >= 0x2400 && source <= 0x27ff)
				va = (source - 0x2400) | 0x400;
			else if (source >= 0x2800 && source <= 0x2bff)
				va = source - 0x2800;
			else if (source >= 0x2c00 && source <= 0x2fff)
				va = (source - 0x2c00) | 0x400;
		} else {
			if (source >= 0x2000 && source <= 0x23ff)
				va = source - 0x2000;
			else if (source >= 0x2400 && source <= 0x27ff)
				va = source - 0x2400;
			else if (source >= 0x2800 && source <= 0x2bff)
				va = (source - 0x2800) | 0x400;
			else if (source >= 0x2c00 && source <= 0x2fff)
				va = (source - 0x2c00) | 0x400;
		}
	} else if (source >= 0x3000 && source <= 0x3eff) {
		/* Mirrors of 0x2000 - 0x2eff */
		return vram_addr_tr(m, op, (source & 0x2fff));
	}

	return va;
}

/**
 * PRG ROM handler
 * @param [in,out] Mapper struct
 * @param [in] op Memory operation (READ/WRITE)
 * @param [in] address Memory address
 * @param [in] value Value to write (when op is WRITE)
 * @uint8_t Value read/written from/to memory
 */
uint8_t m4_prg_mem_handler(struct _mapper_t *m, enum mem_op op,
						   uint16_t address, uint8_t value)
{
	uint64_t idx;
	int reg;
	cartridge_t *cartridge = m->cartridge;
	struct _mmc3_mapper *mmc3 = (struct _mmc3_mapper *)m->data;

	switch (op) {
	case CMEM_READ:
		if (address >= 0x6000 && address <= 0x7fff) {
			/* PRG RAM area */
			if (mmc3->ram_ctrl.mmc3.reg.bits.ram_en == 1) {
				idx = (address & 0x1fff);
				return mmc3->prg_ram[idx];
			}

		} else if (address >= 0x8000 && address <= 0x9fff) {
			/* PRG mode: 0 - R:6 | 1 - second last bank */
			if (mmc3->bank_sel.reg.bits.prg_mode == 0)
				idx =
					((mmc3->reg_bank[6] & 0x3f) * 0x2000) + (address & 0x1fff);
			else
				idx = mmc3->last_bank[1] + (address & 0x1fff);
			return cartridge->rom->prg_rom[idx];

		} else if (address >= 0xa000 && address <= 0xbfff) {
			/* PRG mode: 0/1 - R:7 */
			idx = ((mmc3->reg_bank[7] & 0x3f) * 0x2000) + (address & 0x1fff);
			return cartridge->rom->prg_rom[idx];

		} else if (address >= 0xc000 && address <= 0xdfff) {
			/* PRG mode: 0 - second last bank | 1 - R:6 */
			if (mmc3->bank_sel.reg.bits.prg_mode == 0)
				idx = mmc3->last_bank[1] + (address & 0x1fff);
			else
				idx =
					((mmc3->reg_bank[6] & 0x3f) * 0x2000) + (address & 0x1fff);
			return cartridge->rom->prg_rom[idx];

		} else if (address >= 0xe000 && address <= 0xffff) {
			/* PRG mode: 0/1 - fixed to the last bank */
			idx = mmc3->last_bank[0] + (address & 0x1fff);
			return cartridge->rom->prg_rom[idx];
		}
		break;

	case CMEM_WRITE:
		if (address >= 0x6000 && address <= 0x7fff) {
			/* PRG RAM area */
			if (mmc3->ram_ctrl.mmc3.reg.bits.write_prot == 0) {
				idx = (address & 0x1fff);
				mmc3->prg_ram[idx] = value;
			}
			return value;

		} else if (address >= 0x8000 && address <= 0x9fff) {
			if ((address & 0x1) == 0) {
				/* Bank selection */
				mmc3->bank_sel.reg.raw = value;
			} else {
				/* Write bank register */
				reg = mmc3->bank_sel.reg.bits.bank_reg;
				mmc3->reg_bank[reg] = value;
			}
			return value;

		} else if (address >= 0xa000 && address <= 0xbfff) {
			if ((address & 0x1) == 0) {
				/* Set screen mirroring mode */
				if (mmc3->mirroring_en) {
					if ((value & 0x1))
						mmc3->mirroring = HORIZONTAL_MIRRORING;
					else
						mmc3->mirroring = VERTICAL_MIRRORING;
				}
			} else {
				mmc3->ram_ctrl.mmc3.reg.raw = value;
			}
		} else if (address >= 0xc000 && address <= 0xdfff) {
			if ((address & 0x1) == 0) {
				/* IRQ reload counter value */
				mmc3->IRQ_latch = value;
			} else {
				/* Mark the register to reload */
				mmc3->IRQ_reload = 1;
			}

		} else if (address >= 0xe000 && address <= 0xffff) {
			if ((address & 0x1) == 0) {
				/* Disable MMC3 interrupts */
				mmc3->IRQ_enabled = 0;
				cpu_clear_irq();
			} else {
				/* Enable MMC3 interrupts */
				mmc3->IRQ_enabled = 1;
			}
			return value;
		}
		return value;
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
uint8_t m4_chr_mem_handler(struct _mapper_t *m, enum mem_op op,
						   uint16_t address, uint8_t value)
{
	uint64_t idx = 0;
	cartridge_t *cartridge = m->cartridge;
	struct _mmc3_mapper *mmc3 = (struct _mmc3_mapper *)m->data;

	if (address >= 0 && address <= 0x07ff) {
		/* CHR mode: 0 - R0 | 1 - R2 - R3 */
		if (mmc3->bank_sel.reg.bits.chr_mode == 0) {
			idx = ((mmc3->reg_bank[0] & 0xfe) * 0x400) + (address & 0x7ff);
		} else {
			if (address < 0x400)
				idx = (mmc3->reg_bank[2] * 0x400) + (address & 0x3ff);
			else
				idx = (mmc3->reg_bank[3] * 0x400) + (address & 0x3ff);
		}
	} else if (address >= 0x0800 && address <= 0x0fff) {
		/* CHR mode: 0 - R1 | 1 - R4 - R5 */
		if (mmc3->bank_sel.reg.bits.chr_mode == 0) {
			idx = ((mmc3->reg_bank[1] & 0xfe) * 0x400) + (address & 0x7ff);
		} else {
			if (address < 0x0c00)
				idx = (mmc3->reg_bank[4] * 0x400) + (address & 0x3ff);
			else
				idx = (mmc3->reg_bank[5] * 0x400) + (address & 0x3ff);
		}
	} else if (address >= 0x1000 && address <= 0x17ff) {
		/* CHR mode: 0 - R2 - R3 | 1 - R0 */
		if (mmc3->bank_sel.reg.bits.chr_mode == 0) {
			if (address < 0x1400)
				idx = (mmc3->reg_bank[2] * 0x400) + (address & 0x3ff);
			else
				idx = (mmc3->reg_bank[3] * 0x400) + (address & 0x3ff);
		} else {
			idx = ((mmc3->reg_bank[0] & 0xfe) * 0x400) + (address & 0x7ff);
		}
	} else if (address >= 0x1800 && address <= 0x1fff) {
		/* CHR mode: 0 - R4 - R5 | 1 - R1 */
		if (mmc3->bank_sel.reg.bits.chr_mode == 0) {
			if (address < 0x1c00)
				idx = (mmc3->reg_bank[4] * 0x400) + (address & 0x3ff);
			else
				idx = (mmc3->reg_bank[5] * 0x400) + (address & 0x3ff);
		} else {
			idx = ((mmc3->reg_bank[1] & 0xfe) * 0x400) + (address & 0x7ff);
		}
	}

	switch (op) {
	case CMEM_READ:
		return cartridge->rom->chr_rom[idx];
		break;

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
uint8_t m4_vram_mem_handler(struct _mapper_t *m, enum mem_op op,
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

/** Mapper 4 struct */
mapper_t m4_MMC3 = {
	.init = m4_mapper_init,
	.reset = mapper_reset,
	.finalize = m4_mapper_finalize,
	.prg_mem_handler = m4_prg_mem_handler,
	.chr_mem_handler = m4_chr_mem_handler,
	.vram_mem_handler = m4_vram_mem_handler,
	.palette_mem_handler = m0_palette_mem_handler,
};
