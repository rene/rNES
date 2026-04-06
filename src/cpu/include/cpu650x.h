/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * MOS650x - The MOS 6502 CPU emulator
 * Copyright 2021-2025 Renê de Souza Pinto
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
 * @file cpu650x.h
 * @see cpu650x.c
 */
#ifndef __CPU650X_H__
#define __CPU650X_H__

#include <stdint.h>

/** CPU's state */
enum _CPU_STATE {
	/** CPU is suspended */
	CPU_SUSPENDED = 0,
	/** CPU was requested to suspend (it will suspend) */
	CPU_REQ_SUSPEND = 1,
	/** CPU is running */
	CPU_RUNNING = 2
};

/** CPU internal representation */
struct _cpu650x {
	/** Accumulator */
	uint8_t A;
	/** X register */
	uint8_t X;
	/** Y register */
	uint8_t Y;
	/** P - Status register */
	union {
		struct {
			/** Carry */
			uint8_t C:1;
			/** Zero result */
			uint8_t Z:1;
			/** Interrupt disable */
			uint8_t I:1;
			/** Decimal mode */
			uint8_t D:1;
			/** Break command */
			uint8_t B:1;
			/** Expansion */
			uint8_t res:1;
			/** Overflow */
			uint8_t V:1;
			/** Negative result */
			uint8_t N:1;
		} flags;
		/** Register raw value */
		uint8_t reg;
	} P;
	/** Stack pointer register */
	uint8_t S;
	/** Program counter */
	uint16_t PC;
	/** Clock cycles */
	unsigned long clock_cycles;
	/** Remaining clock cycles for current instruction */
	int8_t clock_rcycles;
	/** IRQ pin status */
	uint8_t irq_pin;
	/** NMI pin status */
	uint8_t nmi_pin;
	/** NMI signal edge detection */
	uint8_t nmi_trigger;
	/** CPU running state */
	enum _CPU_STATE state;
};

/** CPU's state information */
struct _cpu650x_state {
	/** Current opcode */
	uint8_t opcode;
	/** Current opcode's name */
	const char *opcode_name;
	/** Current fetched address */
	uint16_t address;
	/** Current fetched data */
	uint16_t data;
	/** Current state of the CPU */
	struct _cpu650x CPU;
};

/** CPU 650x */
typedef struct _cpu650x cpu650x_t;

/** CPU 650x state information */
typedef struct _cpu650x_state cpu650x_state_t;

int cpu_init(void);
int cpu_destroy(void);
int cpu_suspend(void);
int cpu_wakeup(void);
enum _CPU_STATE cpu_get_state();
void cpu_clock(void);
void cpu_reset(void);
void cpu_trigger_irq(void);
void cpu_clear_irq(void);
void cpu_trigger_nmi(void);
void cpu_clear_nmi(void);
int cpu_register_kill_cb(void (*kill_cb)(cpu650x_state_t));
int cpu_unregister_kill_cb(void);
int cpu_register_debug_cb(void (*debug_cb)(cpu650x_state_t));
int cpu_unregister_debug_cb(void);
unsigned long cpu_get_cycles();

#endif /* __CPU650X_H__ */
