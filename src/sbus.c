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
 * @file sbus.c
 *
 * Performs system BUS emulation
 */
#include "sbus.h"
#include "apu.h"
#include "controller.h"
#include "cpu650x.h"
#include "ppu.h"
#include <errno.h>
#include <stdlib.h>

/** System Bus */
static sbus_t sbus;

/** Debug callback */
static void (*sbus_cb)(mem_op_t, uint16_t, uint8_t);

/**
 * Initialize System Bus
 * @return 0 on success, error code otherwise
 */
int sbus_init(void)
{
	sbus.cpu_ram = malloc(sizeof(uint8_t) * SBUS_CPU_RAM_SIZE);
	sbus.cartridge = NULL;
	sbus.clk_cpu = 1; /* Trigger on first PPU clock */
	sbus.dma_st = NO_DMA;
	sbus.dma_addr = 0;
	sbus.dma_value = 0;
	if (sbus.cpu_ram == NULL)
		return -ENOMEM;

	return 0;
}

/**
 * Finalize System Bus
 * @return 0 on success, error code otherwise
 */
int sbus_destroy(void)
{
	free(sbus.cpu_ram);
	return 0;
}

/**
 * Read one byte (8 bits) from the specified address
 * @param [in] address Memory address
 * @return Byte stored in the specified address
 */
uint8_t sbus_read(uint16_t address)
{
	uint8_t reg;

	if (sbus_cb)
		sbus_cb(MEM_READ, address, 0);

	/* CPU's RAM */
	if (address < 0x2000) {
		return sbus.cpu_ram[address & 0x07ff];
	} else if ((address >= 0x2000 && address <= 0x3fff) ||
			   (address == 0x4014)) {
		/* PPU's registers */
		if (address == 0x4014)
			return ppu_reg_read(0x4014);
		else
			return ppu_reg_read(0x2000 + (address & 0x7));
	} else if ((address >= 0x4000 && address <= 0x4013) ||
			   (address == 0x4015)) {
		/* APU's registers */
		return apu_reg_read(address);
	} else if (address == 0x4016 || address == 0x4017) {
		/* APU shared register */
		if (address == 0x4017)
			reg = apu_reg_read(address);
		else
			reg = 0;
		/* Controller registers */
		reg |= controller_reg_read(address);
		return reg;
	} else if (address >= 0x4020) {
		/* Cartridge Address Space */
		return cartridge_mem_read(sbus.cartridge, address, MEM_SYS);
	}

	return 0;
}

/**
 * Write one byte (8 bits) to the specified address
 * @param [out] address Memory address
 * @param [in] value Data to be written to the specified address
 */
void sbus_write(uint16_t address, uint8_t value)
{
	if (sbus_cb)
		sbus_cb(MEM_WRITE, address, value);

	/* CPU's RAM */
	if (address < 0x2000) {
		sbus.cpu_ram[address & 0x07ff] = value;
	} else if (address >= 0x2000 && address <= 0x3fff) {
		/* PPU's register */
		ppu_reg_write(0x2000 + (address & 0x7), value);
	} else if ((address >= 0x4000 && address <= 0x4013) ||
			   (address == 0x4015)) {
		/* APU's register */
		apu_reg_write(address, value);
	} else if (address == 0x4014) {
		/* Trigger DMA transaction */
		sbus.dma_st = DMA_TRIGGERED;
		sbus.dma_addr = ((value << 8) & 0xff00);
	} else if (address == 0x4016) {
		/* Controller registers */
		controller_reg_write(address, value);
	} else if (address == 0x4017) {
		/* APU frame sequencer register */
		apu_reg_write(address, value);
	} else if (address >= 0x4020) {
		/* Cartridge Address Space */
		cartridge_mem_write(sbus.cartridge, address, value, MEM_SYS);
	}
}

/**
 * Set the current game cartridge
 * @param [in,out] cartridge Cartridge's struct
 * @return 0 on success, -1 otherwise
 */
int sbus_set_cartridge(cartridge_t *cartridge)
{
	if (!cartridge)
		return -EINVAL;

	sbus.cartridge = cartridge;
	return 0;
}

/**
 * Register a debug callback
 * @param [in] debug_cb Debug callback function
 * @return 0 on success, -1 if there is a callback already registered
 * @note WARNING! This function *it is not* thread safe, if you are going
 * to run this code from multiple threads, make sure to add a lock
 * mechanism (e.g., semaphore).
 */
int sbus_register_debug_cb(void (*debug_cb)(mem_op_t, uint16_t, uint8_t))
{
	if (sbus_cb != NULL)
		return -1;

	sbus_cb = debug_cb;
	return 0;
}

/**
 * Unregister debug function callback
 * @return 0 on success, -1 if there is no callback registered
 * @note WARNING! This function *it is not* thread safe, if you are going
 * to run this code from multiple threads, make sure to add a lock
 * mechanism (e.g., semaphore).
 */
int sbus_unregister_debug_cb(void)
{
	if (sbus_cb == NULL)
		return -1;

	sbus_cb = NULL;
	return 0;
}

/**
 * System bus clock. Handles clocks from other modules and DMA transactions
 */
void sbus_clock(void)
{
	/* CPU's clock is 3x slower than PPU's clock */
	sbus.clk_cpu--;
	if (sbus.clk_cpu == 0) {
		/* Reset counter */
		sbus.clk_cpu = 3;
		/* Handle DMA */
		switch (sbus.dma_st) {
		case NO_DMA:
			/* No DMA in progress */
			break;

		case DMA_TRIGGERED:
			/* DMA was triggered, suspend the CPU */
			cpu_suspend();
			sbus.dma_st = DMA_WAIT_HALT_CPU;
			break;

		case DMA_WAIT_HALT_CPU:
			/* Check CPU state */
			if (cpu_get_state() == CPU_SUSPENDED)
				sbus.dma_st = DMA_READ;
			break;

		case DMA_READ:
			/* Read from memory */
			sbus.dma_value = sbus_read(sbus.dma_addr);
			sbus.dma_addr++;
			sbus.dma_st = DMA_WRITE;
			break;

		case DMA_WRITE:
			/* Check if we are done */
			if ((sbus.dma_addr & 0xff) == 0)
				sbus.dma_st = DMA_DONE;
			else
				sbus.dma_st = DMA_READ;

			/* Write to memory */
			ppu_reg_write(REG_OAMDATA, sbus.dma_value);
			break;

		case DMA_DONE:
			/* Reset DMA state */
			sbus.dma_st = NO_DMA;
			sbus.dma_addr = 0;
			sbus.dma_value = 0;
			/* Wake up CPU */
			cpu_wakeup();
			break;
		};
		/* CPU clock has no effect while CPU is suspended */
		cpu_clock();
		/* APU clock:
		 * apu_clock() runs at same speed of CPU, it will handle
		 * differences internally
		 */
		apu_clock();
	}

	/* PPU's clock tick (it drives the system's clock), it should keep it
	 * after CPU clock so we sync the read/write operations from the code
	 * with PPU operations
	 */
	ppu_clock();
}
