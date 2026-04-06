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
 * @file cartridge.h
 * @see cartridge.c
 */
#ifndef __CARTRIDGE_H__
#define __CARTRIDGE_H__

#include "romdec.h"
#include <stdint.h>

/** Game cartridge struct */
struct _cartridge {
	/** ROM struct */
	rom_t *rom;
	/** PPU's VRAM */
	uint8_t *vram;
	/** PPU's Palette indexes */
	uint8_t palette_idx[32];
	/** Mapper */
	int mapper;
	/** Has CHR RAM */
	uint8_t chr_ram;
};

/** Memory address space */
enum _MEM_SPACE {
	MEM_SYS,
	MEM_PPU,
};

/** Game cartridge */
typedef struct _cartridge cartridge_t;

/** Memory address space type */
typedef enum _MEM_SPACE mem_t;

int cartridge_load(const char *pathname, cartridge_t **cartridge);
void cartridge_unload(cartridge_t *cartridge);
const cartridge_t *cartridge_info(cartridge_t *cartridge);
uint8_t cartridge_mem_read(cartridge_t *cartridge, uint16_t address,
						   mem_t memspace);
void cartridge_mem_write(cartridge_t *cartridge, uint16_t address,
						 uint8_t value, mem_t memspace);

#endif /* __CARTRIDGE_H__ */
