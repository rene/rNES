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
 * @file rNES.c
 *
 * POSIX implementation of the main interface for rNES.
 */
#include "apu.h"
#include "cartridge.h"
#include "controller.h"
#include "cpu650x.h"
#include "hal/hal.h"
#include "ppu.h"
#include "sbus.h"
#include <getopt.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/** Generate a version string if not defined */
#ifndef RNES_VERSION
#define RNES_VERSION "dev-build-unknown-version"
#endif

/* Default screen scale factor */
#define DEFAULT_SCALE 2

/** Loaded cartridge */
static cartridge_t *cartridge;

/** Quit emulation flag */
static int quit_emulation;

/** Semaphore to wait for GUI initialization */
sem_t wait_gui;

/* Platform specific tuning */
#ifdef _WIN32
#include <timeapi.h>
#include <windows.h>
inline static void platform_start(void)
{
	timeBeginPeriod(1);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
};
inline static void platform_end(void) { timeEndPeriod(1); };
#else
inline static void platform_start(void) {};
inline static void platform_end(void) {};
#endif

#ifdef _WIN32
static inline int s_init(sem_t *s, unsigned int value)
{
	*s = CreateSemaphore(NULL, value, LONG_MAX, NULL);
	if (s == NULL)
		return -1;
	else
		return 0;
};
static inline int s_lock(sem_t *s)
{
	DWORD ret = WaitForSingleObject(*s, INFINITE);
	if (ret == WAIT_OBJECT_0)
		return 0;
	else
		return -1;
};
static inline int s_unlock(sem_t *s)
{
	if (ReleaseSemaphore(*s, 1, NULL) == 0)
		return 0;
	else
		return -1;
};
static inline int s_destroy(sem_t *s)
{
	if (CloseHandle(*s) == 0)
		return 0;
	else
		return -1;
};
#elif defined(__APPLE__)
/** Initialize semaphore */
static inline int s_init(sem_t *sem, unsigned int value)
{
	*sem = dispatch_semaphore_create(value);
	if (sem == NULL)
		return -1;
	else
		return 0;
}
/** Destroy semaphore */
static inline int s_destroy(sem_t *sem)
{
	dispatch_release(*sem);
	return 0;
};
/** Semaphore DOWN function */
static inline int s_lock(sem_t *sem)
{
	return dispatch_semaphore_wait(*sem, DISPATCH_TIME_FOREVER);
};
/** Semaphore UP function */
static inline int s_unlock(sem_t *sem)
{
	return dispatch_semaphore_signal(*sem);
};
#elif defined(_POSIX_C_SOURCE)
/** Initialize semaphore */
static inline int s_init(sem_t *sem, unsigned int value)
{
	return sem_init(sem, 0, value);
}
/** Destroy semaphore */
static inline int s_destroy(sem_t *sem) { return sem_destroy(sem); };
/** Semaphore DOWN function */
static inline int s_lock(sem_t *sem) { return sem_wait(sem); };
/** Semaphore UP function */
static inline int s_unlock(sem_t *sem) { return sem_post(sem); };
#else
/** Initialize semaphore */
static inline int s_init(sem_t *sem, unsigned int value)
{
	unsigned int *s = (unsigned int *)sem;

	*s = value;
	return 0;
};
/** Destroy semaphore */
static inline int s_destroy(sem_t *sem)
{
	unsigned int *s = (unsigned int *)sem;

	*s = 0;
	return 0;
};
/** Semaphore DOWN function */
static inline int s_lock(sem_t *sem)
{
	unsigned int *s = (unsigned int *)sem;
	int attempts = 1000;

	while (*s == 0 && attempts-- > 0)
		;

	*s = *s - 1;
	return 0;
};
/** Semaphore UP function */
static inline int s_unlock(sem_t *sem)
{
	unsigned int *s = (unsigned int *)sem;

	*s = *s + 1;
	return 0;
};
#endif


/**
 * Callback for CPU kill
 */
static void cpu_kill_cb(cpu650x_state_t debug_info)
{
	gui_draw_text(5, 5, "CPU KILLED!", C_RED);
}

/**
 * Callback for SBUS debugging
 */
static void sbus_debug_cb(mem_op_t op, uint16_t address, uint8_t value)
{
	char const *memop[] = {"READ", "WRITE"};

	log_debug("sbus: [%s] [addr: 0x%x] [value: 0x%x]\n", memop[op], address,
			  value);
}

/**
 * Callback for CPU debugging
 */
static void cpu_debug_cb(cpu650x_state_t debug_info)
{
	log_debug("cpu: %s | A:%d X:%d Y: %d P: %d | DATA: 0x%x | ADDR: 0x%x\n",
			  debug_info.opcode_name, debug_info.CPU.A, debug_info.CPU.X,
			  debug_info.CPU.Y, debug_info.CPU.P.reg, debug_info.data,
			  debug_info.address);
}

/**
 * Return the video standard in string format
 */
static const char *get_video_str(rom_t *rom)
{
	rom_video_t v;

	v = get_rom_video_std(rom);
	switch (v) {
	case ROM_NTSC:
		return "NTSC";
	case ROM_PAL_M:
		return "Pal M";
	case ROM_MULTIPLE:
		return "Multiple";
	case ROM_DENDY:
		return "Dendy";
	default:
		return "Unknown";
	}
}

/**
 * Return the mirroring mode in string format
 */
static const char *get_mirroring_str(rom_t *rom)
{
	rom_mirroring_t m;

	m = get_rom_mirroring(rom);
	switch (m) {
	case VERTICAL_MIRRORING:
		return "Vertical";
	case HORIZONTAL_MIRRORING:
		return "Horizontal";
	case FOUR_SCREEN:
		return "Four screen";
	default:
		return "Unknown";
	}
}

/**
 * Show ROM information
 */
static void show_rom_info(rom_t *rom, uint8_t chr_ram)
{
	if (rom == NULL)
		return;

	log_info("\nROM format:           %s\n",
			 rom->rom_fmt == INES_FMT ? "iNES" : "NES");
	log_info("Video standard:       %s\n", get_video_str(rom));
	log_info("PRG ROM size (bytes): %ld\n", rom->prg_size);
	log_info("CHR ROM size (bytes): %ld\n", chr_ram == 1 ? 0 : rom->chr_size);
	log_info("Mirroring mode:       %s\n", get_mirroring_str(rom));
	log_info("Mapper:               %d\n", rom->mapper);
	log_info("Trainer:              %s\n\n",
			 rom->trainer == NULL ? "No" : "Yes");
}

/**
 * Show information about controllers
 */
static void show_control_info(void)
{
	log_info("Controls:\n");
	log_info("    Start:    a\n");
	log_info("    Select:   s\n");
	log_info("    Button A: z\n");
	log_info("    Button B: x\n");
	log_info("    Move:     Arrow keys\n\n");
}

/**
 * Load and show ROM information
 */
static int load_show_rom_info(const char *romfile)
{
	rom_t *rom;
	int ret;

	/* Load the ROM file */
	ret = load_rom(romfile, &rom);
	if (ret < 0)
		return ret;

	show_rom_info(rom, 0);
	free(rom);
	return 0;
}

/**
 * Show program's license
 * @param progname Program's name
 */
static void show_license(const char *progname)
{
	log_info(
		" Copyright (C) 2024 Renê de Souza Pinto. All rights reserved.\n\n");
	log_info(
		" Redistribution and use in source and binary forms, with or without\n");
	log_info(
		" modification, are permitted provided that the following conditions are met:\n\n");

	log_info(
		" Redistributions of source code must retain the above copyright notice, this\n");
	log_info(" list of conditions and the following disclaimer.\n\n");

	log_info(
		" Redistributions in binary form must reproduce the above copyright notice,\n");
	log_info(
		" this list of conditions and the following disclaimer in the documentation\n");
	log_info(" and/or other materials provided with the distribution.\n\n");

	log_info(
		" Neither the name of the copyright holder nor the names of its contributors\n");
	log_info(
		" may be used to endorse or promote products derived from this software without\n");
	log_info(" specific prior written permission.\n\n");

	log_info(
		" THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\"\n");
	log_info(
		" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE\n");
	log_info(
		" IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\n");
	log_info(
		" ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE\n");
	log_info(
		" LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR\n");
	log_info(
		" CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF\n");
	log_info(
		" SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS\n");
	log_info(
		" INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN\n");
	log_info(
		" CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)\n");
	log_info(
		" ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE\n");
	log_info(" POSSIBILITY OF SUCH DAMAGE.\n\n");
}

/**
 * Show program's version
 * @param progname Program's name
 */
static void show_version(const char *progname)
{
	log_info("rNES - Nintendo Entertainment System Emulator\n\n");
	log_info("Version: %s\n\n", RNES_VERSION);
}

/**
 * Show program's help
 * @param progname Program's name
 */
static void show_help(const char *progname)
{
	show_version(progname);
	log_info("Use: %s [-s <scale_factor>] [-d] [-h] <rom_file>\n\n", progname);
	log_info("    -s, --scale      Set screen scale factor (default: %d)\n",
			 DEFAULT_SCALE);
	log_info(
		"    -d, --debug      Show debug messages (it can be extremely slow!)\n");
	log_info(
		"    -i, --info       Just show ROM information (don't play the game)\n");
	log_info("    -v, --version    Show version\n");
	log_info("    -l, --license    Show license\n");
	log_info("    -h, --help       Show this help\n\n");
}

/**
 * Emulation thread (main routine)
 */
static void *emu_th_loop(void *args)
{
	/* Emulation will be synchronized by the audio callbacks */
	while (!quit_emulation)
		sbus_clock();
	return NULL;
}

/**
 * GUI emulation thread (main routine)
 * @param args Pointer to an integer variable with scale factor
 */
static void *gui_th_loop(void *args)
{
	int *scale = (int *)args;

	if (gui_init(*scale) < 0) {
		log_err("Could not initialize GUI!\n");
		return NULL;
	}
	s_unlock(&wait_gui);
	gui_main_loop();
	quit_emulation = 1;
	return NULL;
}

int main(int argc, char *const argv[])
{
	static struct option long_opts[] = {{"help", no_argument, NULL, 'h'},
										{"scale", required_argument, NULL, 's'},
										{"info", no_argument, NULL, 'i'},
										{"debug", no_argument, NULL, 'd'},
										{"version", no_argument, NULL, 'v'},
										{"license", no_argument, NULL, 'l'},
										{NULL, no_argument, NULL, 0}};
	const char *opt_string = "hs:idvl";
	char *romfile;
	char debug, info;
	int opt, optli, scale, ret;
	pthread_t th_gui, th_emu;
	pthread_attr_t attr_gui, attr_emu;

	/* Handle command line */
	debug = 0;
	info = 0;
	scale = DEFAULT_SCALE;
	while ((opt = getopt_long(argc, argv, opt_string, long_opts, &optli)) !=
		   -1) {
		switch (opt) {
		case 's':
			scale = atoi(optarg);
			break;

		case 'i':
			info = 1;
			break;

		case 'd':
			debug = 1;
			break;

		case 'v':
			show_version(argv[0]);
			return EXIT_SUCCESS;

		case 'l':
			show_version(argv[0]);
			show_license(argv[0]);
			return EXIT_SUCCESS;

		case 'h':
		default:
			show_help(argv[0]);
			return EXIT_SUCCESS;
		}
	}

	/* Check ROM file */
	if (optind != (argc - 1)) {
		show_help(argv[0]);
		log_err("No ROM file was provided.\n\n");
		return EXIT_FAILURE;
	}
	romfile = argv[optind];

	/* Show banner */
	show_version(argv[0]);

	/* Just show ROM info */
	if (info) {
		ret = load_show_rom_info(romfile);
		return ret;
	}

	/* Validate scale factor */
	if (scale < 0) {
		log_err("Scale factor must be a positive integer.\n\n");
		return EXIT_FAILURE;
	}

	/* Load ROM file */
	log_info("Loading ROM: %s ...\n", romfile);
	ret = cartridge_load(romfile, &cartridge);
	if (ret < 0) {
		switch (ret) {
		case -ENOTSUP:
			log_err("ROM not supported (mapper not implemented).\n");
			break;
		case -ENOMEM:
			log_err("Could not allocate memory.\n");
			break;
		case -EINVAL:
			log_err("Invalid ROM format.\n");
			break;
		default:
			perror("Error: ");
		}
		return EXIT_FAILURE;
	}
	show_rom_info(cartridge->rom, cartridge->chr_ram);
	show_control_info();

	/* Run custom platform procedures (if any) */
	platform_start();

	/* Initialize semaphore */
	if (s_init(&wait_gui, 0) < 0) {
		log_err("Cannot initialize semaphore!\n");
		cartridge_unload(cartridge);
		return EXIT_FAILURE;
	}
	log_info("Starting emulation...\n");

	/* Initialize modules */
	sbus_init();
	sbus_set_cartridge(cartridge);
	cpu_init();
	ppu_init();
	apu_init();

	/* Register callbacks */
	cpu_register_kill_cb(cpu_kill_cb);
	if (debug) {
		cpu_register_debug_cb(cpu_debug_cb);
		sbus_register_debug_cb(sbus_debug_cb);
	}

	/* Reset system */
	controller_reset();
	ppu_set_cartridge(cartridge);
	cpu_reset();
	ppu_reset();
	apu_reset();

	/* Create and initialize GUI thread */
	pthread_attr_init(&attr_gui);
	pthread_create(&th_gui, &attr_gui, &gui_th_loop, &scale);

	/* Wait GUI to get ready */
	s_lock(&wait_gui);

	/* Create and initialize emulation thread */
	pthread_attr_init(&attr_emu);
	pthread_create(&th_emu, &attr_emu, &emu_th_loop, NULL);

	/* Wait for return (we only care with GUI) */
	log_info("Emulation is running...\n");
	pthread_join(th_gui, NULL);

	/* Finalize modules */
	log_info("Finalizing Emulation...\n");

	ppu_stop();
	ppu_destroy();
	cpu_destroy();
	apu_destroy();
	sbus_destroy();
	cartridge_unload(cartridge);
	gui_destroy();
	s_destroy(&wait_gui);

	platform_end();

	log_info("Done.\n");
	return EXIT_SUCCESS;
}
