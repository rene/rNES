/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rNES test framework - headless runner
 * Copyright 2021-2026 Renê de Souza Pinto
 *
 * Runs a single ROM on the rNES core with no SDL/GUI/audio device, for a
 * fixed number of frames, and prints a JSON report describing what happened:
 * framebuffer metrics (per sampled frame), CPU health (distinct executed PCs,
 * JAM flag), and PPU state (rendering / NMI enabled). The Python test
 * framework consumes this JSON to decide PASS / FAIL / SKIP.
 *
 * The emulation is driven exactly like src/interface/posix/rNES.c, minus the
 * GUI thread and the realtime (audio-callback) throttle: we call sbus_clock()
 * in a tight loop and detect frame boundaries from the PPU vblank flag.
 */
#include "apu.h"
#include "cartridge.h"
#include "controller.h"
#include "cpu650x.h"
#include "hal/hal.h"
#include "mappers/mapper.h"
#include "ppu.h"
#include "romdec.h"
#include "sbus.h"
#include "null_hal.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Exit codes (consumed by the Python runner) */
#define EXIT_OK 0
#define EXIT_USAGE 2
#define EXIT_SKIPPED 3	 /* unsupported mapper */
#define EXIT_INVALID 4	 /* not a valid ROM */
#define EXIT_LOADERR 5	 /* file / memory error */

/* Defaults (overridable on the command line) */
#define DEF_FRAMES 300	 /* ~5 s of NTSC video */
#define DEF_WARMUP 60	 /* skip the first ~1 s (boot / PPU warm-up) */
#define DEF_INTERVAL 10	 /* sample the screen/CPU every N frames */
#define DEF_BUTTONS "start,a" /* buttons to mash to advance title/menu screens */

/*
 * Byte written over the 2 KiB of CPU RAM before reset.
 *
 * sbus_init() obtains cpu_ram from malloc() and leaves it uninitialized, so
 * without this the emulator powers on with whatever the heap happened to
 * hold. Real hardware is indeed arbitrary at power-on, but a regression
 * suite is not allowed to be: games that read RAM before writing it take
 * different paths per run. Joust, for one, swung between 1.3M and 5.7M
 * instructions over 300 frames and flipped PASS/FAIL between identical
 * sweeps. Filling with a fixed byte makes a verdict reproducible, which is
 * the whole point of a baseline. --ram-fill overrides it, so a title can be
 * re-run under a different power-on state to see if it is sensitive to one.
 */
#define DEF_RAM_FILL 0x00
#define DEF_BLANK_FRAMES 900 /* ~15 s: how long a blank screen gets to draw */
#define INPUT_SLOT 4	 /* frames per press / per release slot */
#define MAX_BUTTONS 8

#define FB_W NULL_SCREEN_WIDTH
#define FB_H NULL_SCREEN_HEIGHT
#define FB_PIXELS (FB_W * FB_H)

#define MAX_SAMPLES 4096

/* ------------------------------------------------------------------ */
/* CPU instrumentation (updated from the CPU debug/kill callbacks)     */
/* ------------------------------------------------------------------ */

static uint8_t pc_seen[8192];		 /* 1 bit per 16-bit PC address */
static unsigned long instr_count;	 /* total instructions executed */
static uint16_t last_pc;			 /* most recently executed PC */
static int cpu_jammed;				 /* set by the CPU kill (JAM) callback */

static void debug_cb(cpu650x_state_t st)
{
	uint16_t pc = st.CPU.PC;
	pc_seen[pc >> 3] |= (uint8_t)(1u << (pc & 7));
	last_pc = pc;
	instr_count++;
}

static void kill_cb(cpu650x_state_t st)
{
	(void)st;
	cpu_jammed = 1;
}

static int popcount_pc_seen(void)
{
	int i, n = 0;
	for (i = 0; i < (int)sizeof(pc_seen); i++)
		n += __builtin_popcount(pc_seen[i]);
	return n;
}

/* ------------------------------------------------------------------ */
/* Per-frame framebuffer analysis                                     */
/* ------------------------------------------------------------------ */

struct frame_sample {
	int frame;
	uint64_t hash;
	int distinct_colors;
	uint32_t dominant_color;   /* 0x00RRGGBB */
	double dominant_frac;
	int distinct_pcs;
	unsigned long instructions;
	uint16_t pc;
	int render_enabled;
	int nmi_enabled;
};

/* Mirrors the verdict thresholds in rnestest/analyze.py: a screen is blank
 * when it is a single flat colour, or when next to nothing is drawn over the
 * background. Used here only to decide how long to keep running -- the
 * PASS/FAIL call itself stays in rnestest. */
#define BLANK_MAX_COLORS 1
#define BLANK_MIN_CONTENT 0.001

static int sample_is_blank(const struct frame_sample *s)
{
	return s->distinct_colors <= BLANK_MAX_COLORS ||
		   (1.0 - s->dominant_frac) < BLANK_MIN_CONTENT;
}

/* Open-addressing color histogram. NES output has well under 512 distinct
 * colors per frame, so a 4096-slot table never gets crowded.
 *
 * ht_used is the sole validity bit: a slot's color and count are meaningless
 * -- and are never read -- unless ht_used[idx] is set. That is why clearing
 * ht_used alone is enough to reset the table between frames; ht_color and
 * ht_count are always written before the bit is set. Do not "also clear" them
 * for tidiness: it would cost a memset per frame and hide a stale-read bug
 * behind plausible-looking zeroes instead of letting it show up.
 */
#define HT_SIZE 4096
static uint32_t ht_color[HT_SIZE];
static uint32_t ht_count[HT_SIZE];
static uint8_t ht_used[HT_SIZE];

static void analyze_framebuffer(const uint32_t *fb, uint64_t *out_hash,
								int *out_distinct, uint32_t *out_dom_color,
								double *out_dom_frac)
{
	const uint64_t FNV_PRIME = 1099511628211ULL;
	uint64_t hash = 1469598103934665603ULL;
	int distinct = 0;
	uint32_t max_count = 0, dom_color = 0;
	int i;

	memset(ht_used, 0, sizeof(ht_used));

	for (i = 0; i < FB_PIXELS; i++) {
		uint32_t c = fb[i] & 0x00ffffffu; /* drop constant alpha */
		uint32_t idx = (c * 2654435761u) & (HT_SIZE - 1);

		/* hash (order-independent-ish mix, fine for change detection) */
		hash ^= c;
		hash *= FNV_PRIME;

		/* insert / bump count with linear probing */
		while (ht_used[idx] && ht_color[idx] != c)
			idx = (idx + 1) & (HT_SIZE - 1);
		if (!ht_used[idx]) {
			ht_used[idx] = 1;
			ht_color[idx] = c;
			ht_count[idx] = 1;
			distinct++;
		} else {
			ht_count[idx]++;
		}
		if (ht_count[idx] > max_count) {
			max_count = ht_count[idx];
			dom_color = c;
		}
	}

	*out_hash = hash;
	*out_distinct = distinct;
	*out_dom_color = dom_color;
	*out_dom_frac = (double)max_count / (double)FB_PIXELS;
}

/* ------------------------------------------------------------------ */
/* Frame boundary detection                                           */
/* ------------------------------------------------------------------ */

/* Advance the emulation by exactly one PPU frame, detected as the rising
 * edge of the PPU vblank flag (set once per frame at scanline 241). Returns
 * the number of sbus_clock() calls consumed, or 0 if the cap was hit without
 * a frame completing (treated as a hang). */
static uint64_t run_one_frame(uint64_t tick_cap)
{
	uint8_t prev = ppu.PPUSTATUS.reg.vblank;
	uint64_t ticks = 0;

	while (ticks < tick_cap) {
		uint8_t cur;
		sbus_clock();
		ticks++;
		cur = ppu.PPUSTATUS.reg.vblank;
		if (cur && !prev)
			return ticks; /* vblank rising edge -> frame complete */
		prev = cur;
	}
	return 0; /* no frame within the cap */
}

/* ------------------------------------------------------------------ */
/* JSON output helpers                                                */
/* ------------------------------------------------------------------ */

static void json_puts_escaped(const char *s)
{
	putchar('"');
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		switch (c) {
		case '"':
			fputs("\\\"", stdout);
			break;
		case '\\':
			fputs("\\\\", stdout);
			break;
		case '\n':
			fputs("\\n", stdout);
			break;
		case '\r':
			fputs("\\r", stdout);
			break;
		case '\t':
			fputs("\\t", stdout);
			break;
		default:
			if (c < 0x20)
				printf("\\u%04x", c);
			else
				putchar(c);
		}
	}
	putchar('"');
}

static const char *video_str(rom_video_t v)
{
	switch (v) {
	case ROM_NTSC:
		return "NTSC";
	case ROM_PAL_M:
		return "PAL";
	case ROM_MULTIPLE:
		return "MULTIPLE";
	case ROM_DENDY:
		return "DENDY";
	default:
		return "UNKNOWN";
	}
}

/* Emit a short JSON object for the early-exit paths (skip / invalid / error) */
static void emit_status(const char *rom, const char *status, const char *reason,
						int mapper)
{
	printf("{\"status\":");
	json_puts_escaped(status);
	printf(",\"rom\":");
	json_puts_escaped(rom);
	printf(",\"reason\":");
	json_puts_escaped(reason);
	if (mapper >= 0)
		printf(",\"mapper\":%d", mapper);
	printf("}\n");
	fflush(stdout);
}

static void usage(const char *argv0)
{
	fprintf(stderr,
			"Usage: %s [--frames N] [--warmup N] [--interval N] "
			"[--freeze-frames N] [--blank-frames N] [--buttons list] "
			"[--ram-fill BYTE] [--no-input] [--json] <rom_file>\n"
			"  --freeze-frames N  if the screen stays static, keep running up "
			"to N frames\n"
			"                     to confirm it is really frozen (0 = off)\n"
			"  --blank-frames N   if the screen is still blank, keep running "
			"up to N frames\n"
			"                     to confirm nothing is ever drawn "
			"(default %d, 0 = off)\n"
			"  --buttons list     comma list from "
			"start,a,b,select,up,down,left,right (default: %s)\n"
			"  --ram-fill BYTE    power-on value for the 2 KiB of CPU RAM; keeps "
			"runs\n"
			"                     reproducible (default 0x%02x)\n",
			argv0, DEF_BLANK_FRAMES, DEF_BUTTONS, DEF_RAM_FILL);
}

/* Map a button name to its raw controller_reg_t byte (0 if unknown). */
static uint8_t button_mask(const char *name)
{
	controller_reg_t cr;
	cr.raw = 0;
	if (!strcmp(name, "start"))
		cr.reg.start = 1;
	else if (!strcmp(name, "a") || !strcmp(name, "A"))
		cr.reg.A = 1;
	else if (!strcmp(name, "b") || !strcmp(name, "B"))
		cr.reg.B = 1;
	else if (!strcmp(name, "select"))
		cr.reg.select = 1;
	else if (!strcmp(name, "up"))
		cr.reg.up = 1;
	else if (!strcmp(name, "down"))
		cr.reg.down = 1;
	else if (!strcmp(name, "left"))
		cr.reg.left = 1;
	else if (!strcmp(name, "right"))
		cr.reg.right = 1;
	return cr.raw;
}

/*
 * Parse "start,a,select" into an array of raw button masks. Returns the
 * number of masks written, or -1 if the list cannot be honoured exactly:
 * too long for the local buffer, holding an unknown name, or naming more
 * buttons than `max`. Every one of those would otherwise degrade into a
 * silently different input script (a truncated "right" -> "righ" is simply
 * dropped), so the caller turns them into a usage error instead.
 */
static int parse_buttons(const char *list, uint8_t *masks, int max)
{
	char buf[64];
	char *save;
	const char *tok;
	int n = 0;

	if (snprintf(buf, sizeof(buf), "%s", list) >= (int)sizeof(buf))
		return -1; /* truncated: would split a name mid-token */
	for (tok = strtok_r(buf, ",", &save); tok;
		 tok = strtok_r(NULL, ",", &save)) {
		uint8_t m = button_mask(tok);
		if (m == 0 || n >= max)
			return -1;
		masks[n++] = m;
	}
	return n;
}

/*
 * Scripted controller input: an aggressive "menu masher". Many games walk
 * through several screens (title -> menu -> game), each needing START or A,
 * so we cycle through the requested buttons pressing each one for INPUT_SLOT
 * frames then releasing it for INPUT_SLOT frames (a clean edge every time).
 * With the default {START, A} that mashes both ~7 times a second, reliably
 * advancing past multi-screen intros. Only one button is ever held at once,
 * so the SELECT+START soft-reset combo can never occur.
 *
 * Returns the raw controller byte to present this frame.
 */
static uint8_t sched_buttons(int frame, const uint8_t *masks, int n)
{
	int unit, idx;

	if (n <= 0)
		return 0;
	unit = frame / INPUT_SLOT;
	idx = unit % (2 * n);		 /* even slot = press, odd slot = release */
	return (idx % 2 == 0) ? masks[idx / 2] : 0;
}

/* Put the 2 KiB of CPU RAM into a known state. sbus_write() is the only
 * public route to it; everything below 0x2000 mirrors into cpu_ram, so the
 * first 0x800 addresses cover the lot. */
static void fill_cpu_ram(uint8_t value)
{
	uint16_t a;

	for (a = 0; a < 0x0800; a++)
		sbus_write(a, value);
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
	static struct frame_sample samples[MAX_SAMPLES];
	int total_frames = DEF_FRAMES;
	int warmup = DEF_WARMUP;
	int interval = DEF_INTERVAL;
	int inject_input = 1;
	const char *rom_path = NULL;
	int i, nsamples = 0;
	int mapper, implemented, rendering_ever = 0;
	uint64_t tick_cap;
	unsigned long prev_instr = 0;
	int frames_run = 0;
	int status_ok = 1;
	int freeze_frames = 0;	/* >total_frames enables adaptive freeze confirm */
	int blank_frames = DEF_BLANK_FRAMES; /* >total_frames: blank confirm */
	int limit, screen_changed = 0, have_base_hash = 0;
	int content_seen = 0, blank_extended = 0;
	uint64_t base_hash = 0;
	const char *buttons_arg = DEF_BUTTONS;
	int ram_fill = DEF_RAM_FILL;
	uint8_t btn_masks[MAX_BUTTONS];
	int n_buttons = 0;
	rom_t *rom = NULL;
	cartridge_t *cart = NULL;
	rom_video_t video;
	int ret;

	/* Parse command line */
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
			total_frames = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--warmup") && i + 1 < argc) {
			warmup = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--interval") && i + 1 < argc) {
			interval = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--freeze-frames") && i + 1 < argc) {
			freeze_frames = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--blank-frames") && i + 1 < argc) {
			blank_frames = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--buttons") && i + 1 < argc) {
			buttons_arg = argv[++i];
		} else if (!strcmp(argv[i], "--ram-fill") && i + 1 < argc) {
			ram_fill = (int)strtol(argv[++i], NULL, 0) & 0xff;
		} else if (!strcmp(argv[i], "--no-input")) {
			inject_input = 0;
		} else if (!strcmp(argv[i], "--json")) {
			/* JSON is always emitted; accepted for clarity */
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage(argv[0]);
			return EXIT_OK;
		} else if (argv[i][0] == '-') {
			usage(argv[0]);
			return EXIT_USAGE;
		} else {
			rom_path = argv[i];
		}
	}
	if (rom_path == NULL) {
		usage(argv[0]);
		return EXIT_USAGE;
	}
	if (total_frames < 1)
		total_frames = 1;
	if (interval < 1)
		interval = 1;
	if (warmup < 0)
		warmup = 0;

	/* Validate --buttons up front: a list we cannot honour exactly is a
	 * usage error, not a silently different (or empty) input script. */
	n_buttons = parse_buttons(buttons_arg, btn_masks, MAX_BUTTONS);
	if (n_buttons < 0) {
		fprintf(stderr, "%s: bad --buttons list: \"%s\"\n", argv[0],
				buttons_arg);
		usage(argv[0]);
		return EXIT_USAGE;
	}
	if (n_buttons == 0)
		inject_input = 0; /* an empty list: nothing to press */

	/* Load the ROM header first: read the mapper and gate on it *before*
	 * cartridge_load() (which does an unbounded mappers[] index). */
	ret = load_rom(rom_path, &rom);
	if (ret < 0) {
		if (ret == -EINVAL)
			emit_status(rom_path, "invalid", "not_a_valid_rom", -1);
		else
			emit_status(rom_path, "error", strerror(-ret), -1);
		return ret == -EINVAL ? EXIT_INVALID : EXIT_LOADERR;
	}
	mapper = (int)rom->mapper;
	video = get_rom_video_std(rom);
	implemented = (mapper < NUM_MAPPERS && mappers[mapper] != NULL);
	if (!implemented) {
		emit_status(rom_path, "skipped", "unsupported_mapper", mapper);
		unload_rom(rom);
		return EXIT_SKIPPED;
	}
	unload_rom(rom); /* cartridge_load() re-parses the file itself */
	rom = NULL;

	/* Load the cartridge (mirrors src/interface/posix/rNES.c) */
	ret = cartridge_load(rom_path, &cart);
	if (ret < 0) {
		if (ret == -ENOTSUP)
			emit_status(rom_path, "skipped", "unsupported_mapper", mapper);
		else if (ret == -EINVAL)
			emit_status(rom_path, "invalid", "not_a_valid_rom", mapper);
		else
			emit_status(rom_path, "error", strerror(-ret), mapper);
		return ret == -ENOTSUP ? EXIT_SKIPPED
			 : ret == -EINVAL  ? EXIT_INVALID
							   : EXIT_LOADERR;
	}

	/* Initialize modules (same order as the real interface) */
	sbus_init();
	sbus_set_cartridge(cart);
	fill_cpu_ram((uint8_t)ram_fill); /* deterministic power-on RAM */
	cpu_init();
	ppu_init();
	apu_init();

	/* Instrument the CPU */
	cpu_register_kill_cb(kill_cb);
	cpu_register_debug_cb(debug_cb);

	/* Reset system */
	controller_reset();
	ppu_set_cartridge(cart);
	cpu_reset();
	ppu_reset();
	apu_reset();

	/* Start discarding audio so apu_clock()'s rbuff_put() never blocks */
	if (null_audio_start_drain() < 0) {
		emit_status(rom_path, "error", "audio_drain_thread", mapper);
		return EXIT_LOADERR;
	}

	/* A generous per-frame tick cap: an NTSC frame is ~89342 PPU cycles.
	 * If we blow way past that without seeing vblank, the emulator itself
	 * is stuck (not the game) -> report a hang. */
	tick_cap = 400000ULL;

	/* The loop runs `total_frames` (the base observation), but may grow to
	 * `freeze_frames` when the screen looks statically stuck: many working
	 * games hold a colourful title screen for well over 5 s, so we keep
	 * running to see whether it ever animates before calling it "frozen".
	 *
	 * Both adaptive extensions below raise `limit` under a `> limit` guard, so
	 * it is monotonic -- neither can shorten a window the other opened, and
	 * they compose in either order. (They are in fact mutually exclusive on a
	 * given sample: the freeze extension requires a non-blank frame, which has
	 * already set content_seen, which is exactly what the blank extension
	 * requires to be unset. The guard is what makes that a safety net rather
	 * than a load-bearing assumption.) */
	limit = total_frames;

	for (frames_run = 0; frames_run < limit; frames_run++) {
		/* Reset the per-frame PC coverage bitset */
		memset(pc_seen, 0, sizeof(pc_seen));

		/* Present this frame's scripted controller input (if enabled) */
		if (inject_input)
			null_set_joypad(sched_buttons(frames_run, btn_masks, n_buttons));

		uint64_t frame_ticks = run_one_frame(tick_cap);
		if (frame_ticks == 0) {
			/* Emulator-level hang: no vblank within the cap. `break` skips
			 * the loop's own increment, so bump frames_run by hand to turn
			 * the 0-based index of the frame we just attempted into the
			 * 1-based count the JSON report expects. */
			status_ok = 0;
			frames_run++;
			break;
		}

		if (ppu.PPUMASK.reg.show_background || ppu.PPUMASK.reg.show_sprites)
			rendering_ever = 1;

		/* Sample after warm-up, every `interval` frames */
		if (frames_run + 1 > warmup && ((frames_run + 1) % interval) == 0 &&
			nsamples < MAX_SAMPLES) {
			struct frame_sample *s = &samples[nsamples++];
			s->frame = frames_run + 1;
			analyze_framebuffer(null_framebuffer(), &s->hash,
								&s->distinct_colors, &s->dominant_color,
								&s->dominant_frac);
			s->distinct_pcs = popcount_pc_seen();
			s->instructions = instr_count - prev_instr;
			s->pc = last_pc;
			s->render_enabled =
				(ppu.PPUMASK.reg.show_background || ppu.PPUMASK.reg.show_sprites)
					? 1
					: 0;
			s->nmi_enabled = ppu.PPUCTRL.reg.nmi ? 1 : 0;

			/* Track whether the framebuffer ever changes across samples */
			if (!have_base_hash) {
				base_hash = s->hash;
				have_base_hash = 1;
			} else if (s->hash != base_hash) {
				screen_changed = 1;
			}

			/* ...and whether anything was ever actually drawn. A blank
			 * screen can still change hash (black -> grey), so this is a
			 * separate question from `screen_changed`. */
			if (!sample_is_blank(s))
				content_seen = 1;

			/* Adaptive freeze confirmation: at the end of the base run, if the
			 * screen is still static but non-blank and the CPU is busy (a
			 * tight loop already fails fast; a blank screen is handled by the
			 * check below), extend the run up to freeze_frames to give a slow
			 * title/menu screen time to animate. */
			if (frames_run + 1 >= total_frames && !screen_changed &&
				freeze_frames > limit && s->distinct_colors > 2 &&
				s->dominant_frac < 0.99 && s->distinct_pcs > 8) {
				limit = freeze_frames;
			}

			/* Adaptive blank confirmation: a game that holds a black screen
			 * through the whole base window looks exactly like a dead one --
			 * but some intros simply take longer than ~5 s to draw their
			 * first pixel. As long as the CPU is still doing varied work
			 * (a tight loop fails on its own), keep running to find out. */
			if (frames_run + 1 >= total_frames && !content_seen &&
				blank_frames > limit && s->distinct_pcs > 8) {
				limit = blank_frames;
				blank_extended = 1;
			}
		}
		prev_instr = instr_count;

		/* Once past the base run, stop as soon as the question that kept us
		 * running is answered: the screen animates, and -- when we extended
		 * to wait out a blank screen -- something has actually been drawn. */
		if (frames_run + 1 >= total_frames && screen_changed &&
			(!blank_extended || content_seen))
			break;
	}

	/* -------------------------------------------------------------- */
	/* Emit the full JSON report                                      */
	/* -------------------------------------------------------------- */
	printf("{");
	printf("\"status\":\"%s\",", status_ok ? "ok" : "hang");
	printf("\"rom\":");
	json_puts_escaped(rom_path);
	printf(",\"mapper\":%d", mapper);
	printf(",\"mapper_implemented\":true");
	printf(",\"video\":\"%s\"", video_str(video));
	printf(",\"frames_requested\":%d", total_frames);
	printf(",\"frames_run\":%d", frames_run);
	printf(",\"screen_changed\":%s", screen_changed ? "true" : "false");
	printf(",\"warmup\":%d", warmup);
	printf(",\"interval\":%d", interval);
	printf(",\"ram_fill\":%d", ram_fill);
	printf(",\"input_injected\":%s", inject_input ? "true" : "false");
	if (inject_input) {
		printf(",\"input_buttons\":");
		json_puts_escaped(buttons_arg);
	}
	printf(",\"cpu_jammed\":%s", cpu_jammed ? "true" : "false");
	printf(",\"total_instructions\":%lu", instr_count);
	printf(",\"rendering_ever_enabled\":%s", rendering_ever ? "true" : "false");
	printf(",\"samples\":[");
	for (i = 0; i < nsamples; i++) {
		struct frame_sample *s = &samples[i];
		printf("%s{", i ? "," : "");
		printf("\"frame\":%d", s->frame);
		printf(",\"frame_hash\":\"%016llx\"", (unsigned long long)s->hash);
		printf(",\"distinct_colors\":%d", s->distinct_colors);
		printf(",\"dominant_color\":\"%06x\"", s->dominant_color & 0xffffffu);
		printf(",\"dominant_frac\":%.5f", s->dominant_frac);
		printf(",\"distinct_pcs\":%d", s->distinct_pcs);
		printf(",\"instructions\":%lu", s->instructions);
		printf(",\"pc\":\"%04x\"", s->pc);
		printf(",\"render_enabled\":%s", s->render_enabled ? "true" : "false");
		printf(",\"nmi_enabled\":%s", s->nmi_enabled ? "true" : "false");
		printf("}");
	}
	printf("]}\n");

	/* The audio drain thread is blocked in rbuff_get(); don't try to join
	 * it. Flush stdio and terminate immediately. */
	fflush(stdout);
	_exit(EXIT_OK);
}
