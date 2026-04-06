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
 * @file clock.c
 *
 * Implements the timer (clock) callback function using the SDL library.
 */
#include "hal/hal.h"
#include <SDL_timer.h>
#include <errno.h>

/**
 * Register Timer
 * @param [in] interval Time delay, in milliseconds
 * @param [in] clock_cb Callback function
 * @param [in] data Custom data to callback function
 * @return int Timer ID on success, error code otherwise.
 */
int register_clock_cb(int interval,
					  unsigned int (*clock_cb)(unsigned int, void *),
					  void *data)
{
	SDL_TimerID timerID = SDL_AddTimer(interval, clock_cb, data);

	return (int)timerID;
}

/**
 * Unregister timer
 * @param [in] timerID ID returned by register_clock_cb()
 * @return int 0 on success, -1 otherwise
 */
int unregister_clock_cb(int timerID)
{
	if (SDL_RemoveTimer(timerID) == SDL_TRUE)
		return 0;
	else
		return -1;
}
