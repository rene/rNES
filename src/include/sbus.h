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
 * @file sbus.h
 * @see sbus.c
 */
#ifndef __SBUS_H__
#define __SBUS_H__
#ifndef SBUS_MEM
#define SBUS_MEM
#endif

#include "cartridge.h"
#include <stdint.h>

/** CPU's RAM memory size */
#define SBUS_CPU_RAM_SIZE 2049

/** Memory Operation */
enum _sbus_mem_op {
	/** Memory Read */
	MEM_READ,
	/** Memory Write */
	MEM_WRITE
};

/** DMA state */
enum _sbus_DMA {
	/** No DMA in progress */
	NO_DMA = 0,
	/** DMA was triggered */
	DMA_TRIGGERED,
	/** Waiting CPU to halt */
	DMA_WAIT_HALT_CPU,
	/** Read from memory */
	DMA_READ,
	/** Write to memory */
	DMA_WRITE,
	/** DMA transaction is complete */
	DMA_DONE
};

/** System BUS struct */
struct _sbus {
	/** CPU's RAM memory */
	uint8_t *cpu_ram;
	/** Game cartridge */
	cartridge_t *cartridge;
	/** CPU clock trigger */
	uint8_t clk_cpu;
	/** DMA state */
	uint8_t dma_st;
	/** DMA address */
	uint16_t dma_addr;
	/** DMA read value */
	uint8_t dma_value;
};

/** System BUS */
typedef struct _sbus sbus_t;

/** Memory operation */
typedef enum _sbus_mem_op mem_op_t;

int sbus_init(void);
int sbus_destroy(void);
int sbus_set_cartridge(cartridge_t *cartridge);
uint8_t sbus_read(uint16_t address);
void sbus_write(uint16_t address, uint8_t value);
uint16_t sbus_read16(uint16_t address);
void sbus_write16(uint16_t address, uint16_t value);
int sbus_register_debug_cb(void (*debug_cb)(mem_op_t, uint16_t, uint8_t));
int sbus_unregister_debug_cb(void);
void sbus_clock(void);

#endif /* __SBUS_H__ */
