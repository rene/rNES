/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rNES - Nintendo Entertainment System Emulator
 * Copyright 2024 Renê de Souza Pinto
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
 * @file mapper.h
 * @see mapper.c
 */
#ifndef __MAPPER_H__
#define __MAPPER_H__

#include "cartridge.h"
#include <stdint.h>

/** Number of mappers */
#define NUM_MAPPERS 256

/** Register a mapper */
#define REGISTER_MAPPER(a, i) (mappers[i] = a)

/** Memory operation type */
enum mem_op {
	/** Cartridge memory read */
	CMEM_READ = 0,
	/** Cartridge memory write */
	CMEM_WRITE
};

/** Mapper struct */
struct _mapper_t {
	/** Mapper identification */
	int id;
	/** Cartridge */
	cartridge_t *cartridge;
	/** Mapper initialization */
	int (*init)(struct _mapper_t *, cartridge_t *);
	/** Mapper reset */
	int (*reset)(struct _mapper_t *);
	/** Mapper finalization */
	int (*finalize)(struct _mapper_t *);
	/** PRG ROM memory handler callback */
	uint8_t (*prg_mem_handler)(struct _mapper_t *, enum mem_op, uint16_t,
							   uint8_t);
	/** CHR ROM/RAM memory handler callback */
	uint8_t (*chr_mem_handler)(struct _mapper_t *, enum mem_op, uint16_t,
							   uint8_t);
	/** VRAM memory handler callback */
	uint8_t (*vram_mem_handler)(struct _mapper_t *, enum mem_op, uint16_t,
								uint8_t);
	/** Palette memory handler callback */
	uint8_t (*palette_mem_handler)(struct _mapper_t *, enum mem_op, uint16_t,
								   uint8_t);
	/** Custom mapper data */
	void *data;
};

/** Mapper type */
typedef struct _mapper_t mapper_t;

/** List of mappers */
extern mapper_t *mappers[NUM_MAPPERS];

/* Prototypes */

int mapper_init(struct _mapper_t *m, cartridge_t *c);
int mapper_reset(struct _mapper_t *m);
int mapper_finalize(struct _mapper_t *m);

/* Functions exported from mappers */

uint8_t m0_vram_mem_handler(struct _mapper_t *m, enum mem_op op,
							uint16_t address, uint8_t value);
uint8_t m0_palette_mem_handler(struct _mapper_t *m, enum mem_op op,
							   uint16_t address, uint8_t value);

#endif /* __MAPPER_H__ */
