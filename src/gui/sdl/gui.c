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
 * @file gui.c
 *
 * Implements the GUI HAL layer using the SDL library.
 */
#include "apu.h"
#include "controller.h"
#include "hal/hal.h"
#include "hussar_font.h"
#include <SDL.h>
#include <SDL_audio.h>
#include <SDL_ttf.h>
#include <semaphore.h>

#define WINDOW_TITLE "rNES"
#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240

#define KEY_PRESSED 1
#define KEY_RELEASED 0

/** Control keys */
enum CTRL_KEYS {
	P0K_UP = 0,
	P0K_DOWN,
	P0K_LEFT,
	P0K_RIGHT,
	P0K_A,
	P0K_B,
	P0K_SEL,
	P0K_START,
	P1K_UP,
	P1K_DOWN,
	P1K_LEFT,
	P1K_RIGHT,
	P1K_A,
	P1K_B,
	P1K_SEL,
	P1K_START,
};

/** GUI's state */
enum GUI_STATE {
	GUI_STOPPED,
	GUI_RUNNING,
};

/** rNES SDL GUI struct */
struct _RNES_SDL_GUI {
	/** Screen buffer */
	uint32_t *pixel_buffer;
	/** Main Window */
	SDL_Window *win;
	/** Main texture */
	SDL_Texture *texture;
	/** Texture for texts */
	SDL_Texture *text_texture;
	/** Main renderer */
	SDL_Renderer *renderer;
	/** TTF Embedded font */
	SDL_RWops *emb_font;
	/** TTF Font */
	TTF_Font *font;
	/** Audio mixer specs */
	SDL_AudioSpec mixer;
	/** Screen size: width */
	int width;
	/** Screen size: height */
	int height;
	/** Video scale */
	int scale;
	/** Control keys */
	uint8_t keys[16];
	/** GUI's state */
	enum GUI_STATE state;
	/** Lock */
	sem_t lock;
};

/** GUI struct */
static struct _RNES_SDL_GUI rnes_gui;

/* Initialize default colors */

/** Black color */
const RGB_t C_BLACK = {.R = 0x00, .G = 0x00, .B = 0x00};
/** White color */
const RGB_t C_WHITE = {.R = 0xff, .G = 0xff, .B = 0xff};
/** Red color */
const RGB_t C_RED = {.R = 0xff, .G = 0x00, .B = 0x00};
/** Green color */
const RGB_t C_GREEN = {.R = 0x00, .G = 0xff, .B = 0x00};
/** Blue color */
const RGB_t C_BLUE = {.R = 0x00, .G = 0x00, .B = 0xff};

/**
 * Initialize GUI
 * @param [in] Screen scale factor (if 0, set default scale)
 * @return 0 on success, -1 otherwise.
 */
int gui_init(int scale)
{
	rnes_gui.win = NULL;
	rnes_gui.texture = NULL;
	rnes_gui.renderer = NULL;
	rnes_gui.state = GUI_STOPPED;

	/* Setup Audio specs */
	SDL_zero(rnes_gui.mixer);
	rnes_gui.mixer.freq = AUDIO_SAMPLE_RATE;
	rnes_gui.mixer.format = AUDIO_S16;
	rnes_gui.mixer.channels = 1;
	rnes_gui.mixer.silence = 0;
	rnes_gui.mixer.samples = AUDIO_BUFFER_LEN;
	rnes_gui.mixer.padding = 0;
	rnes_gui.mixer.callback = apu_get_samples;
	rnes_gui.mixer.userdata = &rnes_gui.mixer;

	/* Allocate a back-buffer for the pixmap (256x240 pixels * 4 bytes each) */
	rnes_gui.pixel_buffer =
		(uint32_t *)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
	memset(rnes_gui.pixel_buffer, 0,
		   SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));

	/* Initialize sempahore */
	if (sem_init(&rnes_gui.lock, 0, 1) < 0)
		return -1;

	/* Initialize SDL */
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
		log_err("Error initializing SDL library: %s\n", SDL_GetError());
		sem_destroy(&rnes_gui.lock);
		return -1;
	}

	/* Initialize audio mixer */
	if (SDL_OpenAudio(&rnes_gui.mixer, NULL) < 0) {
		log_err("Error initializing SDL sound mixer: %s\n", SDL_GetError());
		sem_destroy(&rnes_gui.lock);
		SDL_Quit();
		return -1;
	}

	/* Initialize TTF library */
	if (TTF_Init() < 0) {
		log_err("Error initializing TTF library: %s\n", TTF_GetError());
		SDL_Quit();
		sem_destroy(&rnes_gui.lock);
		return -1;
	}
	rnes_gui.emb_font = SDL_RWFromMem(HUSSARBOLDWEBEDITION_XQ5O_TTF,
									  HUSSARBOLDWEBEDITION_XQ5O_TTF_LEN);
	rnes_gui.font = TTF_OpenFontRW(rnes_gui.emb_font, 1, 13);
	if (!rnes_gui.emb_font || !rnes_gui.font) {
		log_err("Cannot open TTF Font.\n");
		TTF_Quit();
		SDL_Quit();
		sem_destroy(&rnes_gui.lock);
		return -1;
	}

	/* Check scale */
	if (scale == 0) {
		rnes_gui.width = SCREEN_WIDTH;
		rnes_gui.height = SCREEN_HEIGHT;
		rnes_gui.scale = 1;
	} else {
		rnes_gui.width = SCREEN_WIDTH * scale;
		rnes_gui.height = SCREEN_HEIGHT * scale;
		rnes_gui.scale = scale;
	}

	/* Create main window */
	rnes_gui.win = SDL_CreateWindow(WINDOW_TITLE, SDL_WINDOWPOS_UNDEFINED,
									SDL_WINDOWPOS_UNDEFINED, rnes_gui.width,
									rnes_gui.height, SDL_WINDOW_SHOWN);
	/* Create Hardware Accelerated Renderer */
	rnes_gui.renderer =
		SDL_CreateRenderer(rnes_gui.win, -1, SDL_RENDERER_ACCELERATED);
	/* Create Streaming Texture */
	rnes_gui.texture = SDL_CreateTexture(
		rnes_gui.renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
	/* Create texture for texts */
	rnes_gui.text_texture = SDL_CreateTexture(
		rnes_gui.renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);

	/* Set scaling hint to keep pixels sharp */
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	if (rnes_gui.texture == NULL || rnes_gui.renderer == NULL ||
		rnes_gui.text_texture == NULL) {
		SDL_PauseAudio(1);
		TTF_Quit();
		SDL_Quit();
	}
	SDL_SetTextureBlendMode(rnes_gui.text_texture, SDL_BLENDMODE_BLEND);

	SDL_PauseAudio(0);
	return 0;
}

int gui_destroy(void)
{
	/* Free Textures */
	SDL_DestroyTexture(rnes_gui.texture);
	SDL_DestroyTexture(rnes_gui.text_texture);

	/* Destroy main window */
	SDL_DestroyWindow(rnes_gui.win);

	/* Finalize SDL */
	TTF_CloseFont(rnes_gui.font);
	TTF_Quit();
	SDL_Quit();

	/* Destroy semaphore */
	sem_destroy(&rnes_gui.lock);

	return 0;
}

int gui_main_loop(void)
{
	SDL_Event main_ev;
	char quit;
	int key_state;

	/* Main loop */
	rnes_gui.state = GUI_RUNNING;
	quit = 0;
	do {
		sem_wait(&rnes_gui.lock);

		/* Clear GPU backbuffer*/
		SDL_RenderClear(rnes_gui.renderer);

		/* Upload pixmap buffer to GPU texture */
		SDL_UpdateTexture(rnes_gui.texture, NULL, rnes_gui.pixel_buffer,
						  SCREEN_WIDTH * sizeof(uint32_t));

		/* Copy texture to renderer (GPU handles scaling to window size) */
		SDL_RenderCopy(rnes_gui.renderer, rnes_gui.texture, NULL, NULL);

		/* Copy text texture */
		SDL_RenderCopy(rnes_gui.renderer, rnes_gui.text_texture, NULL, NULL);

		/* Present to screen */
		SDL_RenderPresent(rnes_gui.renderer);

		sem_post(&rnes_gui.lock);

		while (SDL_PollEvent(&main_ev) != 0) {
			if (main_ev.type == SDL_QUIT) {
				quit = 1;
				break;
			} else if (main_ev.type == SDL_KEYDOWN ||
					   main_ev.type == SDL_KEYUP) {
				if (main_ev.type == SDL_KEYUP)
					key_state = KEY_RELEASED;
				else
					key_state = KEY_PRESSED;

				switch (main_ev.key.keysym.sym) {
				case SDLK_UP:
					rnes_gui.keys[P0K_UP] = key_state;
					break;
				case SDLK_DOWN:
					rnes_gui.keys[P0K_DOWN] = key_state;
					break;
				case SDLK_LEFT:
					rnes_gui.keys[P0K_LEFT] = key_state;
					break;
				case SDLK_RIGHT:
					rnes_gui.keys[P0K_RIGHT] = key_state;
					break;
				case SDLK_z:
					rnes_gui.keys[P0K_A] = key_state;
					break;
				case SDLK_x:
					rnes_gui.keys[P0K_B] = key_state;
					break;
				case SDLK_a:
					rnes_gui.keys[P0K_START] = key_state;
					break;
				case SDLK_s:
					rnes_gui.keys[P0K_SEL] = key_state;
					break;
				case SDLK_q:
					quit = 1;
					break;
				}
			}
		}
		SDL_Delay(10);
	} while (!quit);

	audio_stop();
	rnes_gui.state = GUI_STOPPED;
	return 0;
}

void gui_set_pixel(int x, int y, RGB_t color)
{
	uint32_t argb;

	if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
		return;

	/* Convert RGB to ARGB8888 and write directly to the buffer */
	argb = (0xff << 24) | (color.R << 16) | (color.G << 8) | color.B;
	rnes_gui.pixel_buffer[y * SCREEN_WIDTH + x] = argb;
}

RGB_t gui_get_pixel(int x, int y)
{
	uint32_t argb = rnes_gui.pixel_buffer[y * SCREEN_WIDTH + x];

	RGB_t pixel = {
		.R = (argb & 0xff),
		.G = ((argb >> 8) & 0xff),
		.B = ((argb >> 16) & 0xff),
	};
	return pixel;
}

void gui_draw_text(int x, int y, const char *text, RGB_t color)
{
	SDL_Color txtcolor = {color.R, color.G, color.B, 255};
	SDL_Surface *gtext_surf;

	/* Create Surface from Font */
	gtext_surf = TTF_RenderText_Blended(rnes_gui.font, text, txtcolor);
	if (gtext_surf == NULL)
		return;

	sem_wait(&rnes_gui.lock);

	/* Update text texture with the new surface */
	SDL_Rect dest = {x, y, gtext_surf->w, gtext_surf->h};
	SDL_UpdateTexture(rnes_gui.text_texture, &dest, gtext_surf->pixels,
					  gtext_surf->pitch);
	SDL_FreeSurface(gtext_surf);

	sem_post(&rnes_gui.lock);
}

int gui_get_scale(void) { return rnes_gui.scale; }

int gui_set_scale(int scale)
{
	if (scale <= 0)
		return -1;

	rnes_gui.scale = scale;
	rnes_gui.width = SCREEN_WIDTH * scale;
	rnes_gui.height = SCREEN_HEIGHT * scale;
	SDL_SetWindowSize(rnes_gui.win, rnes_gui.width, rnes_gui.height);
	return 0;
}

int gui_is_running(void)
{
	if (rnes_gui.state == GUI_RUNNING)
		return 1;

	return 0;
}

uint8_t gui_read_joypad(int joypad)
{
	int i = 0;
	controller_reg_t j;

	if (joypad == 1)
		i = 8;

	j.reg.right = rnes_gui.keys[P0K_RIGHT + i];
	j.reg.left = rnes_gui.keys[P0K_LEFT + i];
	j.reg.down = rnes_gui.keys[P0K_DOWN + i];
	j.reg.up = rnes_gui.keys[P0K_UP + i];
	j.reg.start = rnes_gui.keys[P0K_START + i];
	j.reg.select = rnes_gui.keys[P0K_SEL + i];
	j.reg.A = rnes_gui.keys[P0K_A + i];
	j.reg.B = rnes_gui.keys[P0K_B + i];

	return j.raw;
}
