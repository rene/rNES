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
 * @file cartridge.c
 *
 * Implements the game cartridges and available mappers. This module should
 * handle the all the memory present on cartridges (PRG, CHR_ROM and/or
 * CHR_RAM) and also the VRAM area that is mapped to the PPU into a
 * separated address space.
 *
 * VRAM areas:
 * ----------
 * Address range  Size   Description
 * $0000-$0FFF    $1000  Pattern table 0
 * $1000-$1FFF    $1000  Pattern table 1
 * $2000-$23FF    $0400  Nametable 0
 * $2400-$27FF    $0400  Nametable 1
 * $2800-$2BFF    $0400  Nametable 2
 * $2C00-$2FFF    $0400  Nametable 3
 * $3000-$3EFF    $0F00  Mirrors of $2000-$2EFF
 */
#include "cartridge.h"
#include "mappers/mapper.h"
#include <errno.h>
#include <stdlib.h>

/** NES VRAM size (2KB) */
#define NES_VRAM_SIZE 2048

mapper_t *mapper;

/**
 * Prepare a cartridge and load a game from its ROM file
 * @param [in] pathname Pathname to the game ROM file
 * @param [out] cartridge Cartridge's struct
 * @return int 0 on success, error code otherwise
 */
int cartridge_load(const char *pathname, cartridge_t **cartridge)
{
	int ret, m;
	cartridge_t *new_cartridge;

	/* Load cartridge struct */
	new_cartridge = malloc(sizeof(cartridge_t));
	if (new_cartridge == NULL)
		return -ENOMEM;

	/* Load the ROM file */
	ret = load_rom(pathname, &new_cartridge->rom);
	if (ret < 0) {
		free(new_cartridge);
		return ret;
	}

	/* Initialize mapper */
	m = new_cartridge->rom->mapper;
	mapper = mappers[m];
	if (mapper == NULL) {
		free(new_cartridge);
		return -ENOTSUP;
	}

	/* Initialize VRAM */
	new_cartridge->vram = calloc(NES_VRAM_SIZE, sizeof(uint8_t));
	if (new_cartridge == NULL) {
		unload_rom(new_cartridge->rom);
		free(new_cartridge);
		return -ENOMEM;
	}

	/* Initialize CHR RAM (if needed) */
	if (new_cartridge->rom->chr_size == 0) {
		/* Allocate 8KB for CHR RAM */
		new_cartridge->chr_ram = 1;
		new_cartridge->rom->chr_rom = calloc(0x2000, sizeof(uint8_t));
		new_cartridge->rom->chr_size = 0x2000;
		if (new_cartridge->rom->chr_rom == NULL) {
			unload_rom(new_cartridge->rom);
			free(new_cartridge);
			return -ENOMEM;
		}
	} else {
		new_cartridge->chr_ram = 0;
	}

	*cartridge = new_cartridge;

	/* Execute mapper initialization function */
	mapper->init(mapper, new_cartridge);
	return 0;
}

/**
 * Unload a cartridge previously loaded
 * @param [in,out] cartridge Cartridge's struct
 */
void cartridge_unload(cartridge_t *cartridge)
{
	if (cartridge != NULL) {
		if (cartridge->chr_ram)
			free(cartridge->rom->chr_rom);
		mapper->finalize(mapper);
		unload_rom(cartridge->rom);
		free(cartridge->vram);
		free(cartridge);
	}
}

/**
 * Return cartridge's information
 * @param [in] cartridge Cartridge's struct
 * @return Cartridge information
 */
const cartridge_t *cartridge_info(cartridge_t *cartridge)
{
	/* We don't keep any special information outside the struct, so just
	 * return the cartridge struct as const
	 */
	return (const cartridge_t *)cartridge;
}

/**
 * Read cartridge's memory area
 * @param [in] cartridge Cartridge's struct
 * @param [in] address Memory address
 * @param [in] memspace Memory space (PPU or System Bus)
 * @return Memory stored value
 */
uint8_t cartridge_mem_read(cartridge_t *cartridge, uint16_t address,
						   mem_t memspace)
{
	switch (memspace) {
	case MEM_PPU:
		if (address >= 0 && address <= 0x1fff) {
			return mapper->chr_mem_handler(mapper, CMEM_READ, address, 0);
		} else if (address >= 0x2000 && address <= 0x3eff) {
			return mapper->vram_mem_handler(mapper, CMEM_READ, address, 0);
		} else if (address >= 0x3f00 && address <= 0x3fff) {
			return mapper->palette_mem_handler(mapper, CMEM_READ, address, 0);
		}
		break;
	case MEM_SYS:
		return mapper->prg_mem_handler(mapper, CMEM_READ, address, 0);
	}

	return 0;
}

/**
 * Write cartridge's memory area
 * @param [in] cartridge Cartridge's struct
 * @param [in] address Memory address
 * @param [in] value Value to be stored
 * @param [in] memspace Memory space (PPU or System Bus)
 */
void cartridge_mem_write(cartridge_t *cartridge, uint16_t address,
						 uint8_t value, mem_t memspace)
{
	switch (memspace) {
	case MEM_PPU:
		/* We might have CHR RAM memory depending on the mapper */
		if (address >= 0 && address <= 0x1fff) {
			mapper->chr_mem_handler(mapper, CMEM_WRITE, address, value);
		} else if (address >= 0x2000 && address <= 0x3eff) {
			mapper->vram_mem_handler(mapper, CMEM_WRITE, address, value);
		} else if (address >= 0x3f00 && address <= 0x3fff) {
			mapper->palette_mem_handler(mapper, CMEM_WRITE, address, value);
		}
		break;
	case MEM_SYS:
		mapper->prg_mem_handler(mapper, CMEM_WRITE, address, value);
		break;
	}
}
