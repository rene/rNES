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
 * @file cpu650x.c
 * 650x CPU Emulation
 *
 * This code is developed to be high portable to other emulators and/or
 * architectures. Therefore some high level functions must be provided:
 *
 * RAM memory:
 * sbus_read(address): Read 1 byte from memory address
 * sbus_write(address, value): Writes 1 byte (value) to memory address
 * sbus_init(): To initialize system bus
 * sbus_destroy(): To finalize system bus
 *
 * Functions related to RAM should be defined along with SBUS_MEM. If they are
 * not provided, a single vector cpu_mem[65536] will be declared and will store
 * CPU's RAM memory.
 *
 * To make the CPU execution thread safe, CPU_THREAD_SAFE must be defined
 * and the following functions implemented:
 *
 * s_init(): Initialize semaphores
 * s_destroy(): Destroy semaphores
 * s_lock(): Semaphore UP
 * s_unlock(): Semaphore DOWN
 *
 * A standard implementation is already provided for POSIX environments.
 *
 * The following defines can be used for debugging:
 *     CPU_PRINT_INSTRUCTION : Print each instruction under execution
 *     CPU_TEST_CODE : Sample main() function with a test code
 */
#include <stddef.h>
#include "cpu650x.h"

#if defined(CPU_PRINT_INSTRUCTION) || defined(CPU_TEST_CODE)
#include <stdio.h>
#endif

/**
 * Check and define RAM memory in case system bus functions are not
 * available
 */
#ifndef SBUS_MEM
uint8_t cpu_mem[65536];
#define sbus_read(addr) cpu_mem[addr]
#define sbus_write(addr, value) do { cpu_mem[addr] = value; } while (0)
#define sbus_init()
#define sbus_destroy()
#else
#include "sbus.h"
#endif

/** Set flag value based on input condition */
#define SET_PFLAG(flag, cond) do { flag = (cond) ? 1 : 0; } while (0)

/** Break command status flag mask */
#define BRK_FLAG_MASK (1 << 4)

/** Expansion status flag mask */
#define EXP_FLAG_MASK (1 << 5)

/** Expand CPU instruction information struct (no additional clock cycles) */
#define _I(inst, mode, clocks) { inst, mode, clocks, 0, #inst }

/** Expand CPU instruction information struct (with additional clock cycles) */
#define _H(inst, mode, clocks) { inst, mode, clocks, 1, #inst }

/** Push data into stack */
#define S_PUSH(data) do {\
		sbus_write(0x100 + CPU.S, data); \
		CPU.S--; \
	} while (0)

/** Pop data from stack */
#define S_POP() (\
	{\
		CPU.S++; \
		uint8_t __stack_data = sbus_read(0x100 + CPU.S); \
		__stack_data; \
	})

/** Check and perform conditional branch (relative jump) */
#define COND_BRANCH(cond) do {\
		uint16_t addr; \
		if (cond) { \
			addr = CPU.PC + fetched_addr; \
			CPU.clock_rcycles++; \
			if ((addr & 0xff00) != (CPU.PC & 0xff00)) \
				CPU.clock_rcycles++; \
			CPU.PC = addr; \
		} \
	} while (0)

/**
 * Some memory mapped devices change their state by only reading at a
 * certain memory address, so we can't make any unneeded memory access
 */
#define FETCH_DATA() do { fetched_data = sbus_read(fetched_addr); } while (0)

/** Addressing modes */
enum _addr_modes {
	/** [Accum]/[Implied] Implied: OP code */
	mIMP = 0,
	/** [IMM] Immediate: OP code + Data */
	mIMM,
	/** [ABS] Absolute: OP code + ADL + ADH */
	mABS,
	/** [ABI] Absolute indirect: OP code + IAL + IAH */
	mABI,
	/** [ZP] Zero page: OP code + ADL */
	mZPG,
	/** [Relative] Relative: OP code + Offset */
	mREL,
	/** [ZP,X] Zero page indexed X: Op code + BAL */
	mZPX,
	/** [ZP,Y] Zero page indexed Y: Op code + BAL */
	mZPY,
	/** [ABS,X] Absolute indexed X: Op code + BAL + BAH */
	mABX,
	/** [ABS,Y] Absolute indexed Y: Op code + BAL + BAH */
	mABY,
	/** [IND,X] Indexed indirect X: Op code + BAL */
	mINX,
	/** [IND,Y] Indirect indexed Y: Op code + IAL */
	mINY,
};

/** CPU instruction information */
struct _cpu_instruction {
	/** Instruction implementation function */
	void (*instruction)(void);
	/** Addressing mode */
	enum _addr_modes mode;
	/** Total clock cycles to execute */
	uint8_t clock_cycles;
	/** Additional clock cycles on page crossing */
	uint8_t add_cycles;
	/** Instruction name */
	const char *name;
};


/** 650x CPU instance */
static cpu650x_t CPU;

/** Fetched data */
static uint8_t fetched_data;

/** Fetched address */
static uint16_t fetched_addr;

/** Current op code */
static uint8_t opcode;

/** Current addressing mode */
static enum _addr_modes addrmode;

/** CPU's state */
cpu650x_state_t state_info;

/** Kill function callback */
static void (*cpu_kill_cb)(struct _cpu650x_state);

/** Debug function callback */
static void (*cpu_debug_cb)(struct _cpu650x_state);

/* NOTE: POSIX semaphores and other lock mechanisms on Windows have shown
 * horrible performance during the emulation, so it is better not to use them
 * for now.
 */
#if !defined(CPU_THREAD_SAFE) || defined(_WIN32)
#ifdef CPU_THREAD_SAFE
#warning CPU thread safe is not supported on Windows.
#endif
/** Generic sem_t type */
typedef int sem_t;
/** Initialize semaphores */
inline static int s_init(sem_t *sem) { return 0; };
/** Destroy sempahores */
inline static int s_destroy(sem_t *sem) { return 0; };
/** Semaphore DOWN function */
inline static int s_lock(sem_t *sem) { return 0; };
/** Semaphore UP function */
inline static int s_unlock(sem_t *sem) { return 0; };
#elif defined(_POSIX_C_SOURCE)
#include <semaphore.h>
/** Initialize sempahores */
inline static int s_init(sem_t *sem) {
	return sem_init(sem, 0, 1);
}
/** Destroy sempahores */
inline static int s_destroy(sem_t *sem) {
	return sem_destroy(sem);
};
/** Semaphore DOWN function */
inline static int s_lock(sem_t *sem) {
	return sem_wait(sem);
};
/** Semaphore UP function */
inline static int s_unlock(sem_t *sem) {
	return sem_post(sem);
};
#endif
/** Mutex for CPU instruction */
static sem_t cpu_lock;
/** Mutex for NMI */
static sem_t nmi_lock;
/** Mutex for IRQ */
static sem_t irq_lock;

/* ----------------------------------------------------------------------------
 * CPU instruction implementation
 * ----------------------------------------------------------------------------
 */
static void ADC(void)
{
	uint16_t res;

	FETCH_DATA();
	res = (uint16_t)CPU.A +
		(uint16_t)fetched_data + (uint16_t)CPU.P.flags.C;
	SET_PFLAG(CPU.P.flags.V, ((~(CPU.A ^ fetched_data) &
				((uint16_t)CPU.A ^ res)) & 0x80) != 0);
	CPU.A = (res & 0xff);
	SET_PFLAG(CPU.P.flags.C, res > 255);
	SET_PFLAG(CPU.P.flags.N, (CPU.A & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.A == 0);
}

static void AND(void)
{
	FETCH_DATA();
	CPU.A &= fetched_data;
	SET_PFLAG(CPU.P.flags.N, (CPU.A & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.A == 0);
}

static void ASL(void)
{
	if (addrmode == mIMP) {
		SET_PFLAG(CPU.P.flags.C, (CPU.A & 0x80) != 0);
		CPU.A = (CPU.A << 1) & 0xfe;
		SET_PFLAG(CPU.P.flags.N, (CPU.A & 0x80) != 0);
		SET_PFLAG(CPU.P.flags.Z, CPU.A == 0);
	} else {
		FETCH_DATA();
		SET_PFLAG(CPU.P.flags.C, (fetched_data & 0x80) != 0);
		fetched_data = (fetched_data << 1) & 0xfe;
		SET_PFLAG(CPU.P.flags.N, (fetched_data & 0x80) != 0);
		SET_PFLAG(CPU.P.flags.Z, fetched_data == 0);
		sbus_write(fetched_addr, fetched_data);
	}
}

static void BCC(void)
{
	COND_BRANCH(CPU.P.flags.C == 0);
}

static void BCS(void)
{
	COND_BRANCH(CPU.P.flags.C == 1);
}

static void BEQ(void)
{
	COND_BRANCH(CPU.P.flags.Z == 1);
}

static void BIT(void)
{
	uint8_t res;

	FETCH_DATA();
	res = (fetched_data & CPU.A);
	SET_PFLAG(CPU.P.flags.N, (fetched_data & (1 << 7)) != 0);
	SET_PFLAG(CPU.P.flags.V, (fetched_data & (1 << 6)) != 0);
	SET_PFLAG(CPU.P.flags.Z, res == 0);
}

static void BMI(void)
{
	COND_BRANCH(CPU.P.flags.N == 1);
}

static void BNE(void)
{
	COND_BRANCH(CPU.P.flags.Z == 0);
}

static void BPL(void)
{
	COND_BRANCH(CPU.P.flags.N == 0);
}

static void BRK(void)
{
	uint8_t pch, pcl, adh, adl;

	/* Serve interrupt */
	CPU.PC++;
	pch = ((CPU.PC & 0xff00) >> 8) & 0xff;
	pcl = (uint8_t)(CPU.PC & 0x00ff);
	S_PUSH(pch);
	S_PUSH(pcl);

	/* B flag should be 1 only in stack */
	S_PUSH(CPU.P.reg | EXP_FLAG_MASK | BRK_FLAG_MASK);
	CPU.P.flags.I = 1;

	/* Fetch PC from vector */
	adl = sbus_read(0xfffe);
	adh = sbus_read(0xffff);
	CPU.PC = (((uint16_t)adh << 8) & 0xff00) | (adl & 0xff);
}

static void BVC(void)
{
	COND_BRANCH(CPU.P.flags.V == 0);
}

static void BVS(void)
{
	COND_BRANCH(CPU.P.flags.V == 1);
}

static void CLC(void)
{
	CPU.P.flags.C = 0;
}

static void CLD(void)
{
	CPU.P.flags.D = 0;
}

static void CLI(void)
{
	CPU.P.flags.I = 0;
}

static void CLV(void)
{
	CPU.P.flags.V = 0;
}

static void CMP(void)
{
	uint8_t res;

	FETCH_DATA();
	res = CPU.A - fetched_data;
	SET_PFLAG(CPU.P.flags.N, (res & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, res == 0);
	SET_PFLAG(CPU.P.flags.C, fetched_data <= CPU.A);
}

static void CPX(void)
{
	uint8_t res;

	FETCH_DATA();
	res = CPU.X - fetched_data;
	SET_PFLAG(CPU.P.flags.N, (res & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, res == 0);
	SET_PFLAG(CPU.P.flags.C, CPU.X >= fetched_data);
}

static void CPY(void)
{
	uint8_t res;

	FETCH_DATA();
	res = CPU.Y - fetched_data;
	SET_PFLAG(CPU.P.flags.N, (res & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, res == 0);
	SET_PFLAG(CPU.P.flags.C, CPU.Y >= fetched_data);
}

static void DEC(void)
{
	uint8_t res;

	FETCH_DATA();
	res	= fetched_data - 1;
	sbus_write(fetched_addr, res);
	SET_PFLAG(CPU.P.flags.N, (res & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, res == 0);
}

static void DEX(void)
{
	CPU.X = CPU.X - 1;
	SET_PFLAG(CPU.P.flags.N, (CPU.X & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.X == 0);
}

static void DEY(void)
{
	CPU.Y = CPU.Y - 1;
	SET_PFLAG(CPU.P.flags.N, (CPU.Y & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.Y == 0);
}

static void EOR(void)
{
	FETCH_DATA();
	CPU.A ^= fetched_data;
	SET_PFLAG(CPU.P.flags.N, (CPU.A & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.A == 0);
}

static void INC(void)
{
	uint8_t res;

	FETCH_DATA();
	res = fetched_data + 1;
	sbus_write(fetched_addr, res);
	SET_PFLAG(CPU.P.flags.N, (res & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, res == 0);
}

static void INX(void)
{
	CPU.X = CPU.X + 1;
	SET_PFLAG(CPU.P.flags.N, (CPU.X & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.X == 0);
}

static void INY(void)
{
	CPU.Y = CPU.Y + 1;
	SET_PFLAG(CPU.P.flags.N, (CPU.Y & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.Y == 0);
}

static void JMP(void)
{
	CPU.PC = fetched_addr;
}

static void JSR(void)
{
	uint8_t pch, pcl;

	CPU.PC--;
	pch = ((CPU.PC & 0xff00) >> 8) & 0xff;
	pcl = (uint8_t)(CPU.PC & 0x00ff);
	S_PUSH(pch);
	S_PUSH(pcl);
	CPU.PC = fetched_addr;
}

static void LDA(void)
{
	FETCH_DATA();
	CPU.A = fetched_data;
	SET_PFLAG(CPU.P.flags.N, (CPU.A & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.A == 0);
}

static void LDX(void)
{
	FETCH_DATA();
	CPU.X = fetched_data;
	SET_PFLAG(CPU.P.flags.N, (CPU.X & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.X == 0);
}

static void LDY(void)
{
	FETCH_DATA();
	CPU.Y = fetched_data;
	SET_PFLAG(CPU.P.flags.N, (CPU.Y & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.Y == 0);
}

static void LSR(void)
{
	if (addrmode == mIMP) {
		SET_PFLAG(CPU.P.flags.C, (CPU.A & 0x1) != 0);
		CPU.A         = ((CPU.A >> 1) & 0x7f);
		CPU.P.flags.N = 0;
		SET_PFLAG(CPU.P.flags.Z, CPU.A == 0);
	} else {
		FETCH_DATA();
		SET_PFLAG(CPU.P.flags.C, (fetched_data & 0x1) != 0);
		fetched_data  = ((fetched_data >> 1) & 0x7f);
		CPU.P.flags.N = 0;
		SET_PFLAG(CPU.P.flags.Z, fetched_data == 0);
		sbus_write(fetched_addr, fetched_data);
	}
}

static void NOP(void)
{
	/* Nothing to do */
}

static void ORA(void)
{
	FETCH_DATA();
	CPU.A |= fetched_data;
	SET_PFLAG(CPU.P.flags.N, (CPU.A & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.A == 0);
}

static void PHA(void)
{
	S_PUSH(CPU.A);
}

static void PHP(void)
{
	S_PUSH(CPU.P.reg | BRK_FLAG_MASK | EXP_FLAG_MASK);
}

static void PLA(void)
{
	CPU.A = S_POP();
	SET_PFLAG(CPU.P.flags.N, (CPU.A & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.A == 0);
}

static void PLP(void)
{
	CPU.P.reg = S_POP();
}

static void ROL(void)
{
	uint8_t tmp = CPU.P.flags.C;

	if (addrmode == mIMP) {
		SET_PFLAG(CPU.P.flags.C, (CPU.A & 0x80) != 0);
		CPU.A = ((CPU.A << 1) & 0xfe) | tmp;
		SET_PFLAG(CPU.P.flags.N, (CPU.A & 0x80) != 0);
		SET_PFLAG(CPU.P.flags.Z, CPU.A == 0);
	} else {
		FETCH_DATA();
		SET_PFLAG(CPU.P.flags.C, (fetched_data & 0x80) != 0);
		fetched_data = ((fetched_data << 1) & 0xfe) | tmp;
		SET_PFLAG(CPU.P.flags.N, (fetched_data & 0x80) != 0);
		SET_PFLAG(CPU.P.flags.Z, fetched_data == 0);
		sbus_write(fetched_addr, fetched_data);
	}
}

static void ROR(void)
{
	uint8_t tmp = CPU.P.flags.C;

	if (addrmode == mIMP) {
		SET_PFLAG(CPU.P.flags.C, (CPU.A & 0x1) != 0);
		CPU.A = ((CPU.A >> 1) & 0x7f) | ((tmp << 7) & 0x80);
		CPU.P.flags.N = tmp;
		SET_PFLAG(CPU.P.flags.Z, CPU.A == 0);
	} else {
		FETCH_DATA();
		SET_PFLAG(CPU.P.flags.C, (fetched_data & 0x1) != 0);
		fetched_data  = ((fetched_data >> 1) & 0x7f)
			| ((tmp << 7) & 0x80);
		CPU.P.flags.N = tmp;
		SET_PFLAG(CPU.P.flags.Z, fetched_data == 0);
		sbus_write(fetched_addr, fetched_data);
	}
}

static void RTI(void)
{
	uint16_t addr;
	uint8_t pch, pcl;

	CPU.P.reg = S_POP();
	pcl  = S_POP();
	pch  = S_POP();
	addr = (((uint16_t)pch << 8) & 0xff00) | ((uint16_t)pcl & 0x00ff);
	CPU.PC = addr;
}

static void RTS(void)
{
	uint16_t addr;
	uint8_t pch, pcl;

	pcl  = S_POP();
	pch  = S_POP();
	addr = (((uint16_t)pch << 8) & 0xff00) | ((uint16_t)pcl & 0x00ff);
	CPU.PC = addr + 1;
}

static void SBC(void)
{
	uint16_t tmp;
	uint16_t res;

	FETCH_DATA();
	tmp = (uint16_t)fetched_data ^ 0x00ff;
	res = (uint16_t)CPU.A +
		(uint16_t)tmp + (uint16_t)CPU.P.flags.C;
	SET_PFLAG(CPU.P.flags.V, ((~(CPU.A ^ tmp) &
				((uint16_t)CPU.A ^ res)) & 0x80) != 0);
	CPU.A = (res & 0xff);
	SET_PFLAG(CPU.P.flags.C, res > 255);
	SET_PFLAG(CPU.P.flags.N, (CPU.A & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.A == 0);
}

static void SEC(void)
{
	CPU.P.flags.C = 1;
}

static void SED(void)
{
	/* TODO: Implement decimal mode */
	CPU.P.flags.D = 1;
}

static void SEI(void)
{
	CPU.P.flags.I = 1;
}

static void STA(void)
{
	sbus_write(fetched_addr, CPU.A);
}

static void STX(void)
{
	sbus_write(fetched_addr, CPU.X);
}

static void STY(void)
{
	sbus_write(fetched_addr, CPU.Y);
}

static void TAX(void)
{
	CPU.X = CPU.A;
	SET_PFLAG(CPU.P.flags.N, (CPU.X & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.X == 0);
}

static void TAY(void)
{
	CPU.Y = CPU.A;
	SET_PFLAG(CPU.P.flags.N, (CPU.Y & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.Y == 0);
}

static void TSX(void)
{
	CPU.X = CPU.S;
	SET_PFLAG(CPU.P.flags.N, (CPU.X & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.X == 0);
}

static void TXA(void)
{
	CPU.A = CPU.X;
	SET_PFLAG(CPU.P.flags.N, (CPU.A & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.A == 0);
}

static void TXS(void)
{
	CPU.S = CPU.X;
}

static void TYA(void)
{
	CPU.A = CPU.Y;
	SET_PFLAG(CPU.P.flags.N, (CPU.A & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.A == 0);
}

/* ------------------------- Unofficial op codes ------------------------- */

static void KIL(void)
{
	/* Stalls CPU */
	CPU.clock_rcycles = -1;
	/* Call kill callback (if registerd) */
	if (cpu_kill_cb)
		cpu_kill_cb(state_info);
}

static void SLO(void)
{
	ASL();
	ORA();
}

static void RLA(void)
{
	ROL();
	AND();
}

static void SRE(void)
{
	LSR();
	EOR();
}

static void RRA(void)
{
	ROR();
	ADC();
}

static void SAX(void)
{
	uint8_t val = (CPU.A & CPU.X);

	sbus_write(fetched_addr, val);
}

static void AHX(void)
{
	uint8_t H = ((fetched_addr >> 8) & 0xff);

	sbus_write(fetched_addr, (CPU.A & CPU.X & H));
}

static void LAX(void)
{
	if (addrmode == mIMM) {
		LDA();
		TAX();
	} else {
		LDA();
		LDX();
	}
}

static void DCP(void)
{
	DEC();
	fetched_data--; /* DEC will not update fetched_data */
	CMP();
}

static void ISC(void)
{
	INC();
	fetched_data++; /* INC will not update fetched_data */
	SBC();
}

static void ANC(void)
{
	AND();
	SET_PFLAG(CPU.P.flags.C, (CPU.A & 0x80) != 0);
}

static void ALR(void)
{
	AND();
	addrmode = mIMP;
	LSR();
}

static void ARR(void)
{
	AND();
	addrmode = mIMP;
	ROR();
	/* C flag is Accumulator's bit 6 and V is bit 6 xor bit 5 */
	SET_PFLAG(CPU.P.flags.C, (CPU.A & 0x40) != 0);
	SET_PFLAG(CPU.P.flags.V, ((CPU.A ^ (CPU.A << 1)) & 0x40) != 0);
}

static void XAA(void)
{
	TXA();
	AND();
}

static void SXC(void)
{
	SBC();
	NOP();
}

static void TAS(void)
{
	uint8_t res = (CPU.A & CPU.X);

	CPU.S = res;
	sbus_write(fetched_addr, res);
}

static void LAS(void)
{
	uint8_t res;

	FETCH_DATA();
	res = (fetched_data & CPU.S);
	CPU.A = res;
	CPU.X = res;
	CPU.S = res;
}

static void AXS(void) /* SBX | SAX */
{
	uint16_t res;

	FETCH_DATA();
	res = (CPU.A & CPU.X) - fetched_data;
	CPU.X = (res & 0xff);
	SET_PFLAG(CPU.P.flags.C, res < 0x100);
	SET_PFLAG(CPU.P.flags.N, (CPU.X & 0x80) != 0);
	SET_PFLAG(CPU.P.flags.Z, CPU.X == 0);
}

static void SHX(void) /* SXA */
{
	uint8_t H = ((fetched_addr >> 8) + 1) & 0xff;
	uint8_t v = (CPU.X & H);
	uint16_t addr = (fetched_addr & 0x00ff) | ((v << 8) & 0xff00);

	sbus_write(addr, (CPU.X & H));
}

static void SHY(void) /* SAY | SYA */
{
	uint8_t H = ((fetched_addr >> 8) + 1) & 0xff;
	uint8_t v = (CPU.Y & H);
	uint16_t addr = (fetched_addr & 0x00ff) | ((v << 8) & 0xff00);

	sbus_write(addr, (CPU.Y & H));
}
/* ------------------------------------------------------------------------- */

/**
 * OP Code matrix
 */
static const struct _cpu_instruction op_codes[] = {
/* Columns:
 *        0                 1                 2                 3
 *        4                 5                 6                 7
 *        8                 9                 A                 B
 *        C                 D                 E                 F
 */
/*0*/
	_I(BRK, mIMP, 7), _I(ORA, mINX, 6), _I(KIL, mIMP, 0), _I(SLO, mINX, 8),
	_I(NOP, mZPG, 3), _I(ORA, mZPG, 3), _I(ASL, mZPG, 5), _I(SLO, mZPG, 5),
	_I(PHP, mIMP, 3), _I(ORA, mIMM, 2), _I(ASL, mIMP, 2), _I(ANC, mIMM, 2),
	_I(NOP, mABS, 4), _I(ORA, mABS, 4), _I(ASL, mABS, 6), _I(SLO, mABS, 6),

/*1*/
	_I(BPL, mREL, 2), _H(ORA, mINY, 5), _I(KIL, mIMP, 0), _I(SLO, mINY, 8),
	_I(NOP, mZPX, 4), _I(ORA, mZPX, 4), _I(ASL, mZPX, 6), _I(SLO, mZPX, 6),
	_I(CLC, mIMP, 2), _H(ORA, mABY, 4), _I(NOP, mIMP, 2), _I(SLO, mABY, 7),
	_H(NOP, mABX, 4), _H(ORA, mABX, 4), _I(ASL, mABX, 7), _I(SLO, mABX, 7),

/*2*/
	_I(JSR, mABS, 6), _I(AND, mINX, 6), _I(KIL, mIMP, 0), _I(RLA, mINX, 8),
	_I(BIT, mZPG, 3), _I(AND, mZPG, 3), _I(ROL, mZPG, 5), _I(RLA, mZPG, 5),
	_I(PLP, mIMP, 4), _I(AND, mIMM, 2), _I(ROL, mIMP, 2), _I(ANC, mIMM, 2),
	_I(BIT, mABS, 4), _I(AND, mABS, 4), _I(ROL, mABS, 6), _I(RLA, mABS, 6),

/*3*/
	_H(BMI, mREL, 2), _H(AND, mINY, 5), _I(KIL, mIMP, 0), _I(RLA, mINY, 8),
	_I(NOP, mZPX, 4), _I(AND, mZPX, 4), _I(ROL, mZPX, 6), _I(RLA, mZPX, 6),
	_I(SEC, mIMP, 2), _H(AND, mABY, 4), _I(NOP, mIMP, 2), _I(RLA, mABY, 7),
	_H(NOP, mABX, 4), _H(AND, mABX, 4), _I(ROL, mABX, 7), _I(RLA, mABX, 7),

/*4*/
	_I(RTI, mIMP, 6), _I(EOR, mINX, 6), _I(KIL, mIMP, 0), _I(SRE, mINX, 8),
	_I(NOP, mZPG, 3), _I(EOR, mZPG, 3), _I(LSR, mZPG, 5), _I(SRE, mZPG, 5),
	_I(PHA, mIMP, 3), _I(EOR, mIMM, 2), _I(LSR, mIMP, 2), _I(ALR, mIMM, 2),
	_I(JMP, mABS, 3), _I(EOR, mABS, 4), _I(LSR, mABS, 6), _I(SRE, mABS, 6),

/*5*/
	_H(BVC, mREL, 2), _H(EOR, mINY, 5), _I(KIL, mIMP, 0), _I(SRE, mINY, 8),
	_I(NOP, mZPX, 4), _I(EOR, mZPX, 4), _I(LSR, mZPX, 6), _I(SRE, mZPX, 6),
	_I(CLI, mIMP, 2), _H(EOR, mABY, 4), _I(NOP, mIMP, 2), _I(SRE, mABY, 7),
	_H(NOP, mABX, 4), _H(EOR, mABX, 4), _I(LSR, mABX, 7), _I(SRE, mABX, 7),

/*6*/
	_I(RTS, mIMP, 6), _I(ADC, mINX, 6), _I(KIL, mIMP, 0), _I(RRA, mINX, 8),
	_I(NOP, mZPG, 3), _I(ADC, mZPG, 3), _I(ROR, mZPG, 5), _I(RRA, mZPG, 5),
	_I(PLA, mIMP, 4), _I(ADC, mIMM, 2), _I(ROR, mIMP, 2), _I(ARR, mIMM, 2),
	_I(JMP, mABI, 5), _I(ADC, mABS, 4), _I(ROR, mABS, 6), _I(RRA, mABS, 6),

/*7*/
	_H(BVS, mREL, 2), _H(ADC, mINY, 5), _I(KIL, mIMP, 0), _I(RRA, mINY, 8),
	_I(NOP, mZPG, 4), _I(ADC, mZPX, 4), _I(ROR, mZPX, 6), _I(RRA, mZPX, 6),
	_I(SEI, mIMP, 2), _H(ADC, mABY, 4), _I(NOP, mIMP, 2), _I(RRA, mABY, 7),
	_H(NOP, mABX, 4), _H(ADC, mABX, 4), _I(ROR, mABX, 7), _I(RRA, mABX, 7),

/*8*/
	_I(NOP, mIMM, 2), _I(STA, mINX, 6), _I(NOP, mIMM, 2), _I(SAX, mINX, 6),
	_I(STY, mZPG, 3), _I(STA, mZPG, 3), _I(STX, mZPG, 3), _I(SAX, mZPG, 3),
	_I(DEY, mIMP, 2), _I(NOP, mIMM, 2), _I(TXA, mIMP, 2), _I(XAA, mIMP, 2),
	_I(STY, mABS, 4), _I(STA, mABS, 4), _I(STX, mABS, 4), _I(SAX, mABS, 4),

/*9*/
	_H(BCC, mREL, 2), _I(STA, mINY, 6), _I(KIL, mIMP, 0), _I(AHX, mINY, 6),
	_I(STY, mZPX, 4), _I(STA, mZPX, 4), _I(STX, mZPY, 4), _I(SAX, mZPY, 4),
	_I(TYA, mIMP, 2), _I(STA, mABY, 5), _I(TXS, mIMP, 2), _I(TAS, mABY, 5),
	_I(SHY, mABX, 5), _I(STA, mABX, 5), _I(SHX, mABY, 5), _I(AHX, mABY, 5),

/*A*/
	_I(LDY, mIMM, 2), _I(LDA, mINX, 6), _I(LDX, mIMM, 2), _I(LAX, mINX, 6),
	_I(LDY, mZPG, 3), _I(LDA, mZPG, 3), _I(LDX, mZPG, 3), _I(LAX, mZPG, 3),
	_I(TAY, mIMP, 2), _I(LDA, mIMM, 2), _I(TAX, mIMP, 2), _I(LAX, mIMM, 2),
	_I(LDY, mABS, 4), _I(LDA, mABS, 4), _I(LDX, mABS, 4), _I(LAX, mABS, 4),

/*B*/
	_H(BCS, mREL, 2), _H(LDA, mINY, 5), _I(KIL, mIMP, 0), _H(LAX, mINY, 5),
	_I(LDY, mZPX, 4), _I(LDA, mZPX, 4), _I(LDX, mZPY, 4), _I(LAX, mZPY, 4),
	_I(CLV, mIMP, 2), _H(LDA, mABY, 4), _I(TSX, mIMP, 2), _H(LAS, mABY, 4),
	_H(LDY, mABX, 4), _H(LDA, mABX, 4), _H(LDX, mABY, 4), _H(LAX, mABY, 4),

/*C*/
	_I(CPY, mIMM, 2), _I(CMP, mINX, 6), _I(NOP, mIMM, 2), _I(DCP, mINX, 8),
	_I(CPY, mZPG, 3), _I(CMP, mZPG, 3), _I(DEC, mZPG, 5), _I(DCP, mZPG, 5),
	_I(INY, mIMP, 2), _I(CMP, mIMM, 2), _I(DEX, mIMP, 2), _I(AXS, mIMM, 2),
	_I(CPY, mABS, 4), _I(CMP, mABS, 4), _I(DEC, mABS, 6), _I(DCP, mABS, 6),

/*D*/
	_H(BNE, mREL, 2), _H(CMP, mINY, 5), _I(KIL, mIMP, 0), _I(DCP, mINY, 8),
	_I(NOP, mZPX, 4), _I(CMP, mZPX, 4), _I(DEC, mZPX, 6), _I(DCP, mZPX, 6),
	_I(CLD, mIMP, 2), _H(CMP, mABY, 4), _I(NOP, mIMP, 2), _I(DCP, mABY, 7),
	_H(NOP, mABX, 4), _H(CMP, mABX, 4), _I(DEC, mABX, 7), _I(DCP, mABX, 7),

/*E*/
	_I(CPX, mIMM, 2), _I(SBC, mINX, 6), _I(NOP, mIMM, 2), _I(ISC, mINX, 8),
	_I(CPX, mZPG, 3), _I(SBC, mZPG, 3), _I(INC, mZPG, 5), _I(ISC, mZPG, 5),
	_I(INX, mIMP, 2), _I(SBC, mIMM, 2), _I(NOP, mIMP, 2), _I(SXC, mIMM, 2),
	_I(CPX, mABS, 4), _I(SBC, mABS, 4), _I(INC, mABS, 6), _I(ISC, mABS, 6),

/*F*/
	_H(BEQ, mREL, 2), _H(SBC, mINY, 5), _I(KIL, mIMP, 0), _I(ISC, mINY, 8),
	_I(NOP, mZPX, 4), _I(SBC, mZPX, 4), _I(INC, mZPX, 6), _I(ISC, mZPX, 6),
	_I(SED, mIMP, 2), _H(SBC, mABY, 4), _I(NOP, mIMP, 2), _I(ISC, mABY, 7),
	_H(NOP, mABX, 4), _H(SBC, mABX, 4), _I(INC, mABX, 7), _I(ISC, mABX, 7),
};

/**
 * Fetches operand from memory
 * @param [in] Addressing mode
 * @return 1 if page boundary is crossed, 0 otherwise.
 * @note PC should be at the next byte from opcode
 */
static int fetch_operand(enum _addr_modes mode)
{
	uint16_t addr, adl, adh, ial, iah, bal, bah, offset;
	int cross = 0;

	switch (mode) {
	case mIMP:
		/* Nothing to do */
		break;

	case mIMM:
		fetched_addr = CPU.PC++;
		break;

	case mABS:
		adl = sbus_read(CPU.PC++);
		adh = (sbus_read(CPU.PC++) << 8);
		fetched_addr = (adh & 0xff00) | (adl & 0x00ff);
		break;

	case mABI:
		ial  = sbus_read(CPU.PC++);
		iah  = sbus_read(CPU.PC++);
		addr = ((iah << 8) & 0xff00) | ial;
		bal  = sbus_read(addr);

		/* Simulate CPU page boundary hardware bug */
		if (ial == 0xff)
			bah = (sbus_read(addr & 0xff00) << 8);
		else
			bah = (sbus_read(addr + 1) << 8);
		addr = (bah & 0xff00) | (bal & 0xff);

		/* Fetches the data */
		fetched_addr = addr;
		break;

	case mZPG:
		adl = sbus_read(CPU.PC++);
		fetched_addr = (adl & 0xff);
		break;

	case mREL:
		offset = sbus_read(CPU.PC++);
		if ((offset & 0x80) != 0)
			offset |= 0xff00;
		fetched_addr = offset;
		fetched_data = 0;
		break;

	case mZPX:
		bal  = sbus_read(CPU.PC++);
		/* Page crossing is not allowed in this addr. mode */
		addr = (bal + CPU.X) & 0xff;
		fetched_addr = addr;
		break;

	case mZPY:
		bal  = sbus_read(CPU.PC++);
		/* Page crossing is not allowed in this addr. mode */
		addr = (bal + CPU.Y) & 0xff;
		fetched_addr = addr;
		break;

	case mABX:
		bal   = sbus_read(CPU.PC++);
		bah   = (sbus_read(CPU.PC++) << 8);
		addr  = (bah & 0xff00) | (bal & 0xff);
		addr += CPU.X;

		/* Check for page crossing */
		if ((addr & 0xff00) != bah)
			cross = 1;

		/* Fetches the data */
		fetched_addr = addr;
		break;

	case mABY:
		bal   = sbus_read(CPU.PC++);
		bah   = (sbus_read(CPU.PC++) << 8);
		addr  = (bah & 0xff00) | (bal & 0xff);
		addr += CPU.Y;

		/* Check for page crossing */
		if ((addr & 0xff00) != bah)
			cross = 1;

		/* Fetches the data */
		fetched_addr = addr;
		break;

	case mINX:
		bal  = sbus_read(CPU.PC++);
		adl  = sbus_read((bal + CPU.X) & 0xff);
		adh  = (sbus_read((bal + CPU.X + 1) & 0xff) << 8);
		addr = (adh & 0xff00) | (adl & 0xff);
		fetched_addr = addr;
		break;

	case mINY:
		ial   = sbus_read(CPU.PC++);
		bal   = sbus_read(ial);
		bah   = (sbus_read((ial + 1) & 0xff) << 8);
		addr  = (bah & 0xff00) | (bal & 0xff);
		addr += CPU.Y;

		/* Check for page crossing */
		if ((addr & 0xff00) != bah)
			cross = 1;

		fetched_addr = addr;
		break;
	}

	return cross;
}

/**
 * Execute NMI
 */
static void do_nmi(void)
{
	uint8_t pch, pcl, adh, adl;

	/* NMI was triggered, set trigger back to zero */
	CPU.nmi_trigger = 0;

	/* Serve interrupt */
	pch = ((CPU.PC & 0xff00) >> 8) & 0xff;
	pcl = (uint8_t)(CPU.PC & 0x00ff);
	S_PUSH(pch);
	S_PUSH(pcl);

	S_PUSH(CPU.P.reg & ~BRK_FLAG_MASK); // clear B flag

	CPU.P.flags.I = 1;

	/* Fetch PC from vector */
	adl = sbus_read(0xfffa);
	adh = sbus_read(0xfffb);
	CPU.PC = (((uint16_t)adh << 8) & 0xff00) | (adl & 0xff);

	/* Cycles to serve interrupt */
	CPU.clock_rcycles = 8;
}

/**
 * Execute IRQ
 */
static void do_irq(void)
{
	uint8_t pch, pcl, adh, adl;

	/* Serve interrupt */
	pch = ((CPU.PC & 0xff00) >> 8) & 0xff;
	pcl = (uint8_t)(CPU.PC & 0x00ff);
	S_PUSH(pch);
	S_PUSH(pcl);

	S_PUSH(CPU.P.reg & ~BRK_FLAG_MASK); // clear B flag

	CPU.P.flags.I = 1;

	/* Fetch PC from vector */
	adl  = sbus_read(0xfffe);
	adh  = sbus_read(0xffff);
	CPU.PC = (((uint16_t)adh << 8) & 0xff00) | (adl & 0xff);

	/* Cycles to serve interrupt */
	CPU.clock_rcycles = 7;
}


/**
 * Initialize CPU internal structures
 * @return int 0 on success, -1 otherwise.
 */
int cpu_init(void)
{
	cpu_debug_cb = NULL;
	s_init(&cpu_lock);
	s_init(&nmi_lock);
	s_init(&irq_lock);
	return 0;
}

/**
 * Finalize CPU internal structures
 * @return int 0 on success, -1 otherwise.
 */
int cpu_destroy(void)
{
	s_destroy(&cpu_lock);
	s_destroy(&nmi_lock);
	s_destroy(&irq_lock);
	return 0;
}

/**
 * Suspend CPU
 * @return int 0 on success, -1 otherwise.
 */
int cpu_suspend(void)
{
	s_lock(&cpu_lock);
	CPU.state = CPU_REQ_SUSPEND;
	s_unlock(&cpu_lock);
	return 0;
}

/**
 * Wake up CPU
 * @return int 0 on success, -1 otherwise.
 */
int cpu_wakeup(void)
{
	s_lock(&cpu_lock);
	CPU.state = CPU_RUNNING;
	s_unlock(&cpu_lock);
	return 0;
}

/**
 * Return CPU's current state
 * @return enum _CPU_STATE
 */
enum _CPU_STATE cpu_get_state()
{
	return CPU.state;
}

/**
 * Return the total number of clock cycles of the CPU
 * @return unsigned long Clock cycles
 */
unsigned long cpu_get_cycles()
{
	return CPU.clock_cycles;
}

/**
 * Reset CPU
 */
void cpu_reset(void)
{
	uint8_t adh, adl;

	s_lock(&cpu_lock);

	CPU.A = 0;
	CPU.X = 0;
	CPU.Y = 0;
	CPU.S = 0xfd;
	CPU.P.reg = 0x34;

	/* Fetch initial vectors */
	adl = sbus_read(0xfffc);
	adh = sbus_read(0xfffd);
	CPU.PC = (((uint16_t)adh << 8) & 0xff00) | (adl & 0xff);

	/* Reset interrupt lines */
	CPU.irq_pin = 1;
	CPU.nmi_pin = 1;
	CPU.nmi_trigger = 0;

	/* Reset takes 8 clock cycles */
	CPU.clock_rcycles = 8;
	CPU.clock_cycles = 0;

	CPU.state = CPU_RUNNING;

	s_unlock(&cpu_lock);
}

/**
 * Trigger maskable interrupt
 */
void cpu_trigger_irq(void)
{
	s_lock(&irq_lock);
	if (CPU.state == CPU_RUNNING)
		CPU.irq_pin = 0;
	s_unlock(&irq_lock);
}

/**
 * Clear maskable interrupt
 */
void cpu_clear_irq(void)
{
	s_lock(&irq_lock);
	if (CPU.state == CPU_RUNNING)
		CPU.irq_pin = 1;
	s_unlock(&irq_lock);
}

/**
 * Trigger non-maskable interrupt
 */
void cpu_trigger_nmi(void)
{
	s_lock(&nmi_lock);
	if (CPU.state == CPU_RUNNING) {
		if (CPU.nmi_pin == 1) {
			/* Edge detected! */
			CPU.nmi_trigger = 1;
			CPU.nmi_pin = 0;
		}
	}
	s_unlock(&nmi_lock);
}

/**
 * Release non-maskable interrupt pin
 */
void cpu_clear_nmi(void)
{
	s_lock(&nmi_lock);
	if (CPU.state == CPU_RUNNING) {
		CPU.nmi_trigger = 0;
		CPU.nmi_pin = 1;
	}
	s_unlock(&nmi_lock);
}

/**
 * Register a kill callback
 * @param [in] kill_cb CPU's kill callback function
 * @return 0 on success, -1 if there is a callback already registered
 * @note WARNING! This function *it is not* thread safe, if you are going
 * to run the CPU emulation code on a dedicated thread and call this
 * function from other, make sure to add a lock mechanism (e.g.,
 * semaphore).
 */
int cpu_register_kill_cb(void (*kill_cb)(cpu650x_state_t))
{
	if (cpu_kill_cb != NULL)
		return -1;

	cpu_kill_cb = kill_cb;
	return 0;
}

/**
 * Unregister kill function callback
 * @return 0 on success, -1 if there is no callback registered
 * @note WARNING! This function *it is not* thread safe, if you are going
 * to run the CPU emulation code on a dedicated thread and call this
 * function from other, make sure to add a lock mechanism (e.g.,
 * semaphore).
 */
int cpu_unregister_kill_cb(void)
{
	if (cpu_kill_cb == NULL)
		return -1;

	cpu_kill_cb = NULL;
	return 0;
}

/**
 * Register a debug callback
 * @param [in] debug_cb Debug callback function
 * @return 0 on success, -1 if there is a callback already registered
 * @note WARNING! This function *it is not* thread safe, if you are going
 * to run the CPU emulation code on a dedicated thread and call this
 * function from other, make sure to add a lock mechanism (e.g.,
 * semaphore).
 */
int cpu_register_debug_cb(void (*debug_cb)(cpu650x_state_t))
{
	if (cpu_debug_cb != NULL)
		return -1;

	cpu_debug_cb = debug_cb;
	return 0;
}

/**
 * Unregister debug function callback
 * @return 0 on success, -1 if there is no callback registered
 * @note WARNING! This function *it is not* thread safe, if you are going
 * to run the CPU emulation code on a dedicated thread and call this
 * function from other, make sure to add a lock mechanism (e.g.,
 * semaphore).
 */
int cpu_unregister_debug_cb(void)
{
	if (cpu_debug_cb == NULL)
		return -1;

	cpu_debug_cb = NULL;
	return 0;
}

/**
 * Performs one clock tick in the CPU
 */
void cpu_clock(void)
{
	int cross;

	s_lock(&cpu_lock);

	if (CPU.state == CPU_SUSPENDED) {
		s_unlock(&cpu_lock);
		return;
	}

	if (CPU.clock_rcycles <= 0) {
		/* If the CPU was requested to suspend, now it's the time to go
		 * offline
		 */
		if (CPU.state == CPU_REQ_SUSPEND) {
			CPU.state = CPU_SUSPENDED;
			s_unlock(&cpu_lock);
			return;
		}

		/* If some interrupt was triggered, it should wait for current
		 * instruction to finish in order to jump to server the interrupt.
		 * NMI has priority over masked interrupt.
		 */
		if (CPU.nmi_trigger == 1) {
			do_nmi();
		} else if (CPU.irq_pin == 0 && !CPU.P.flags.I) {
			do_irq();
		} else {
			/* Each instruction takes different amount of clock cycles to be
			 * executed. However, in this emulation the instruction will be
			 * executed at once and will do nothing on the remaining cycles.
			 */
			opcode   = sbus_read(CPU.PC++);
			addrmode = op_codes[opcode].mode;

			/* Fetch operands and decode instruction */
			cross = fetch_operand(op_codes[opcode].mode);
			CPU.clock_rcycles = op_codes[opcode].clock_cycles;

			/* Check if need to add a cycle */
			if (cross && op_codes[opcode].add_cycles == 1)
				CPU.clock_rcycles++;

			/* Execute instruction */
			op_codes[opcode].instruction();
			if (cpu_debug_cb != NULL) {
				state_info.opcode      = opcode;
				state_info.opcode_name = op_codes[opcode].name;
				state_info.address     = fetched_addr;
				state_info.data        = fetched_data;
				state_info.CPU         = CPU;
				cpu_debug_cb(state_info);
			}
#ifdef CPU_PRINT_INSTRUCTION
			printf("%s | A:%d X:%d Y: %d P: %d | DATA: 0x%x | ADDR: 0x%x\n",
			 op_codes[opcode].name, CPU.A, CPU.X, CPU.Y, CPU.P.reg,
			 fetched_data, fetched_addr);
#endif
		}
	}

	CPU.clock_cycles++;
	if (CPU.clock_rcycles >= 0)
		CPU.clock_rcycles--;

	s_unlock(&cpu_lock);
}

#ifdef CPU_TEST_CODE
#include <unistd.h>

static inline void debug_pflags(struct _cpu650x *cpu)
{
	printf("N:%d|V:%d|res:%d|B:%d|D:%d|I:%d|Z:%d|C:%d\n",
			cpu->P.flags.N,   cpu->P.flags.V,
			cpu->P.flags.res, cpu->P.flags.B,
			cpu->P.flags.D,   cpu->P.flags.I,
			cpu->P.flags.Z,   cpu->P.flags.C);
}

int main(int argc, const char *argv[])
{
	sbus_init();
	cpu_init();

	/*
	 * LDA #4
	 * LDX #2
	 * STA 0x0f00
	 * INC 0x0f00
	 * LDA 0x0f00,X
	 * LDA #6
	 * STA 0x50
	 * LDA #254
	 * ADC 0x50
	 * TAX
	 * LDA #211
	 * STA 0x51
	 * LDA #13
	 * ADC 0x51
	 * TAX
	 * SEC
	 * LDA #6
	 * STA 0x52
	 * LDA #254
	 * ADC 0x52
	 * loop:
	 * jmp loop
	 */
	char testcode[] = {
		0xA9, 0x04, 0xA2, 0x02, 0x8D, 0x00, 0x0F, 0xEE,
		0x00, 0x0F, 0xBD, 0x00, 0x0F, 0xA9, 0x06, 0x85,
		0x50, 0xA9, 0xFE, 0x65, 0x50, 0xAA, 0xA9, 0xD3,
		0x85, 0x51, 0xA9, 0x0D, 0x65, 0x51, 0xAA, 0x38,
		0xA9, 0x06, 0x85, 0x52, 0xA9, 0xFE, 0x65, 0x52,
		0x4C, 0x28, 0x00
	};

	for (int i = 0; i < sizeof(testcode)/sizeof(char); i++)
		sbus_write(i, testcode[i]);

	sbus_write(0xfffc, 0);
	sbus_write(0xfffd, 0);

	cpu_reset();
	while (1) {
		cpu_clock();
		usleep(30000);
		if (CPU.clock_rcycles == 0) {
			printf("%s | A:0x%.2x\tX:0x%.2x\t0x0f00:\t0x%.2x ",
			 op_codes[opcode].name, CPU.A, CPU.X, sbus_read(0xf00));
			debug_pflags(&CPU);
		}
	}

	cpu_destroy();
	sbus_destroy();
	return 0;
}
#endif
