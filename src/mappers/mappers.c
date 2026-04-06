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
 * @file mappers.c
 *
 * Implements the HAL layer for mappers.
 */
#include <errno.h>
#include <mappers/mapper.h>

/** List of implemented mappers */
extern mapper_t m0_NROM;
extern mapper_t m1_SxROM;
extern mapper_t m2_UxROM;
extern mapper_t m3_CNROM;
extern mapper_t m4_MMC3;
extern mapper_t m7_AxROM;

/** Vector of all mappers */
mapper_t *mappers[NUM_MAPPERS] = {
	&m0_NROM,  /* 0 */
	&m1_SxROM, /* 1 */
	&m2_UxROM, /* 2 */
	&m3_CNROM, /* 3 */
	&m4_MMC3,  /* 4 */
	0,		   /* 5 */
	0,		   /* 6 */
	&m7_AxROM, /* 7 */
};

/**
 * Generic mapper initialization function
 * @param [in,out] m Mapper
 * @param [in,out] c Cartridge
 * @return int 0 on success, error code otherwise
 */
int mapper_init(struct _mapper_t *m, cartridge_t *c)
{
	if (m)
		m->cartridge = c;
	return 0;
}

/**
 * Generic mapper reset function
 * @param [in,out] m Mapper
 * @return int 0 on success, error code otherwise
 */
int mapper_reset(struct _mapper_t *m)
{
	cartridge_t *c;

	if (!m)
		return -EINVAL;

	c = m->cartridge;
	m->finalize(m);
	return m->init(m, c);
}

/**
 * Generic mapper finalization function
 * @param [in,out] m Mapper
 * @return int 0 on success, error code otherwise
 */
int mapper_finalize(struct _mapper_t *m) { return 0; }
