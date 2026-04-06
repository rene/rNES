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
 * @file controller.c
 *
 * Handles the controller ports.
 */
#include "controller.h"
#include "hal/hal.h"

/** Gamepad registers */
controllers_t ctrlregs;

/**
 * Reset gamepad registers
 */
void controller_reset(void)
{
	ctrlregs.joypad[0] = 0;
	ctrlregs.joypad[1] = 0;
	ctrlregs.strobe[0] = 0;
	ctrlregs.strobe[1] = 0;
}

/**
 * Read one byte (8 bits) from the specified register
 * @param [in] reg Register address
 * @return Register value
 */
uint8_t controller_reg_read(uint16_t reg)
{
	int i;
	uint8_t val = 0;

	if (reg == REG_JOYPAD1)
		i = 0;
	else
		i = 1;

	if (ctrlregs.joypad[i] & 0x80)
		val = 1;

	ctrlregs.joypad[i] <<= 1;

	return val;
}

/**
 * Write one byte (8 bits) to the specified register
 * @param [in] reg Register address
 * @param [in] value Value
 */
void controller_reg_write(uint16_t reg, uint8_t value)
{
	int i;
	uint8_t state = value;

	if (reg == REG_JOYPAD1)
		i = 0;
	else
		i = 1;

	ctrlregs.strobe[i] = state;

	if (state)
		ctrlregs.joypad[i] = gui_read_joypad(i);
}
