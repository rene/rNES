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
 * @file controller.h
 * @see controller.c
 */
#ifndef __CONTROLLER_H__
#define __CONTROLLER_H__

#include <stdint.h>

/** Joypad 1 register */
#define REG_JOYPAD1 0x4016
/** Joypad 2 register */
#define REG_JOYPAD2 0x4017

/**
 * Single controller register
 */
struct _controller_reg {
	union {
		struct _reg {
			uint8_t right : 1;
			uint8_t left : 1;
			uint8_t down : 1;
			uint8_t up : 1;
			uint8_t start : 1;
			uint8_t select : 1;
			uint8_t B : 1;
			uint8_t A : 1;
		} reg;
		uint8_t raw;
	};
};

/**
 * Struct for all controller registers
 */
struct _controller_regs {
	/** Joypad registers */
	uint8_t joypad[2];
	/** Strobe */
	uint8_t strobe[2];
};

/** Controller type */
typedef struct _controller_regs controllers_t;
/** Controller register type */
typedef struct _controller_reg controller_reg_t;

void controller_reset(void);
uint8_t controller_reg_read(uint16_t reg);
void controller_reg_write(uint16_t reg, uint8_t value);

#endif /* __CONTROLLER_H__ */
