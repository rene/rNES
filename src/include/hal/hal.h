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
 * @file hal.h
 *
 * HAL - Hardware Abstraction Layer functions
 */
#ifndef __HAL_H__
#define __HAL_H__

#include <stdint.h>
#include <stdlib.h>

/* For macOS compatibility */
#ifndef ssize_t
typedef long ssize_t;
#endif

/* LOG operations */
#ifndef log_err
#define log_err(...) fprintf(stderr, "Error: " __VA_ARGS__)
#endif
#ifndef log_info
#define log_info(...) fprintf(stdout, __VA_ARGS__)
#endif
#ifndef log_debug
#define log_debug(...) fprintf(stdout, __VA_ARGS__)
#endif

/** Audio buffer size */
#define AUDIO_BUFFER_LEN 2048
/** Audio sample rate */
#define AUDIO_SAMPLE_RATE 44100

/* GUI types */

/** RGB color */
struct _RGB_COLOR {
	/** Red */
	uint8_t R;
	/** Green */
	uint8_t G;
	/** Blue */
	uint8_t B;
};

/** RGB color type */
typedef struct _RGB_COLOR RGB_t;

/* Default colors (must be initialized by GUI) */

/** Black color */
extern const RGB_t C_BLACK;
/** White color */
extern const RGB_t C_WHITE;
/** Red color */
extern const RGB_t C_RED;
/** Green color */
extern const RGB_t C_GREEN;
/** Blue color */
extern const RGB_t C_BLUE;

/* File operations */

ssize_t load_file(const char *pathname, uint8_t **data);

/* GUI functions */

int gui_init(int scale);
int gui_destroy(void);
int gui_main_loop(void);
void gui_set_pixel(int x, int y, RGB_t color);
RGB_t gui_get_pixel(int x, int y);
void gui_draw_text(int x, int y, const char *text, RGB_t color);
int gui_get_scale(void);
int gui_set_scale(int scale);
int gui_is_running(void);
uint8_t gui_read_joypad(int joypad);

/* Audio functions */

int audio_init(void);
int audio_destroy(void);
int audio_play(void);
int audio_stop(void);
int audio_set_cb(void (*audio_cb)(void *, void *, int), void *data);
int audio_stream_put(void *stream, void *data, int len, int volume);

/* Clock function */

int register_clock_cb(int interval,
					  unsigned int (*clock_cb)(unsigned int, void *),
					  void *data);
int unregister_clock_cb(int timerID);

#endif /* __HAL_H__ */
