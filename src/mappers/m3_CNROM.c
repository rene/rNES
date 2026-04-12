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
 * @file m3_CNROM.c
 *
 * Implements the mapper 3 (CNROM).
 */
#include <errno.h>
#include <mappers/mapper.h>
#include <stdlib.h>

/* CNROM custom data */
struct _m3_mapper {
	/** CHR bank */
	uint8_t chr_bank;
	/** PRG RAM */
	uint8_t *prg_ram;
};

/**
 * Mapper initialization function
 * @param [in,out] m Mapper
 * @param [in,out] c Cartridge
 * @return int 0 on success, error code otherwise
 */
static int m3_mapper_init(struct _mapper_t *m, cartridge_t *c)
{
	struct _m3_mapper *m3;

	if (m) {
		m->cartridge = c;
		m->data = calloc(sizeof(struct _m3_mapper), 1);
		if (m->data == NULL)
			return -ENOMEM;

		m3 = (struct _m3_mapper *)m->data;
	} else {
		return -EINVAL;
	}

	/* Allocates 2 KB of PRG RAM */
	m3->prg_ram = calloc(0x800, sizeof(uint8_t));
	if (!m3->prg_ram) {
		free(m->data);
		return -ENOMEM;
	}

	m3->chr_bank = 0;
	return 0;
}

/**
 * Mapper finalization function
 * @param [in,out] m Mapper
 * @return int 0 on success, error code otherwise
 */
static int m3_mapper_finalize(struct _mapper_t *m)
{
	struct _m3_mapper *m3 = (struct _m3_mapper *)m->data;

	free(m3->prg_ram);
	free(m3);
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
static uint8_t m3_prg_mem_handler(struct _mapper_t *m, enum mem_op op,
								  uint16_t address, uint8_t value)
{
	cartridge_t *cartridge = m->cartridge;
	struct _m3_mapper *m3 = (struct _m3_mapper *)m->data;
	uint64_t idx;

	switch (op) {
	case CMEM_READ:
		if (address >= 0x6000 && address <= 0x7fff) {
			/* PRG RAM */
			idx = address & 0x7ff;
			return m3->prg_ram[idx];
		} else if (address >= 0x8000 && address <= 0xffff) {
			idx = address & 0x7fff;
			return cartridge->rom->prg_rom[idx];
		}
		break;

	case CMEM_WRITE:
		if (address >= 0x6000 && address <= 0x7fff) {
			/* PRG RAM */
			idx = address & 0x7ff;
			m3->prg_ram[idx] = value;
		} else if (address >= 0x8000 && address <= 0xffff) {
			m3->chr_bank = (value & 0x03);
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
static uint8_t m3_chr_mem_handler(struct _mapper_t *m, enum mem_op op,
								  uint16_t address, uint8_t value)
{
	cartridge_t *cartridge = m->cartridge;
	struct _m3_mapper *m3 = (struct _m3_mapper *)m->data;
	uint64_t idx;

	idx = (m3->chr_bank * 0x2000) + (address & 0x1fff);

	if (address < 0x2000) {
		switch (op) {
		case CMEM_READ:
			return cartridge->rom->chr_rom[idx];

		case CMEM_WRITE:
			break;
		}
	}

	return 0;
}

/** Mapper 2 struct */
mapper_t m3_CNROM = {
	.init = m3_mapper_init,
	.reset = mapper_reset,
	.finalize = m3_mapper_finalize,
	.prg_mem_handler = m3_prg_mem_handler,
	.chr_mem_handler = m3_chr_mem_handler,
	.vram_mem_handler = m0_vram_mem_handler,
	.palette_mem_handler = m0_palette_mem_handler,
};
