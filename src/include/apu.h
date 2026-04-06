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
 * @file apu.h
 * @see apu.c
 */
#ifndef __APU_H__
#define __APU_H__

#include "ringbuffer.h"
#include <stdint.h>

/* APU Registers */

#define REG_PWM0 0x4000
#define REG_PWM0_SWEEP 0x4001
#define REG_PWM0_RAWL 0x4002
#define REG_PWM0_RAWH 0x4003
#define REG_PWM1 0x4004
#define REG_PWM1_SWEEP 0x4005
#define REG_PWM1_RAWL 0x4006
#define REG_PWM1_RAWH 0x4007
#define REG_TRI_CTR 0x4008
#define REG_TRI_TMRL 0x400a
#define REG_TRI_TMRH 0x400b
#define REG_NOISE_VOL 0x400c
#define REG_NOISE_TONE 0x400e
#define REG_NOISE_LC 0x400f
#define REG_DMC 0x4010
#define REG_DMC_DLOAD 0x4011
#define REG_DMC_SADDR 0x4012
#define REG_DMC_SLEN 0x4013
#define REG_STATUS 0x4015
#define REG_FRM_SEQ 0x4017

/** APU Type */
enum APU_TYPE { APU_NTSC = 0, APU_PAL };

/** First order IIR filter */
struct _apu_filter {
	/** Alpha value */
	float alpha;
	/** Previous input */
	float prev_input;
	/** Previous output */
	float prev_output;
};

/** Frame Sequencer */
struct _apu_frm_seq {
	/** Step mode */
	uint8_t mode;
	/** IRQ inhibit flag */
	uint8_t irq_inhibit;
	/** IRQ internal flag */
	uint8_t irq_flag;
	/** APU cycles */
	uint32_t cycles;
};

/** Timer counter */
struct _apu_timer {
	/** Reload value */
	uint16_t reload;
	/** Counter */
	uint16_t counter;
};

/** Length counter */
struct _lc_counter {
	/** Counter */
	uint16_t counter;
	/** Halt flag */
	uint8_t halt;
	/** Status: enable / disabled */
	uint8_t enable;
};

/** Linear counter */
struct _lin_counter {
	/** Counter */
	uint16_t counter;
	/** Reload value */
	uint16_t reload_value;
	/** Control flag */
	uint8_t ctrl;
	/** Reload flag */
	uint8_t reload;
};

/** PWM sequencer */
struct _pwm_sequencer {
	/** Duty cycle */
	uint8_t duty;
	/** Current position in LUT */
	uint8_t lut_pos;
	/** Current output */
	uint8_t output;
};

/** Triangle sequencer */
struct _tri_sequencer {
	/** Current position in LUT */
	uint8_t lut_pos;
	/** Current output */
	uint8_t output;
};

/** Envelope Generator */
struct _envelope {
	/** Start flag */
	uint8_t start;
	/** Loop flag */
	uint8_t loop;
	/** Constant volume */
	uint8_t const_vol;
	/** Divider */
	struct _apu_timer divider;
	/** Decay level counter */
	uint16_t decay;
	/** Volume level */
	uint8_t volume;
	/** Output */
	uint16_t output;
};

/** APU Status register */
struct _apu_status {
	union _reg_status {
		struct _status {
			/** PWM channel 0 */
			uint8_t pwm_0 : 1;
			/** PWM channel 1 */
			uint8_t pwm_1 : 1;
			/** Triangle */
			uint8_t triangle : 1;
			/** Noise */
			uint8_t noise : 1;
			/** DMC */
			uint8_t dmc : 1;
			/** Reserved */
			uint8_t reserved : 1;
			/** Frame interrupt */
			uint8_t frm_int : 1;
			/** DMC Interrupt */
			uint8_t dmc_int : 1;
		} bits;
		uint8_t raw;
	} reg;
};

/** APU PWM wave register */
struct _apu_pwm {
	union _reg_pwm {
		struct _pwm {
			/** Volume / Envelope divider */
			uint8_t vol_env : 4;
			/** Constant volume / Envelope flag */
			uint8_t cv_env : 1;
			/** Length counter halt / Envelope loop */
			uint8_t lc_halt : 1;
			/** Duty cycle */
			uint8_t duty : 2;
		} bits;
		uint8_t raw;
	} reg;
};

/** APU sweep register */
struct _apu_sweep {
	union _reg_sweep {
		struct _sweep_reg {
			uint8_t shift : 3;
			uint8_t neg : 1;
			uint8_t period : 3;
			uint8_t enabled : 1;
		} bits;
		uint8_t raw;
	} reg;
};

/** APU Triangle wave register */
struct _apu_triangle {
	union _reg_triangle {
		struct _tri {
			uint8_t linear_cnt : 7;
			uint8_t lc_halt : 1;
		} bits;
		uint8_t raw;
	} reg;
};

/** APU Noise wave register */
struct _apu_noise {
	union _reg_noise {
		struct _noise {
			uint8_t period : 4;
			uint8_t reserved : 3;
			uint8_t tone_mode : 1;
		} bits;
		uint8_t raw;
	} reg;
};

/** DMC register */
struct _apu_dmc {
	union _reg_dmc {
		struct _dmc {
			uint8_t rate : 4;
			uint8_t reserved : 2;
			uint8_t loop : 1;
			uint8_t irq : 1;
		} bits;
		uint8_t raw;
	} reg;
};

/** APU Noise envelope and length counter register */
struct _apu_noise_lc_env {
	union _reg_noise_env {
		struct _noise_env {
			uint8_t vol_env : 4;
			/** Constant volume / Envelope flag */
			uint8_t cv_env : 1;
			/** Length counter halt / Envelope loop */
			uint8_t lc_halt : 1;
			/** Reserved */
			uint8_t reserved : 2;
		} bits;
		uint8_t raw;
	} reg;
};

/** Sweep unit */
struct _sweep {
	/** Sweep register */
	struct _apu_sweep sweep;
	/** Divider counter */
	struct _apu_timer divider;
	/** Mute channel */
	uint8_t mute;
	/** Reload flag */
	uint8_t reload;
};

/** PWM Channel */
struct _apu_pwm_channel {
	/** PWM register */
	struct _apu_pwm pwm;
	/** Timer raw value */
	uint16_t raw;
	/** Envelope generator */
	struct _envelope env;
	/** Sweep unit */
	struct _sweep sw;
	/** Timer */
	struct _apu_timer tm;
	/** Sequencer */
	struct _pwm_sequencer seq;
	/** Length counter */
	struct _lc_counter lc;
};

/** Triangle wave channel */
struct _apu_triangle_channel {
	/** APU triangle wave register */
	struct _apu_triangle r;
	/** Triangle sequencer */
	struct _tri_sequencer seq;
	/** Timer counter */
	struct _apu_timer tm;
	/** Timer raw value */
	int32_t raw;
	/** Length counter */
	struct _lc_counter lc;
	/** Linear counter */
	struct _lin_counter lin;
};

/** Noise channel */
struct _apu_noise_channel {
	/** APU noise register */
	struct _apu_noise r;
	/** APU noise envelop/length counter register */
	struct _apu_noise_lc_env renv;
	/** Envelope generator */
	struct _envelope env;
	/** Timer counter */
	struct _apu_timer tm;
	/** Length counter */
	struct _lc_counter lc;
	/** Shift register */
	uint16_t shift;
};

/** DMC Buffer */
struct _apu_dmc_buffer {
	/** Single sample */
	uint8_t sample;
	/** Is the buffer empty? */
	uint8_t is_empty;
};

/** DMC DMA Reader */
struct _apu_dmc_dma {
	/** Sample address */
	uint16_t sample_addr;
	/** Current address */
	uint16_t current_addr;
	/** Sample length */
	uint16_t sample_len;
	/** Counter (remaining bytes) */
	uint16_t bytes_remaining;
};

/** DMA Output module */
struct _apu_dmc_output {
	/** Shift register */
	uint8_t reg;
	/** Bits remaining */
	uint8_t bits_remaining;
	/** Silence flag */
	uint8_t silence;
	/** Output */
	uint8_t output;
};

/** DMC channel */
struct _apu_dmc_channel {
	/** DMC register */
	struct _apu_dmc dmc;
	/** Timer period */
	uint16_t period;
	/** DMA Reader */
	struct _apu_dmc_dma dma;
	/** Sample buffer */
	struct _apu_dmc_buffer buffer;
	/** Output */
	struct _apu_dmc_output out;
	/** CPU cycles */
	uint32_t cycles;
	/** CPU Suspended cycles */
	int cpu_sus;
	/** Enable status */
	uint8_t enable;
	/** IRQ flag */
	uint8_t irq_flag;
};

/** APU */
struct _apu_t {
	/** APU Type */
	enum APU_TYPE type;
	/** Status register */
	struct _apu_status status;
	/** Frame sequencer */
	struct _apu_frm_seq frm_seq;
	/** Frame sequencer reset counter */
	uint8_t frm_seq_reset_cnt;
	/** PWM Channels: 0 and 1 */
	struct _apu_pwm_channel PWM[2];
	/** Triangle wave channel */
	struct _apu_triangle_channel triangle;
	/** Noise channel */
	struct _apu_noise_channel noise;
	/** DMC channel */
	struct _apu_dmc_channel dmc;
	/** Clock counter */
	uint64_t clock;
	/** Ratio between APU and Audio Sampling frequencies */
	uint64_t freq_ratio;
	/** Counter of samples before push to audio buffer */
	uint64_t sample_counter;
	/** Audio sample (output) */
	float sample;
	/** Buffer with the audio samples */
	ringbuffer_t buffer;
	/** Flag to stop read/writing from/to buffer */
	uint8_t stop;
	/** High-pass filter at 90Hz */
	struct _apu_filter hp_90;
	/** High-pass filter at 440Hz */
	struct _apu_filter hp_440;
	/** Low-pass filter at 14KHz */
	struct _apu_filter lp_14k;
};

int apu_init(void);
int apu_destroy(void);
void apu_reset(void);
uint8_t apu_reg_read(uint16_t reg);
void apu_reg_write(uint16_t reg, uint8_t value);
void apu_clock(void);
void apu_get_samples(void *data, uint8_t *stream, int len);

#endif /* __APU_H__ */
