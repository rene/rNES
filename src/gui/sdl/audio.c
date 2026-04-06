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
 * @file audio.c
 *
 * Implements the Audio HAL layer using the SDL library.
 */
#include "hal/hal.h"
#include <SDL_audio.h>
#include <errno.h>

/**
 * Initialize audio mixer internal structures. At this stage, it should open
 * and prepare sound mixer devices in the host.
 * @return int 0 on success, error code otherwise.
 */
int audio_init(void)
{
	/* Audio is initialized on gui_init(). Nothing to do here. */
	return 0;
}

/**
 * Finalize the audio mixer internal structures.
 * @return int 0 on success, error code otherwise.
 */
int audio_destroy(void)
{
	/* Audio is finalized on gui_init(). Nothing to do here. */
	return 0;
}

/**
 * Enable audio mixer and allow play sounds.
 * @return int 0 on success, error code otherwise.
 */
int audio_play(void)
{
	SDL_PauseAudio(0);
	return 0;
}

/**
 * Stop any audio being played.
 * @return int 0 on success, error code otherwise.
 */
int audio_stop(void)
{
	SDL_PauseAudio(1);
	return 0;
}

/**
 * Send sound data to the sound mixer device.
 * @param [in] data Audio samples (buffer)
 * @param [in] len Buffer length
 * @param [in] volume Audio volume
 * @return int 0 on success, error code otherwise.
 */
int audio_stream_put(void *stream, void *data, int len, int volume)
{
	SDL_MixAudio(stream, data, len, volume);
	return 0;
}
