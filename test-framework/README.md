# rNES ROM Test Framework

Automated smoke-testing for [rNES](../README.md): run a batch of NES ROMs and
flag the obviously-broken ones — crashes, hangs, blank (black/gray/white)
screens, frozen screens, and CPU jams / infinite loops — without a display,
sound card, or human. Designed to run locally over a large ROM set **and** in
GitHub CI.

## How it works

Two pieces:

1. **`rnes_headless`** — a small C program that links the rNES emulator *core*
   (CPU, PPU, APU, mappers, …) together with an SDL-free "null" backend
   ([`harness/null_hal.c`](harness/null_hal.c)). It runs one ROM for a fixed
   number of frames *as fast as possible*, capturing the video into RAM and
   discarding audio, then prints a one-line JSON report with per-frame metrics.
   Needs only `gcc`/`make` — no SDL, no X server.

2. **`rnestest`** — a pure-stdlib Python package that reads a ROM list, skips
   ROMs whose mapper rNES doesn't implement, runs the rest through the harness
   (in parallel, with a timeout), judges each **PASS / FAIL / SKIP**, and
   compares against a committed baseline so CI only reacts to *regressions*.

```
 roms-ci.txt ─► rnestest ─► parse header ─► mapper implemented?
                               │                   │ no ─► SKIP
                               │ yes
                               ▼
                        rnes_headless <rom>  ──► JSON metrics
                               │
                               ▼
                    heuristics ─► PASS / FAIL / SKIP
                               │
                               ▼
                    compare vs baseline.json ─► regressions? ─► exit 1
```

## Building

```sh
make -C test-framework      # produces test-framework/rnes_headless
```

### Extra checking

Two optional targets keep the test infrastructure held to the same standard
as the code it tests. Both are off the default path so a 790-ROM sweep stays
fast.

```sh
make -C test-framework strict     # compile harness/ with -Wall -Wextra -Werror
make -C test-framework sanitize   # rebuild the harness under ASan + UBSan
```

`strict` is syntax-only and takes under a second, so CI runs it on every
push. It is scoped to `harness/` deliberately: the emulator core still has
warnings of its own (`-Wmaybe-uninitialized` in `ppu.c`), and failing this
check on those would make it useless as a gate on new code.

`sanitize` produces the same binary with the same CLI, roughly 2–3x slower.
It is worth pointing at a handful of ROMs — especially after a mapper or
timing change — to catch memory and UB errors that a PASS/FAIL verdict
cannot see:

```sh
make -C test-framework sanitize
./test-framework/rnes_headless --frames 600 some-rom.nes
```

Remember to `make -C test-framework` again afterwards to get the fast build
back before a full sweep.

## Running locally

Point it at any list of ROM paths (one per line; `#` comments allowed). This is
the same format as the repo's `roms.txt`:

```sh
# Test your full private collection
python3 test-framework/run_tests.py --rom-list ~/my-roms.txt

# Fewer/more frames, more parallelism, no baseline
python3 test-framework/run_tests.py --rom-list ~/my-roms.txt \
    --frames 300 --jobs 8
```

Relative ROM paths resolve against the list file's directory (or `--roms DIR`).
Absolute paths are used as-is.

### Inspect a single ROM

The harness is useful on its own for debugging:

```sh
./test-framework/rnes_headless --frames 300 "path/to/game.nes" | python3 -m json.tool
```

## Controller input

Many games sit on a static title/menu screen — and often a *chain* of them
(title → menu → game), each needing **Start** or **A** — so a naive headless
run would wrongly flag them as "frozen". To avoid that, the harness mashes
controller input by default: it cycles through the requested buttons
(`start,a` by default), pressing each for a few frames then releasing it (a
clean edge every time), so both buttons get pressed several times a second and
reliably walk through multi-screen intros. Only one button is held at a time,
so the SELECT+START soft-reset combo can never happen. The input travels the
exact same path a real keypress does (`gui_read_joypad` → `controller.c`), so
games react to it just as they would to a player.

A working game therefore advances into gameplay/attract mode and its screen
animates → **PASS**. A genuinely hung game ignores the input and stays static →
**FAIL**.

- `--buttons start,a,select` — customize which buttons are mashed (choose from
  `start,a,b,select,up,down,left,right`). Add `select` for games with a
  mode-select screen; add a direction for games that need one.
- `--no-input` — disable input entirely.
- `--frames 600` — give games with unusually long intros more time to reach
  animating content.

## What counts as a failure

Applied to the *post-warmup* samples; a signal only fails a ROM if it holds for
the **whole** window (so a screen that animates even once, or a CPU that ever
escapes a loop, passes).

| Verdict reason | How it's detected |
|----------------|-------------------|
| `crashed (signal N)` | harness killed by a signal (e.g. SIGSEGV) |
| `hang: wall-clock timeout` | ROM didn't finish within `--timeout` |
| `emulator hang (no video output)` | PPU never produced a vblank |
| `CPU jammed (JAM/KIL opcode)` | the CPU executed an illegal jam opcode |
| `blank/solid screen (#RRGGBB, N% drawn)` | ≤ `--blank-colors` colors, or less than `--min-content-frac` of the screen drawn, every sample — after running on to `--blank-frames` to confirm (this is the black/gray/white "freeze") |
| `CPU stuck in tight loop` | ≤ `--loop-max-pcs` distinct executed PCs/frame, every sample |
| `frozen screen (no change for N frames)` | **opt-in** (`--detect-freeze`): a *colourful* framebuffer that never changes |

`SKIP` covers unsupported mappers, non-NES files, and missing files.

> **Why `frozen screen` is opt-in.** A *blank* screen (black/gray/white) that
> is still blank once it has been given time to draw (see below) is a reliable
> failure and is always checked. A *colourful*
> static screen is **not** — lots of working games hold a title/menu screen for
> well over five seconds before it animates or attracts. So failing on
> "colourful screen didn't change" produces false positives and is off by
> default.
>
> When you do enable `--detect-freeze`, the harness runs a still-static screen
> **longer** (up to `--freeze-frames`, default ~30 s) to confirm: a working
> slow-title game animates given time and passes; only a screen that *never*
> changes is failed. The extra time is spent only on ROMs that look stuck, so
> most ROMs stay fast.

> **How `blank` is measured.** By *content coverage* — the share of pixels
> differing from the dominant (background) colour — not by how many distinct
> colours are on screen. Counting colours is a bad proxy: copyright cards,
> story text and "PRESS START" screens are white-on-black, i.e. exactly two
> colours, yet perfectly healthy. Wolverine's intro draws ~5.7% of the screen
> in two colours; Cabal, Rampart, Mega Man 3 and ~25 others are the same
> shape. A screen only counts as blank when essentially *nothing was drawn*
> on top of the background.

> **Why a blank screen also gets extra time.** Five seconds of black is not
> proof of death: some intros simply take longer to draw their first pixel
> (*Maniac Mansion* holds a black screen until frame ~320, just past the
> default 300-frame window). So a ROM that is still blank at the end of the
> base run keeps going up to `--blank-frames` (~15 s) and stops the moment
> anything is drawn. The extension is granted only while the CPU is still
> executing varied code — a ROM sitting in a tight loop is already dead and
> fails immediately, so the extra time is spent on a handful of ROMs at most.

## Determinism

A baseline is only worth having if the same ROM produces the same verdict
every time, so the harness pins everything that would otherwise vary between
runs.

The one that actually bites is power-on RAM. `sbus_init()` gets the 2 KiB of
CPU RAM from `malloc()` and never initialises it, so the emulator starts with
whatever the heap happens to hold. Games that read a location before writing
it then take different paths from run to run — Joust swung between 1.3M and
5.7M instructions over the same 300 frames and flipped PASS/FAIL between two
otherwise identical sweeps. The harness therefore fills CPU RAM with a fixed
byte (`0x00`) before reset.

That is a harness policy, not a claim about hardware: a real NES powers on
with arbitrary RAM, and the emulator proper is left alone. To find out whether
a title genuinely depends on its power-on state, re-run it under a different
fill:

```sh
./run_tests.py --rom-list roms.txt --ram-fill 0xff
```

A ROM whose verdict changes with `--ram-fill` is reading uninitialised RAM;
that is worth knowing, and is a property of the game (or of a mapper bug), not
noise in the test suite. The value used is recorded as `ram_fill` in the JSON
report, so a result always says which power-on state produced it.

## Baseline & regression gating

The baseline (`baseline.json`) records the expected verdict per ROM. CI fails
only when a ROM goes **PASS → FAIL** (a regression); it also reports
**FAIL → PASS** (fixes) and ROMs not yet in the baseline.

```sh
# Create/refresh the baseline from the current behavior
python3 test-framework/run_tests.py --rom-list test-framework/roms-ci.txt \
    --baseline test-framework/baseline.json --update-baseline

# Check against it (exit 1 on regressions)
python3 test-framework/run_tests.py --rom-list test-framework/roms-ci.txt \
    --baseline test-framework/baseline.json
```

Use `--fail-on-new` to also fail on ROMs that fail but aren't in the baseline.

## CI

[`.github/workflows/rom-tests.yml`](../.github/workflows/rom-tests.yml) builds
the harness and runs [`roms-ci.txt`](roms-ci.txt) against `baseline.json` on
every push/PR, emitting GitHub annotations + a step summary and uploading
`results.json` / `results.xml` (JUnit) artifacts.

Commercial ROMs can't be committed, so `roms/` ships empty (CI is a green no-op)
until you add **license-clean** ROMs — see [`roms/README.md`](roms/README.md).

## Useful options

```
--frames N          frames to emulate per ROM (default 300 ≈ 5 s NTSC)
--warmup N          frames to ignore at the start (default 60)
--interval N        sample the screen/CPU every N frames (default 10)
--timeout S         per-ROM wall-clock timeout, seconds (default 30)
--jobs N            ROMs to run in parallel (default: CPU count)
--buttons LIST      buttons to mash (default: start,a; e.g. start,a,select)
--no-input          don't inject any controller input (input is on by default)
--blank-colors N    distinct-color threshold for a flat screen (default 1)
--min-content-frac F  share of non-background pixels a screen needs to count
                    as drawn (default 0.001 = 0.1%)
--loop-max-pcs N    distinct-PC/frame threshold for "tight loop" (default 8)
--detect-freeze     also fail colourful screens that never change (opt-in)
--freeze-frames N   with --detect-freeze, run a static screen up to N frames
                    to confirm before failing (default 1800 ~= 30s)
--blank-frames N    run a still-blank screen up to N frames to confirm nothing
                    is ever drawn (default 900 ~= 15s; 0 = off)
--json FILE         write full results as JSON
--junit FILE        write JUnit XML
--github            emit GitHub Actions annotations + step summary
```

## Tests

```sh
python3 -m unittest discover -s test-framework/tests
```

## Layout

```
test-framework/
├── harness/            # C: null_hal.{c,h} + rnes_headless.c
├── rnestest/           # Python package (romheader, mappers, runner,
│                       #   analyze, baseline, report, __main__)
├── run_tests.py        # CLI wrapper
├── roms/               # license-clean CI ROMs (empty by default)
├── roms-ci.txt         # ROM list used by CI
├── baseline.json       # expected verdicts
├── tests/              # unit tests
└── Makefile            # builds rnes_headless (no SDL)
```
