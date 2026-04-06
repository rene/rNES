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
 * @file m0_NROM.c
 *
 * Implements the mapper 0 (NROM).
 */
#include <errno.h>
#include <mappers/mapper.h>
#include <stdlib.h>

/** 16KB PRG ROM */
#define S_16KB 16384
/** 32KB PRG ROM */
#define S_32KB 32768

/** NROM custom data */
struct _m0_mapper {
	/** PRG RAM */
	uint8_t *prg_ram;
	/** CHR RAM */
	uint8_t *chr_mem;
};

/**
 * Translate addresses of PRG ROM
 * @param [in] m Mapper's struct
 * @param [in] op Memory operation (R/W)
 * @param [in] source Source address
 * @return uint16_t Physical address
 */
static uint16_t prg_addr_tr(struct _mapper_t *m, enum mem_op op,
							uint16_t source)
{
	cartridge_t *cartridge = m->cartridge;

	switch (cartridge->rom->prg_size) {
	case S_16KB:
		if (source >= 0xc000)
			return (source - 0xc000);
		else
			return (source - 0x8000);
	case S_32KB:
		return (source - 0x8000);
	}
	return source;
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
	cartridge_t *cartridge = m->cartridge;

	/* VRAM: Name tables */
	if (source >= 0x2000 && source <= 0x2fff) {
		/* Translate according to the mirroring mode */
		if (cartridge->rom->mirroring == VERTICAL_MIRRORING) {
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
 * Translate mirror addresses of Palette memory area
 * @param [in] m Mapper's struct
 * @param [in] op Memory operation (R/W)
 * @param [in] source Source address
 * @return uint16_t Physical address
 */
static uint16_t palette_addr_tr(struct _mapper_t *m, enum mem_op op,
								uint16_t source)
{
	uint16_t addr = source & 0x1f;
	/* Palette mirrors */
	if (op == CMEM_WRITE) {
		switch (addr) {
		case 0x10:
		case 0x14:
		case 0x18:
		case 0x1c:
			return (addr - 0x10);
		}
	} else {
		switch (addr & 0x0f) {
		case 0x00:
		case 0x04:
		case 0x08:
		case 0x0c:
			return 0;
		}
	}
	/* No translation */
	return addr;
}

/**
 * Mapper initialization function
 * @param [in,out] m Mapper
 * @param [in,out] c Cartridge
 * @return int 0 on success, error code otherwise
 */
static int m0_mapper_init(struct _mapper_t *m, cartridge_t *c)
{
	struct _m0_mapper *m0;

	if (m) {
		m->cartridge = c;

		m->data = calloc(sizeof(struct _m0_mapper), 1);
		if (m->data == NULL)
			return -ENOMEM;

		m0 = (struct _m0_mapper *)m->data;
	} else {
		return -EINVAL;
	}

	/* Check PRG RAM */
	if (c->rom->header->flag10.ines.prg_ram) {
		/* Allocates 8 KB of PRG RAM */
		m0->prg_ram = calloc(sizeof(uint8_t), 0x2000);
		if (!m0->prg_ram) {
			free(m0);
			return -ENOMEM;
		}
	} else {
		m0->prg_ram = m->cartridge->rom->prg_rom;
	}

	/* Check CHR RAM */
	if (c->rom->chr_size == 0) {
		/* Allocates 8 KB of CHR RAM */
		m0->chr_mem = calloc(sizeof(uint8_t), 0x2000);
		if (!m0->chr_mem) {
			free(m0->prg_ram);
			free(m0);
			return -ENOMEM;
		}
	} else {
		m0->chr_mem = m->cartridge->rom->chr_rom;
	}

	return 0;
}

/**
 * Mapper finalization function
 * @param [in,out] m Mapper
 * @return int 0 on success, error code otherwise
 */
static int m0_mapper_finalize(struct _mapper_t *m)
{
	struct _m0_mapper *m0 = (struct _m0_mapper *)m->data;

	free(m0->prg_ram);
	free(m0->chr_mem);
	free(m0);
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
uint8_t m0_prg_mem_handler(struct _mapper_t *m, enum mem_op op,
						   uint16_t address, uint8_t value)
{
	cartridge_t *cartridge = m->cartridge;
	struct _m0_mapper *m0 = (struct _m0_mapper *)m->data;
	uint16_t idx;

	if (address >= 0x6000 && address <= 0x7fff) {
		idx = (address & 0x1fff);
		if (op == CMEM_READ)
			return m0->prg_ram[idx];

		m0->prg_ram[idx] = value;
		return value;
	}

	idx = prg_addr_tr(m, op, address);
	if (op == CMEM_READ)
		return cartridge->rom->prg_rom[idx];

	return value;
}

/**
 * CHR ROM handler
 * @param [in,out] Mapper struct
 * @param [in] op Memory operation (READ/WRITE)
 * @param [in] address Memory address
 * @param [in] value Value to write (when op is WRITE)
 * @uint8_t Value read/written from/to memory
 */
uint8_t m0_chr_mem_handler(struct _mapper_t *m, enum mem_op op,
						   uint16_t address, uint8_t value)
{
	struct _m0_mapper *m0 = (struct _m0_mapper *)m->data;

	if (op == CMEM_READ)
		return m0->chr_mem[address];

	/* Let's assume we have CHR RAM */
	m0->chr_mem[address] = value;
	return value;
}

/**
 * VRAM's memory handler
 * @param [in,out] Mapper struct
 * @param [in] op Memory operation (READ/WRITE)
 * @param [in] address Memory address
 * @param [in] value Value to write (when op is WRITE)
 * @uint8_t Value read/written from/to memory
 */
uint8_t m0_vram_mem_handler(struct _mapper_t *m, enum mem_op op,
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

/**
 * Palette's memory handler
 * @param [in,out] Mapper struct
 * @param [in] op Memory operation (READ/WRITE)
 * @param [in] address Memory address
 * @param [in] value Value to write (when op is WRITE)
 * @uint8_t Value read/written from/to memory
 */
uint8_t m0_palette_mem_handler(struct _mapper_t *m, enum mem_op op,
							   uint16_t address, uint8_t value)
{
	uint16_t idx;
	cartridge_t *cartridge = m->cartridge;

	idx = palette_addr_tr(m, op, address);
	if (op == CMEM_READ)
		return cartridge->palette_idx[idx];

	cartridge->palette_idx[idx] = value;
	return value;
}

/** Mapper 0 struct */
mapper_t m0_NROM = {
	.init = m0_mapper_init,
	.reset = mapper_reset,
	.finalize = m0_mapper_finalize,
	.prg_mem_handler = m0_prg_mem_handler,
	.chr_mem_handler = m0_chr_mem_handler,
	.vram_mem_handler = m0_vram_mem_handler,
	.palette_mem_handler = m0_palette_mem_handler,
};
