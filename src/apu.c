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
 * @file apu.c
 *
 * Implements the APU (Audio Processing Unit).
 *
 * Memory areas:
 * ----------
 * Address range  Size   Description
 * $4000-$4003    $3     First PWM channel
 * $4004-$4007    $3     Second PWM channel
 * $4008-$400B    $3     Triangle wave channel
 * $400C-$400F    $3     Noise channel
 * $4010-$4013    $3     DMC Playback
 * $4015          $1     Status Register
 * $4017          $1     Frame Sequencer register
 */
#include "apu.h"
#include "cpu650x.h"
#include "hal/hal.h"
#include "sbus.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/** APU NTSC CPU clock Frequency (Hz) */
#define APU_NTSC_FREQ 1789773
/** APU PAL CPU clock Frequency (Hz) */
#define APU_PAL_FREQ 1662607

/** Frame counter cycles */
static const uint32_t frm_cycles[][5] = {
	{3728, 7456, 11185, 14914, 18640}, /* NTSC */
	{4165, 8313, 12469, 16626, 20782}, /* PAL */
};

/** PWM sequencer pattern */
static const uint8_t pwm_lut[][8] = {
	{0, 1, 0, 0, 0, 0, 0, 0}, /* 12.5% */
	{0, 1, 1, 0, 0, 0, 0, 0}, /* 25.0% */
	{0, 1, 1, 1, 1, 0, 0, 0}, /* 50.0% */
	{1, 0, 0, 1, 1, 1, 1, 1}  /* 75.0% */
};

/** Triangle waveform pattern */
static const uint8_t triangle_lut[] = {15, 14, 13, 12, 11, 10, 9,  8,  7,  6, 5,
									   4,  3,  2,  1,  0,  0,  1,  2,  3,  4, 5,
									   6,  7,  8,  9,  10, 11, 12, 13, 14, 15};

/** Noise timer period */
static const int noise_period[][16] = {
	{4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068},
	{4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708, 944, 1890, 3778},
};

/** DMC timer period */
static const int dmc_period[][16] = {
	{428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72,
	 54}, /* NTSC */
	{398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118, 98, 78, 66,
	 50}, /* PAL */
};

/** Length counter Lookup Table */
static const uint16_t lc_lut[] = {10, 254, 20,	2,	40, 4,	80, 6,	160, 8,	 60,
								  10, 14,  12,	26, 14, 12, 16, 24, 18,	 48, 20,
								  96, 22,  192, 24, 72, 26, 16, 28, 32,	 30};

#ifndef APU_LINEAR_DAC_OUTPUT
/** Output Lookup table for PWM channels */
float pwm_out_lut[] = {
	0.000000, 0.011609, 0.022939, 0.034001, 0.044803, 0.055355, 0.065665,
	0.075741, 0.085591, 0.095224, 0.104645, 0.113862, 0.122882, 0.131710,
	0.140353, 0.148816, 0.157105, 0.165226, 0.173183, 0.180981, 0.188626,
	0.196120, 0.203470, 0.210679, 0.217751, 0.224689, 0.231499, 0.238182,
	0.244744, 0.251186, 0.257513, 0.263726};

/** Output lookup table for triangle, noise and DMC channels */
float tnd_out_lut[] = {
	0.000000, 0.006700, 0.013345, 0.019936, 0.026474, 0.032959, 0.039393,
	0.045775, 0.052106, 0.058386, 0.064618, 0.070800, 0.076934, 0.083020,
	0.089058, 0.095050, 0.100996, 0.106896, 0.112751, 0.118561, 0.124327,
	0.130049, 0.135728, 0.141365, 0.146959, 0.152512, 0.158024, 0.163494,
	0.168925, 0.174315, 0.179666, 0.184978, 0.190252, 0.195487, 0.200684,
	0.205845, 0.210968, 0.216054, 0.221105, 0.226120, 0.231099, 0.236043,
	0.240953, 0.245828, 0.250669, 0.255477, 0.260252, 0.264993, 0.269702,
	0.274379, 0.279024, 0.283638, 0.288220, 0.292771, 0.297292, 0.301782,
	0.306242, 0.310673, 0.315074, 0.319446, 0.323789, 0.328104, 0.332390,
	0.336649, 0.340879, 0.345083, 0.349259, 0.353408, 0.357530, 0.361626,
	0.365696, 0.369740, 0.373759, 0.377752, 0.381720, 0.385662, 0.389581,
	0.393474, 0.397344, 0.401189, 0.405011, 0.408809, 0.412584, 0.416335,
	0.420064, 0.423770, 0.427454, 0.431115, 0.434754, 0.438371, 0.441966,
	0.445540, 0.449093, 0.452625, 0.456135, 0.459625, 0.463094, 0.466543,
	0.469972, 0.473380, 0.476769, 0.480138, 0.483488, 0.486818, 0.490129,
	0.493421, 0.496694, 0.499948, 0.503184, 0.506402, 0.509601, 0.512782,
	0.515946, 0.519091, 0.522219, 0.525330, 0.528423, 0.531499, 0.534558,
	0.537601, 0.540626, 0.543635, 0.546627, 0.549603, 0.552563, 0.555507,
	0.558434, 0.561346, 0.564243, 0.567123, 0.569988, 0.572838, 0.575673,
	0.578493, 0.581298, 0.584088, 0.586863, 0.589623, 0.592370, 0.595101,
	0.597819, 0.600522, 0.603212, 0.605887, 0.608549, 0.611197, 0.613831,
	0.616452, 0.619059, 0.621653, 0.624234, 0.626802, 0.629357, 0.631899,
	0.634428, 0.636944, 0.639448, 0.641939, 0.644418, 0.646885, 0.649339,
	0.651781, 0.654212, 0.656630, 0.659036, 0.661431, 0.663813, 0.666185,
	0.668544, 0.670893, 0.673229, 0.675555, 0.677869, 0.680173, 0.682465,
	0.684746, 0.687017, 0.689276, 0.691525, 0.693763, 0.695991, 0.698208,
	0.700415, 0.702611, 0.704797, 0.706973, 0.709139, 0.711294, 0.713440,
	0.715576, 0.717702, 0.719818, 0.721924, 0.724021, 0.726108, 0.728186,
	0.730254, 0.732313, 0.734362, 0.736402, 0.738433, 0.740455, 0.742468};

#define GET_SND_OUT(pwm0, pwm1, tr, noise, dmc)                                \
	(pwm_out_lut[pwm0 + pwm1] + tnd_out_lut[(3 * tr) + (2 * noise) + dmc])
#else

#define GET_SND_OUT(pwm0, pwm1, tr, noise, dmc)                                \
	(((float)0.00752 * (pwm0 + pwm1)) +                                        \
	 ((float)(0.00851 * tr) + (0.00494 * noise) + (0.00335 * dmc)))
#endif

/** System's APU */
static struct _apu_t apu;

/**
 * Set the counter value of a timer
 * @param [out] t Timer
 * @param [in] reload Reload value
 */
static inline void tm_load(struct _apu_timer *t, uint16_t reload)
{
	t->reload = reload;
}

/**
 * Reset a timer (set counter to the reload value)
 * @param [in,out] t Timer
 */
static inline void tm_reset(struct _apu_timer *t) { t->counter = t->reload; }

/**
 * Clocks a timer
 * @param [in,out] t Timer
 * @return uint8_t Output of the timer
 */
static inline uint8_t tm_clock(struct _apu_timer *t)
{
	uint8_t out = 0;

	if (t->counter > 0) {
		t->counter--;
	} else {
		t->counter = t->reload;
		out = 1;
	}

	return out;
}

/**
 * Set the counter value of a length counter
 * @param [out] lc Length counter
 * @param [in] idx Index
 */
static inline void lc_load(struct _lc_counter *lc, uint8_t idx)
{
	if (lc->enable)
		lc->counter = lc_lut[idx];
}

/**
 * Clocks a length counter
 * @param [in,out] lc Length counter
 */
static inline void lc_clock(struct _lc_counter *lc)
{
	if (lc->counter > 0 && !lc->halt)
		lc->counter--;
}

/**
 * Set halt flag of a length counter
 * @param [in,out] lc Length counter
 * @param [in] halt Halt flag (1 = halted, 0 = enabled)
 */
static inline void lc_halt(struct _lc_counter *lc, uint8_t halt)
{
	lc->halt = halt;
}

/**
 * Set length counter to zero
 * @param [in,out] lc Length counter
 */
static inline void lc_reset(struct _lc_counter *lc) { lc->counter = 0; }

/**
 * Enable / disable length counter
 * @param [in,out] lc Length counter
 * @param [in] enable 1 = enabled | 0 = disabled
 */
static inline void lc_set_enable(struct _lc_counter *lc, uint8_t enable)
{
	lc->enable = enable;
	if (lc->enable == 0) {
		lc_reset(lc);
		lc->halt = 1;
	}
}

/**
 * Return the output of a length counter
 * @return uint16_t Length counter output
 */
static inline uint16_t lc_output(struct _lc_counter *lc)
{
	if (lc->enable)
		return lc->counter;
	else
		return 0;
}

/**
 * Clocks the PWM sequencer
 * @param [in,out] seq PWM's sequencer
 */
static inline void pwm_seq_clock(struct _pwm_sequencer *seq)
{
	seq->output = pwm_lut[seq->duty][seq->lut_pos++];
	if (seq->lut_pos >= 8)
		seq->lut_pos = 0;
}

/**
 * Set the duty cycle of a PWM sequencer
 * @param [in,out] seq PWM's sequencer
 * @param [in] duty Duty cycle
 */
static inline void pwm_seq_set_duty(struct _pwm_sequencer *seq, uint8_t duty)
{
	seq->duty = duty;
}

/**
 * Resets the PWM sequencer
 * @param [in,out] seq PWM's sequencer
 */
static inline void pwm_seq_reset(struct _pwm_sequencer *seq)
{
	seq->lut_pos = 0;
}

/**
 * Get PWM sequencer current output
 * @param [in,out] seq PWM's sequencer
 * @return uint8_t Current output
 */
static inline uint8_t pwm_seq_get_output(struct _pwm_sequencer *seq)
{
	return seq->output;
}

/**
 * Clocks the Envelope Generator
 * @param [in] env Envelope Generator
 */
static inline void env_clock(struct _envelope *env)
{
	if (!env->start) {
		/* Start flag is cleared, clock the divider */
		if (tm_clock(&env->divider)) {
			/* Divider reached 0, clocks the decay counter */
			if (env->decay > 0)
				env->decay--;
			else if (env->loop)
				env->decay = 15;
		}
	} else {
		/* Start the envelope sequence: clear start flag and reset the
		 * clock divider
		 */
		env->start = 0;
		env->decay = 15;
		tm_reset(&env->divider);
	}

	if (env->const_vol)
		env->output = env->volume;
	else
		env->output = env->decay;
}

/**
 * Restart the Envelope Generator
 * @param [in] env Envelope Generator
 */
static inline void env_reset(struct _envelope *env) { env->start = 1; }

/**
 * Return the output of an Envelope Generator
 * @param [in] env Envelope Generator
 * @return uint16_t Evelope output
 */
static inline uint16_t env_get_output(struct _envelope *env)
{
	return env->output;
}

/**
 * Clocks the triangle sequencer
 * @param [in,out] seq triangle's sequencer
 */
static inline void tri_seq_clock(struct _tri_sequencer *seq)
{
	seq->output = triangle_lut[seq->lut_pos++];
	if (seq->lut_pos >= 32)
		seq->lut_pos = 0;
}

/**
 * Get triangle sequencer current output
 * @param [in,out] seq triangle's sequencer
 * @return uint8_t Current output
 */
static inline uint8_t tri_seq_get_output(struct _tri_sequencer *seq)
{
	return seq->output;
}

/**
 * Clocks all envelope generators of the APU and the linear counter of
 * triangle channel
 */
static inline void clock_envelopes(void)
{
	/* PWM Channels */
	env_clock(&apu.PWM[0].env);
	env_clock(&apu.PWM[1].env);

	/* Noise channel */
	env_clock(&apu.noise.env);
}

/**
 * Clocks all sweep units of the APU
 */
static void clock_sweeps(void)
{
	int ch, sw, new_raw, shift;
	uint8_t is_muted, div;

	for (ch = 0; ch < 2; ch++) {
		/* Calculate the new target period (RAW timer) */
		shift = apu.PWM[ch].sw.sweep.reg.bits.shift;
		sw = apu.PWM[ch].raw >> shift;

		if (apu.PWM[ch].sw.sweep.reg.bits.neg)
			sw = ~sw + ch;

		new_raw = apu.PWM[ch].raw + sw;

		/* Determine if we need to mute */
		if (apu.PWM[ch].raw < 8 || new_raw > 0x7ff)
			is_muted = 1;
		else
			is_muted = 0;
		apu.PWM[ch].sw.mute = is_muted;

		/* Handle Divider and Period Update */
		div = tm_clock(&apu.PWM[ch].sw.divider);
		if (div && apu.PWM[ch].sw.sweep.reg.bits.enabled && !is_muted &&
			shift > 0) {
			apu.PWM[ch].raw = (uint16_t)new_raw;
			tm_load(&apu.PWM[ch].tm, apu.PWM[ch].raw);
		}

		/* Handle reload */
		if (div || apu.PWM[ch].sw.reload) {
			tm_load(&apu.PWM[ch].sw.divider,
					apu.PWM[ch].sw.sweep.reg.bits.period);
			apu.PWM[ch].sw.reload = 0;
		}
	}
}

/**
 * Clocks all length counters of the APU
 */
static inline void clock_lcs(void)
{
	/* PWM Channels */
	lc_clock(&apu.PWM[0].lc);
	lc_clock(&apu.PWM[1].lc);

	/* Triangle channel */
	lc_clock(&apu.triangle.lc);

	/* Noise channel */
	lc_clock(&apu.noise.lc);
}

/**
 * Frame Sequencer Quarter Frame
 */
static void frm_QuarterFrame(void)
{
	/* Envelopes */
	clock_envelopes();

	/* Triangle Linear counter */
	if (apu.triangle.lin.reload)
		apu.triangle.lin.counter = apu.triangle.lin.reload_value;
	else if (apu.triangle.lin.counter > 0)
		apu.triangle.lin.counter--;

	if (!apu.triangle.lin.ctrl)
		apu.triangle.lin.reload = 0;
}

/**
 * Frame Sequencer Half Frame
 */
static void frm_HalfFrame(void)
{
	/* Length counters + Sweep units */
	clock_lcs();
	clock_sweeps();
}

/**
 * Reset the Frame Sequencer
 */
static void frm_seq_reset(void) { apu.frm_seq.cycles = 0; }

/**
 * Clocks the Frame Sequencer
 */
static void frm_seq_clock(void)
{
	if (apu.frm_seq.mode == 0) {
		/* Mode 0: 4 steps */
		if (apu.frm_seq.cycles == frm_cycles[apu.type][0]) {
			/* Step 0 */
			frm_QuarterFrame();
		} else if (apu.frm_seq.cycles == frm_cycles[apu.type][1]) {
			/* Step 1 */
			frm_QuarterFrame();
			frm_HalfFrame();
		} else if (apu.frm_seq.cycles == frm_cycles[apu.type][2]) {
			/* Step 2 */
			frm_QuarterFrame();
		} else if (apu.frm_seq.cycles >= frm_cycles[apu.type][3] &&
				   apu.frm_seq.cycles <= (frm_cycles[apu.type][3] + 1)) {
			/* Step 3 */
			/* IRQ flag is set for these 3 cycles */
			if (apu.frm_seq.irq_inhibit == 0)
				apu.frm_seq.irq_flag = 1;

			if (apu.frm_seq.cycles == frm_cycles[apu.type][3]) {
				frm_QuarterFrame();
				frm_HalfFrame();
			} else if (apu.frm_seq.cycles == (frm_cycles[apu.type][3] + 1)) {
				apu.frm_seq.cycles = 0;
				return;
			}
		}
	} else {
		/* Mode 1: 5 steps */
		if (apu.frm_seq.cycles == frm_cycles[apu.type][0]) {
			/* Step 0 */
			frm_QuarterFrame();
		} else if (apu.frm_seq.cycles == frm_cycles[apu.type][1]) {
			/* Step 1 */
			frm_QuarterFrame();
			frm_HalfFrame();
		} else if (apu.frm_seq.cycles == frm_cycles[apu.type][2]) {
			/* Step 2 */
			frm_QuarterFrame();
			/* PS: We do nothing in Step 3 */
		} else if (apu.frm_seq.cycles == frm_cycles[apu.type][4]) {
			/* Step 4 */
			apu.frm_seq.cycles = 0;
			return;
		}
	}

	apu.frm_seq.cycles++;
}

/**
 * Clocks the DMC unity
 */
static void dmc_clock(void)
{
	char bit, timer_fired;

	/* Memory reader (should run on every CPU cycle) */
	if (apu.dmc.buffer.is_empty && apu.dmc.dma.bytes_remaining > 0) {
		/* CPU cycles due to suspend */
		apu.dmc.cpu_sus = 4;

		/* Fill the buffer */
		apu.dmc.buffer.sample = sbus_read(apu.dmc.dma.current_addr);
		apu.dmc.buffer.is_empty = 0;

		/* Increment address */
		if (apu.dmc.dma.current_addr == 0xffff)
			apu.dmc.dma.current_addr = 0x8000;
		else
			apu.dmc.dma.current_addr++;

		/* Remaining bytes */
		apu.dmc.dma.bytes_remaining--;

		/* Is the output cycle finished? */
		if (apu.dmc.dma.bytes_remaining == 0) {
			/* Sample ended, should be restarted? */
			if (apu.dmc.dmc.reg.bits.loop) {
				apu.dmc.dma.current_addr = apu.dmc.dma.sample_addr;
				apu.dmc.dma.bytes_remaining = apu.dmc.dma.sample_len;
			} else if (apu.dmc.dmc.reg.bits.irq) {
				apu.dmc.irq_flag = 1;
				cpu_trigger_irq();
			}
		}
	}

	/* Handle DMC unit clock cycle */
	timer_fired = 0;
	if (++apu.dmc.cycles > apu.dmc.period) {
		apu.dmc.cycles = 0;
		timer_fired = 1;
	}

	/* Spend CPU cycles due to DMA operation */
	if (apu.dmc.cpu_sus > 0) {
		apu.dmc.cpu_sus--;
		return;
	} else if (apu.dmc.cpu_sus == 0) {
		apu.dmc.cpu_sus = -1;
	}

	if (timer_fired) {
		/* Process the output cycle */
		if (!apu.dmc.out.silence) {
			bit = apu.dmc.out.reg & 0x1;
			apu.dmc.out.reg >>= 1;

			if (bit == 0) {
				if (apu.dmc.out.output >= 2)
					apu.dmc.out.output -= 2;
			} else {
				if (apu.dmc.out.output <= 125)
					apu.dmc.out.output += 2;
			}
		}

		apu.dmc.out.bits_remaining--;
		if (apu.dmc.out.bits_remaining == 0) {
			/* Output cycle ended */
			apu.dmc.out.bits_remaining = 8;
			if (apu.dmc.buffer.is_empty) {
				apu.dmc.out.silence = 1;
			} else {
				/* Load sample buffer to shift register */
				apu.dmc.out.reg = apu.dmc.buffer.sample;
				apu.dmc.buffer.is_empty = 1;
				apu.dmc.out.silence = 0;
			}
		}
	}
}

/**
 * PWM Channel signal sound level
 * @param [in] ch PWM Channel
 * @return uint16_t Sound level [0, 15]
 * @note PWM Channel schematic:
 *                    Sweep -----> Timer
 *                      |            |
 *                      |            |
 *                      |            v
 *                      |        Sequencer   Length Counter
 *                      |            |             |
 *                      |            |             |
 *                      v            v             v
 *   Envelope ------> Gate0 ----> Gate1 ------> Gate2 --->(to mixer)
 */
static uint16_t pwm_get_output(int ch)
{
	uint8_t seq_out;
	uint16_t env_out;

	/* Length counter */
	if (lc_output(&apu.PWM[ch].lc) == 0)
		return 0;

	/* PWM sequencer */
	seq_out = pwm_seq_get_output(&apu.PWM[ch].seq);
	if (seq_out == 0)
		return 0;

	/* Sweep mute */
	if (apu.PWM[ch].sw.mute)
		return 0;

	/* Envelope */
	env_out = env_get_output(&apu.PWM[ch].env);

	return env_out;
}

/**
 * Triangle Channel signal sound level
 * @return uint16_t Sound level [0, 15]
 * @note triangle channel schematic:
 *       Linear Counter   Length Counter
 *             |                |
 *             v                v
 * Timer ---> Gate ----------> Gate ---> Sequencer ---> (to mixer)
 */
static inline uint16_t tri_get_output(void)
{
	uint16_t out = 0;

	if (apu.triangle.lin.counter > 0 && lc_output(&apu.triangle.lc) > 0)
		out = tri_seq_get_output(&apu.triangle.seq);

	return out;
}

/**
 * Noise Channel signal sound level
 * @return uint16_t Sound level [0, 15]
 * @note noise channel schematic:
 *   Timer --> Shift Register   Length Counter
 *                    |                |
 *                    v                v
 * Envelope -------> Gate ----------> Gate --> (to mixer)
 */
static inline uint16_t noise_get_output(void)
{
	uint16_t env_out;

	/* Shift register */
	if ((apu.noise.shift & 0x1))
		return 0;

	/* Length counter */
	if (lc_output(&apu.noise.lc) == 0)
		return 0;

	/* Envelope */
	env_out = env_get_output(&apu.noise.env);

	return env_out;
}

/**
 * DMC Channel signal level
 * @return uint16_t Sound level [0, 15]
 *
 * @note DMC channel schematic:
 *                           Timer
 *                            |
 *                            v
 * Reader ---> Buffer ---> Shifter ---> Output level ---> (to the mixer)
 */
static inline uint16_t dmc_get_output(void)
{
	if (!apu.dmc.out.silence)
		return apu.dmc.out.output;
	else
		return 0;
}

/***
 * Write PWM Channel register
 * @param [in] ch PWM Channel number
 * @param value Value to be written
 */
static inline void write_pwm_reg(uint8_t ch, uint8_t value)
{
	/* Register */
	apu.PWM[ch].pwm.reg.raw = value;

	/* Length counter */
	lc_halt(&apu.PWM[ch].lc, apu.PWM[ch].pwm.reg.bits.lc_halt);

	/* Envelope */
	apu.PWM[ch].env.volume = apu.PWM[ch].pwm.reg.bits.vol_env;
	tm_load(&apu.PWM[ch].env.divider, apu.PWM[ch].env.volume + 1);
	apu.PWM[ch].env.const_vol = apu.PWM[ch].pwm.reg.bits.cv_env;
	apu.PWM[ch].env.loop = apu.PWM[ch].pwm.reg.bits.lc_halt;

	/* Duty cycle */
	pwm_seq_set_duty(&apu.PWM[ch].seq, apu.PWM[ch].pwm.reg.bits.duty);
}

/***
 * Write PWM Channel length counter register
 * @param [in] ch PWM Channel number
 * @param value Value to be written
 */
static inline void write_pwm_lc_reg(uint8_t ch, uint8_t value)
{
	/* Register */
	apu.PWM[ch].raw &= 0xff;
	apu.PWM[ch].raw |= ((((uint16_t)value & 0x7) << 8) & 0x700);

	/* Load length counter */
	lc_load(&apu.PWM[ch].lc, ((value & 0xf8) >> 3) & 0x1f);

	/* Load the new period (without resetting) */
	tm_load(&apu.PWM[ch].tm, apu.PWM[ch].raw);

	/* Restart the sequencer */
	pwm_seq_reset(&apu.PWM[ch].seq);

	/* Restart envelope */
	env_reset(&apu.PWM[ch].env);
}

/**
 * Reset/Initialize all filters coefficients
 */
static void init_filters(void)
{
	float dt = 1.0f / AUDIO_SAMPLE_RATE;

	/* Pre-calculated values: 1.0 / (2.0 * PI * cutoff_frequency) */
	float rc_hp90 = 0.001768388;
	float rc_hp440 = 0.0003617158;
	float rc_lp14k = 0.00001136821;

	/* High-pass filter at 90Hz */
	apu.hp_90.alpha = rc_hp90 / (rc_hp90 + dt);
	apu.hp_90.prev_input = 0;
	apu.hp_90.prev_output = 0;

	/* High-pass filter at 440Hz */
	apu.hp_440.alpha = rc_hp440 / (rc_hp440 + dt);
	apu.hp_440.prev_input = 0;
	apu.hp_440.prev_output = 0;

	/* Low-pass filter at 14KHz */
	apu.lp_14k.alpha = rc_lp14k / (rc_lp14k + dt);
	apu.lp_14k.prev_input = 0;
	apu.lp_14k.prev_output = 0;
}

/**
 * IIR first order Low-pass filter
 * @param [in,out] Filter f
 * @param input Filter input
 * @return Output
 */
static float low_pass_filter(struct _apu_filter *f, float input)
{
	float output = (1.0f - f->alpha) * input + (f->alpha * f->prev_output);

	f->prev_output = output;
	return output;
}

/**
 * IIR first order High-pass filter
 * @param [in,out] Filter f
 * @param input Filter input
 * @return Output
 */
static float high_pass_filter(struct _apu_filter *f, float input)
{
	float output = f->alpha * (f->prev_output + input - f->prev_input);

	f->prev_input = input;
	f->prev_output = output;
	return output;
}

/**
 * Convert float samples to int16_t
 * @param sample Float value
 * @return int16_t converted value
 */
static int16_t float_to_int16(float sample)
{
	float scaled = sample * 32767.0f;

	/* Prevent overflow */
	if (scaled > 32767.0f)
		return 32767;

	if (scaled < -32768.0f)
		return -32768;

	return (int16_t)scaled;
}

/**
 * Return the current APU sound level output
 */
static float apu_get_output(void)
{
	uint16_t pwm[2], tr, noise, dmc;

	pwm[0] = pwm_get_output(0);
	pwm[1] = pwm_get_output(1);
	tr = tri_get_output();
	noise = noise_get_output();
	dmc = dmc_get_output();

	return GET_SND_OUT(pwm[0], pwm[1], tr, noise, dmc);
}

/**
 * Initialize APU’s internal structures
 * @return 0 on success, -1 otherwise
 */
int apu_init(void)
{
	int ret;

	ret = rbuff_init(&apu.buffer, AUDIO_BUFFER_LEN);
	if (ret < 0)
		return ret;
	apu_reset();
	return 0;
}

/**
 * Finalize APU’s internal structures
 *
 * @return 0 on success, -1 otherwise
 */
int apu_destroy(void)
{
	apu.stop = 1;
	audio_stop();
	rbuff_destroy(&apu.buffer);
	return 0;
}

/**
 * Resets APU’s to initial state
 */
void apu_reset(void)
{
	audio_stop();
	memset(&apu.frm_seq, 0, sizeof(struct _apu_frm_seq));
	memset(apu.PWM, 0, sizeof(struct _apu_pwm_channel) * 2);
	memset(&apu.triangle, 0, sizeof(struct _apu_triangle_channel));
	memset(&apu.noise, 0, sizeof(struct _apu_noise_channel));
	memset(&apu.dmc, 0, sizeof(struct _apu_dmc_channel));
	apu.noise.shift = 1;
	apu.type = APU_NTSC;
	apu.freq_ratio = APU_NTSC_FREQ / AUDIO_SAMPLE_RATE;
	apu.clock = 0;
	apu.triangle.r.reg.raw = 0;
	apu.frm_seq_reset_cnt = 0;
	apu.dmc.cpu_sus = -1;
	apu.dmc.buffer.is_empty = 1;
	apu.sample_counter = 0;
	apu.sample = 0;
	apu.stop = 0;
	frm_seq_reset();
	init_filters();
	audio_play();
}

/**
 * Read one byte (8 bits) from the specified register
 * @param [in] reg Register address
 * @return Register value
 */
uint8_t apu_reg_read(uint16_t reg)
{
	uint8_t value;
	struct _apu_status st;

	st.reg.raw = 0;
	switch (reg) {
	case REG_STATUS:
		if (apu.PWM[0].lc.counter > 0)
			st.reg.bits.pwm_0 = 1;

		if (apu.PWM[1].lc.counter > 0)
			st.reg.bits.pwm_1 = 1;

		if (apu.triangle.lc.counter > 0)
			st.reg.bits.triangle = 1;

		if (apu.noise.lc.counter > 0)
			st.reg.bits.noise = 1;

		/* Clear frame counter interrupt flag */
		st.reg.bits.frm_int = apu.frm_seq.irq_flag;
		apu.frm_seq.irq_flag = 0;
		cpu_clear_irq();

		/* DMC */
		if (apu.dmc.dma.bytes_remaining > 0)
			st.reg.bits.dmc = 1;
		else
			st.reg.bits.dmc = 0;

		/* DMC interrupt flag */
		st.reg.bits.dmc_int = apu.dmc.irq_flag;
		value = st.reg.raw;
		break;

	default:
		value = 0;
	}

	return value;
}

/**
 * Write one byte (8 bits) to the specified register
 * @param [in] reg Register address
 * @param [in] value Value
 */
void apu_reg_write(uint16_t reg, uint8_t value)
{
	switch (reg) {
	case REG_PWM0:
		write_pwm_reg(0, value);
		break;

	case REG_PWM0_SWEEP:
		apu.PWM[0].sw.sweep.reg.raw = value;
		apu.PWM[0].sw.reload = 1;
		tm_load(&apu.PWM[0].sw.divider,
				apu.PWM[0].sw.sweep.reg.bits.period + 1);
		break;

	case REG_PWM0_RAWL:
		apu.PWM[0].raw = (apu.PWM[0].raw & 0x700) | value;
		tm_load(&apu.PWM[0].tm, apu.PWM[0].raw);
		break;

	case REG_PWM0_RAWH:
		write_pwm_lc_reg(0, value);
		break;

	case REG_PWM1:
		write_pwm_reg(1, value);
		break;

	case REG_PWM1_SWEEP:
		apu.PWM[1].sw.sweep.reg.raw = value;
		apu.PWM[1].sw.reload = 1;
		tm_load(&apu.PWM[1].sw.divider,
				apu.PWM[1].sw.sweep.reg.bits.period + 1);
		break;

	case REG_PWM1_RAWL:
		apu.PWM[1].raw = (apu.PWM[1].raw & 0x700) | value;
		tm_load(&apu.PWM[1].tm, apu.PWM[1].raw);
		break;

	case REG_PWM1_RAWH:
		write_pwm_lc_reg(1, value);
		break;

	case REG_TRI_CTR:
		apu.triangle.lin.reload_value = (value & 0x7f);
		apu.triangle.lin.counter = apu.triangle.lin.reload_value;
		if ((value & 0x80)) {
			apu.triangle.lin.ctrl = 1;
			apu.triangle.lc.halt = 1;
		} else {
			apu.triangle.lin.ctrl = 0;
			apu.triangle.lc.halt = 0;
		}
		break;

	case REG_TRI_TMRL:
		apu.triangle.raw = (apu.triangle.raw & 0x700) | value;
		break;

	case REG_TRI_TMRH:
		apu.triangle.raw &= 0xff; /* Clear upper bits */
		apu.triangle.raw |= ((value & 0x7) << 8);
		apu.triangle.lin.reload = 1;
		if (apu.status.reg.bits.triangle)
			lc_load(&apu.triangle.lc, (value & 0xf8) >> 3);
		tm_load(&apu.triangle.tm, apu.triangle.raw);
		break;

	case REG_NOISE_VOL:
		apu.noise.renv.reg.raw = value;
		apu.noise.env.volume = apu.noise.renv.reg.bits.vol_env;
		tm_load(&apu.noise.env.divider, apu.noise.env.volume + 1);
		apu.noise.env.const_vol = apu.noise.renv.reg.bits.cv_env;
		apu.noise.env.loop = apu.noise.renv.reg.bits.lc_halt;
		lc_halt(&apu.noise.lc, apu.noise.renv.reg.bits.lc_halt);
		break;

	case REG_NOISE_TONE:
		apu.noise.r.reg.raw = value;
		tm_load(&apu.noise.tm,
				noise_period[apu.type][apu.noise.r.reg.bits.period]);
		break;

	case REG_NOISE_LC:
		lc_load(&apu.noise.lc, (value & 0xf8) >> 3);
		env_reset(&apu.noise.env);
		break;

	case REG_FRM_SEQ:
		if ((value & 0x80)) {
			apu.frm_seq.mode = 1;
			apu.frm_seq.irq_inhibit = 1;
		} else {
			apu.frm_seq.mode = 0;
			if ((value & 0x40)) {
				apu.frm_seq.irq_inhibit = 1;
				apu.frm_seq.irq_flag = 0;
				cpu_clear_irq();
			} else {
				apu.frm_seq.irq_inhibit = 0;
			}
		}

		if ((apu.clock & 0x1) == 0)
			apu.frm_seq_reset_cnt = 3;
		else
			apu.frm_seq_reset_cnt = 4;
		break;

	case REG_STATUS:
		apu.status.reg.raw = value;
		lc_set_enable(&apu.PWM[0].lc, apu.status.reg.bits.pwm_0);
		lc_set_enable(&apu.PWM[1].lc, apu.status.reg.bits.pwm_1);
		lc_set_enable(&apu.triangle.lc, apu.status.reg.bits.triangle);
		lc_set_enable(&apu.noise.lc, apu.status.reg.bits.noise);
		if (apu.status.reg.bits.dmc) {
			/* DMC is active */
			if (apu.dmc.dma.bytes_remaining == 0) {
				apu.dmc.dma.current_addr = apu.dmc.dma.sample_addr;
				apu.dmc.dma.bytes_remaining = apu.dmc.dma.sample_len;

				if (apu.dmc.cycles > apu.dmc.period)
					apu.dmc.cycles = 0;
			}
			apu.dmc.out.silence = 0;
		} else {
			/* DMC bit is clear */
			apu.dmc.dma.bytes_remaining = 0;
			apu.dmc.out.silence = 1;
		}
		/* Clear DMC irq flag */
		apu.dmc.irq_flag = 0;
		apu.status.reg.bits.dmc_int = 0;
		break;

	case REG_DMC:
		apu.dmc.dmc.reg.raw = value;
		apu.dmc.period = dmc_period[apu.type][apu.dmc.dmc.reg.bits.rate];

		if (!apu.dmc.dmc.reg.bits.irq) {
			apu.dmc.irq_flag = 0;
			cpu_clear_irq();
		}
		break;

	case REG_DMC_DLOAD:
		apu.dmc.out.output = value;
		break;

	case REG_DMC_SADDR:
		apu.dmc.dma.sample_addr = 0xC000 + (value * 64);
		apu.dmc.dma.current_addr = apu.dmc.dma.sample_addr;
		break;

	case REG_DMC_SLEN:
		apu.dmc.dma.sample_len = (value * 16) + 1;
		break;
	}
}

/**
 * Performs one clock tick in the APU
 */
void apu_clock(void)
{
	uint8_t feedback, bsh;
	int16_t final_sample;
	float f1, f2, f3;

	/* Check if we need to reset frame counter */
	if (apu.frm_seq_reset_cnt > 0) {
		if (--apu.frm_seq_reset_cnt == 0) {
			frm_seq_reset();
			if (apu.frm_seq.mode) {
				frm_QuarterFrame();
				frm_HalfFrame();
			}
		}
	}

	/* Pulse channel and frame sequencer timers are incremented on a CPU/2
	 * clock rate, we achieve that by only incrementing on even cycles
	 */
	if ((apu.clock++ & 0x1) == 0) {
		/* PWM channels */
		if (tm_clock(&apu.PWM[0].tm))
			pwm_seq_clock(&apu.PWM[0].seq);
		if (tm_clock(&apu.PWM[1].tm))
			pwm_seq_clock(&apu.PWM[1].seq);

		/* Clocks the frame sequencer */
		frm_seq_clock();

		/* Assert CPU IRQ line if needed */
		if (!apu.frm_seq.irq_inhibit && apu.frm_seq.irq_flag)
			cpu_trigger_irq();
	}

	/* Triangle channel */
	if (tm_clock(&apu.triangle.tm)) {
		if (lc_output(&apu.triangle.lc) > 0 && apu.triangle.lin.counter > 0)
			tri_seq_clock(&apu.triangle.seq);
	}

	/* Noise channel */
	if (tm_clock(&apu.noise.tm)) {
		/* Calculate feedback */
		if (apu.noise.r.reg.bits.tone_mode)
			bsh = 5;
		else
			bsh = 1;
		feedback = ((apu.noise.shift & 0x1) ^ ((apu.noise.shift >> bsh) & 0x1));
		/* Shift register */
		apu.noise.shift >>= 1;
		/* Set feedback */
		apu.noise.shift &= ~(1 << 14);
		apu.noise.shift |= (feedback << 14);
	}

	/* DMC channel */
	dmc_clock();

	/* Get an audio sample and push it to audio buffer (when needed) */
	apu.sample += apu_get_output();
	if (apu.sample_counter++ >= apu.freq_ratio) {
		/* APU's clock ticks only on this function, so we need to
		 * compensate the frequency ratio of audio sampling, which is
		 * lower than the clock frequency of the real hardware. In a
		 * nutshell, sound card takes an audio sample every apu.freq_ratio
		 * times, so we will just make an average of the output between
		 * the samples and push it to the audio buffer.
		 */
		apu.sample /= (float)apu.freq_ratio;
		/* Apply the chain of filters */
		f1 = high_pass_filter(&apu.hp_90, apu.sample);
		f2 = high_pass_filter(&apu.hp_440, f1);
		f3 = low_pass_filter(&apu.lp_14k, f2);
		final_sample = float_to_int16(f3);
		/* Push final sample to the audio buffer */
		rbuff_put(&apu.buffer, final_sample);
		apu.sample = 0;
		apu.sample_counter = 0;
	}
}

/**
 * APU's callback function to retrieve audio samples
 */
void apu_get_samples(void *data, uint8_t *stream, int len)
{
	int16_t *snd_buffer = (int16_t *)stream;
	size_t nsamples;
	int i;

	nsamples = len / sizeof(int16_t);
	for (i = 0; i < nsamples; i++)
		snd_buffer[i] = rbuff_get(&apu.buffer);
}
