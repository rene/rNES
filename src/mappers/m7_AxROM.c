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
 * @file m7_AxROM.c
 *
 * Implements the mapper 7 (AxROM).
 */
#include <errno.h>
#include <mappers/mapper.h>
#include <stdlib.h>

/* Bank register */
struct _m7_bank_reg {
	union _bank_reg_bs {
		struct _bs {
			uint8_t prg_bank : 3;
			uint8_t reserved1 : 1;
			uint8_t vram_page : 1;
			uint8_t reserved2 : 3;
		} bits;
		uint8_t raw;
	} reg;
};

/* AxROM custom data */
struct _m7_mapper {
	/** Mirroring mode */
	enum _rom_mirroring mirroring;
	/** Bank register */
	struct _m7_bank_reg prg_bank;
};

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
	struct _m7_mapper *m7 = (struct _m7_mapper *)m->data;

	/* VRAM: Name tables */
	if (source >= 0x2000 && source <= 0x2fff) {
		if (m7->mirroring == ONE_SCREEN_LOW) {
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
 * Mapper initialization function
 * @param [in,out] m Mapper
 * @param [in,out] c Cartridge
 * @return int 0 on success, error code otherwise
 */
static int m7_mapper_init(struct _mapper_t *m, cartridge_t *c)
{
	struct _m7_mapper *m7;

	if (m) {
		m->cartridge = c;
		m->data = calloc(sizeof(struct _m7_mapper), 1);
		if (m->data == NULL)
			return -ENOMEM;

		m7 = (struct _m7_mapper *)m->data;
	} else {
		return -EINVAL;
	}

	m7->prg_bank.reg.raw = 0;
	m7->mirroring = ONE_SCREEN_LOW;
	return 0;
}

/**
 * Mapper finalization function
 * @param [in,out] m Mapper
 * @return int 0 on success, error code otherwise
 */
static int m7_mapper_finalize(struct _mapper_t *m)
{
	struct _m7_mapper *m7 = (struct _m7_mapper *)m->data;

	free(m7);
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
static uint8_t m7_prg_mem_handler(struct _mapper_t *m, enum mem_op op,
								  uint16_t address, uint8_t value)
{
	cartridge_t *cartridge = m->cartridge;
	struct _m7_mapper *m7 = (struct _m7_mapper *)m->data;
	uint64_t idx;

	switch (op) {
	case CMEM_READ:
		if (address >= 0x8000 && address <= 0xffff) {
			idx =
				(m7->prg_bank.reg.bits.prg_bank * 0x8000) + (address - 0x8000);
			return cartridge->rom->prg_rom[idx];
		}
		break;

	case CMEM_WRITE:
		if (address >= 0x8000 && address <= 0xffff) {
			m7->prg_bank.reg.raw = value;
			if (m7->prg_bank.reg.bits.vram_page == 0) {
				m7->mirroring = ONE_SCREEN_LOW;
			} else {
				m7->mirroring = ONE_SCREEN_HIGH;
			}
			return value;
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
static uint8_t m7_chr_mem_handler(struct _mapper_t *m, enum mem_op op,
								  uint16_t address, uint8_t value)
{
	cartridge_t *cartridge = m->cartridge;
	uint64_t idx;

	idx = (address & 0x1fff);

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
uint8_t m7_vram_mem_handler(struct _mapper_t *m, enum mem_op op,
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

/** Mapper 2 struct */
mapper_t m7_AxROM = {
	.init = m7_mapper_init,
	.reset = mapper_reset,
	.finalize = m7_mapper_finalize,
	.prg_mem_handler = m7_prg_mem_handler,
	.chr_mem_handler = m7_chr_mem_handler,
	.vram_mem_handler = m7_vram_mem_handler,
	.palette_mem_handler = m0_palette_mem_handler,
};
