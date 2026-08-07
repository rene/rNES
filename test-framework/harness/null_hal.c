/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rNES test framework - headless HAL backend
 * Copyright 2021-2026 Renê de Souza Pinto
 *
 * SDL-free replacement for src/gui/sdl/{gui,audio,clock}.c. Provides every
 * HAL symbol the emulator core references, capturing video into RAM and
 * draining audio in the background. See null_hal.h.
 */
#include "null_hal.h"
#include "apu.h"
#include "hal/hal.h"
#include <pthread.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Default colors (declared extern const in hal.h, defined by the GUI) */
/* ------------------------------------------------------------------ */

const RGB_t C_BLACK = {.R = 0x00, .G = 0x00, .B = 0x00};
const RGB_t C_WHITE = {.R = 0xff, .G = 0xff, .B = 0xff};
const RGB_t C_RED = {.R = 0xff, .G = 0x00, .B = 0x00};
const RGB_t C_GREEN = {.R = 0x00, .G = 0xff, .B = 0x00};
const RGB_t C_BLUE = {.R = 0x00, .G = 0x00, .B = 0xff};

/* ------------------------------------------------------------------ */
/* Video: in-RAM framebuffer                                          */
/* ------------------------------------------------------------------ */

static uint32_t framebuffer[NULL_SCREEN_WIDTH * NULL_SCREEN_HEIGHT];
static int gui_running;

const uint32_t *null_framebuffer(void) { return framebuffer; }

int gui_init(int scale)
{
	(void)scale;
	memset(framebuffer, 0, sizeof(framebuffer));
	gui_running = 1;
	return 0;
}

int gui_destroy(void)
{
	gui_running = 0;
	return 0;
}

int gui_main_loop(void) { return 0; }

void gui_set_pixel(int x, int y, RGB_t color)
{
	uint32_t argb;

	if (x < 0 || x >= NULL_SCREEN_WIDTH || y < 0 || y >= NULL_SCREEN_HEIGHT)
		return;

	/* Same ARGB8888 packing used by the SDL backend */
	argb = (0xffu << 24) | (color.R << 16) | (color.G << 8) | color.B;
	framebuffer[y * NULL_SCREEN_WIDTH + x] = argb;
}

RGB_t gui_get_pixel(int x, int y)
{
	uint32_t argb = 0;
	RGB_t pixel;

	if (x >= 0 && x < NULL_SCREEN_WIDTH && y >= 0 && y < NULL_SCREEN_HEIGHT)
		argb = framebuffer[y * NULL_SCREEN_WIDTH + x];

	pixel.R = (argb >> 16) & 0xff;
	pixel.G = (argb >> 8) & 0xff;
	pixel.B = argb & 0xff;
	return pixel;
}

void gui_draw_text(int x, int y, const char *text, RGB_t color)
{
	(void)x;
	(void)y;
	(void)text;
	(void)color;
}

int gui_get_scale(void) { return 1; }

int gui_set_scale(int scale)
{
	(void)scale;
	return 0;
}

int gui_is_running(void) { return gui_running; }

/* Injected controller-1 state (raw controller_reg_t byte); set by the runner
 * to simulate button presses (e.g. START to get past a title screen). */
static volatile uint8_t joypad0_raw;

void null_set_joypad(uint8_t raw) { joypad0_raw = raw; }

uint8_t gui_read_joypad(int joypad)
{
	/* Controller 1 replays the scripted input; controller 2 stays idle. */
	return joypad == 0 ? joypad0_raw : 0;
}

/* ------------------------------------------------------------------ */
/* Audio: discard everything via a background drain thread            */
/* ------------------------------------------------------------------ */

static pthread_t drain_th;
static int drain_started;

/*
 * apu_clock() pushes samples with rbuff_put(), which blocks on a counting
 * semaphore once the ring is full. Nothing consumes audio in headless mode,
 * so without a drainer the emulation loop would deadlock mid-frame. This
 * thread mimics the sound card callback and throws the samples away.
 */
static void *audio_drain_loop(void *arg)
{
	uint8_t buf[AUDIO_BUFFER_LEN * sizeof(int16_t)];

	(void)arg;
	for (;;)
		apu_get_samples(NULL, buf, sizeof(buf));
	return NULL;
}

int null_audio_start_drain(void)
{
	if (drain_started)
		return 0;
	if (pthread_create(&drain_th, NULL, audio_drain_loop, NULL) != 0)
		return -1;
	pthread_detach(drain_th);
	drain_started = 1;
	return 0;
}

int audio_init(void) { return 0; }
int audio_destroy(void) { return 0; }
int audio_play(void) { return 0; }
int audio_stop(void) { return 0; }

int audio_set_cb(void (*audio_cb)(void *, void *, int), void *data)
{
	(void)audio_cb;
	(void)data;
	return 0;
}

int audio_stream_put(void *stream, void *data, int len, int volume)
{
	(void)stream;
	(void)data;
	(void)len;
	(void)volume;
	return 0;
}

/* ------------------------------------------------------------------ */
/* Clock: unused by the core (only the SDL backend used timers)       */
/* ------------------------------------------------------------------ */

int register_clock_cb(int interval,
					  unsigned int (*clock_cb)(unsigned int, void *),
					  void *data)
{
	(void)interval;
	(void)clock_cb;
	(void)data;
	return 0;
}

int unregister_clock_cb(int timerID)
{
	(void)timerID;
	return 0;
}
