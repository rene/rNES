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
 * @file m2_UxROM.c
 *
 * Implements the mapper 2 (UxROM).
 */
#include <errno.h>
#include <mappers/mapper.h>
#include <stdlib.h>

/* UxROM custom data */
struct _m2_mapper {
	/** Bank selector register */
	uint8_t reg_banksel;
	/** Address of the last PRG ROM bank */
	uint64_t last_bank;
};

/**
 * Mapper initialization function
 * @param [in,out] m Mapper
 * @param [in,out] c Cartridge
 * @return int 0 on success, error code otherwise
 */
static int m2_mapper_init(struct _mapper_t *m, cartridge_t *c)
{
	struct _m2_mapper *m2;

	if (m) {
		m->cartridge = c;
		m->data = calloc(sizeof(struct _m2_mapper), 1);
		if (m->data == NULL)
			return -ENOMEM;

		m2 = (struct _m2_mapper *)m->data;
	} else {
		return -EINVAL;
	}

	m2->last_bank = c->rom->prg_size - 0x4000;
	m2->reg_banksel = 0;
	return 0;
}

/**
 * Mapper finalization function
 * @param [in,out] m Mapper
 * @return int 0 on success, error code otherwise
 */
static int m2_mapper_finalize(struct _mapper_t *m)
{
	struct _m2_mapper *m2 = (struct _m2_mapper *)m->data;

	free(m2);
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
static uint8_t m2_prg_mem_handler(struct _mapper_t *m, enum mem_op op,
								  uint16_t address, uint8_t value)
{
	cartridge_t *cartridge = m->cartridge;
	struct _m2_mapper *m2 = (struct _m2_mapper *)m->data;
	uint64_t idx = 0;

	if (address >= 0x8000 && address <= 0xbfff) {
		idx = (m2->reg_banksel * 0x4000) + (address & 0x3fff);
	} else if (address >= 0xc000 && address <= 0xffff) {
		idx = m2->last_bank + (address & 0x3fff);
	}

	switch (op) {
	case CMEM_READ:
		return cartridge->rom->prg_rom[idx % cartridge->rom->prg_size];

	case CMEM_WRITE:
		if (address >= 0x8000 && address <= 0xffff) {
			m2->reg_banksel = (value & 0x0f);
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
static uint8_t m2_chr_mem_handler(struct _mapper_t *m, enum mem_op op,
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

/** Mapper 2 struct */
mapper_t m2_UxROM = {
	.init = m2_mapper_init,
	.reset = mapper_reset,
	.finalize = m2_mapper_finalize,
	.prg_mem_handler = m2_prg_mem_handler,
	.chr_mem_handler = m2_chr_mem_handler,
	.vram_mem_handler = m0_vram_mem_handler,
	.palette_mem_handler = m0_palette_mem_handler,
};
