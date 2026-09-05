# Contributing to rNES

Thanks for your interest in rNES! Contributions of all kinds are welcome:
bug reports, accuracy fixes, new mappers, ports to new platforms, and
documentation.

rNES has three goals, and they shape how patches are reviewed:

1. **Accuracy to real hardware**
2. **Low memory footprint**
3. **High portability across platforms**

A change that improves one of these without hurting the others is an easy
"yes". A change that trades one away needs to say so explicitly in the pull
request description.

Everyone participating in this project is expected to follow the
[Code of Conduct](CODE_OF_CONDUCT.md).


## Table of Contents

- [Reporting bugs](#reporting-bugs)
- [Getting the source and building](#getting-the-source-and-building)
- [Repository layout](#repository-layout)
- [Coding style](#coding-style)
- [Source file headers](#source-file-headers)
- [Documenting code](#documenting-code)
- [Commit messages and sign-off](#commit-messages-and-sign-off)
- [AI assisted code](#ai-assisted-code)
- [Pull requests](#pull-requests)
- [Adding a new mapper](#adding-a-new-mapper)
- [Porting rNES to a new platform](#porting-rnes-to-a-new-platform)
- [The CPU core](#the-cpu-core)
- [ROMs and copyright](#roms-and-copyright)
- [Security issues](#security-issues)
- [License of contributions](#license-of-contributions)


## Reporting bugs

Open an issue at https://github.com/rene/rNES/issues.

Most rNES bugs are game-specific accuracy problems, and those are very hard to
act on without details. Please include:

- **The game**, and the output of `./rNES -i <rom_file>` (mapper number, PRG/CHR
  sizes, mirroring). Do **not** attach the ROM itself — see
  [ROMs and copyright](#roms-and-copyright).
- **What happens vs. what should happen**: hangs on the title screen, garbled
  sprites, wrong audio pitch, crash, etc. A screenshot or short video helps a
  lot for graphical issues.
- **Where it happens**: how far into the game, and whether it is reproducible
  from a fresh start.
- **Platform**: OS and version, compiler version, SDL2 version, and whether it
  was a native build or the MinGW-w64 cross-build.
- **The commit** you built from (`git rev-parse --short HEAD`).
- Anything useful from `./rNES -d <rom_file>` (debug output — note that it is
  extremely slow).

If a test ROM from a public accuracy suite (nestest, blargg's tests, etc.)
reproduces the problem, say which one and which sub-test fails. Those are the
most valuable reports, because they are freely redistributable and easy to
verify.

If it crashes, a build with `-fsanitize=address,undefined -g` and the resulting
report is worth more than any description.


## Getting the source and building

```sh
git clone https://github.com/rene/rNES.git
cd rNES/src
make
./rNES <rom_file>
```

On a Debian-family system the build needs `libsdl2-dev` and `libsdl2-ttf-dev`.
See the [README](README.md) for the Windows cross-compile setup and the
PaletteEditor tool.

Useful `make` targets, all run from `src/`:

| Target | What it does |
|---|---|
| `make` | Build rNES for POSIX systems using SDL |
| `make format-src` | Check coding style with clang-format (dry run, no files touched) |
| `make docs` | Generate Doxygen documentation into `src/docs/dist` |
| `make clean` | Remove build artifacts |
| `make help` | List the targets |

Please run `make format-src` before opening a pull request — CI runs the same
check and will fail on style violations.


## Repository layout

| Path | Contents |
|---|---|
| `src/` | Emulator core and build system |
| `src/cpu/` | 6502 core, imported from [MOS650x](https://github.com/rene/MOS650x) |
| `src/mappers/` | Cartridge mappers (`m0_NROM.c`, `m1_SxROM.c`, …) |
| `src/include/` | Public headers for the core modules |
| `src/hal/` | Hardware abstraction layer (file I/O) |
| `src/gui/sdl/` | SDL2 video, audio and clock backend |
| `src/interface/posix/` | POSIX entry point (`rNES.c`) |
| `docs/` | Developer documentation |
| `tools/PaletteEditor/` | Qt6 palette editor |

The [Developer Documentation](docs/README.md) explains the three-layer
architecture (core / HAL / interface), the individual modules, and the thread
model. Read [docs/core-modules.md](docs/core-modules.md) before touching the
CPU, PPU, APU or SBUS.


## Coding style

Style is enforced mechanically by [`src/.clang-format`](src/.clang-format), so
there is nothing to argue about: run the formatter.

```sh
cd src
make format-src                       # check everything (what CI runs)
clang-format -i path/to/file.c        # format a specific file
```

The short version of the configuration: LLVM base style, **tabs** for
indentation with a width of 4, Linux brace placement, 80-column limit, case
labels not indented, and no single-line `if` bodies.

CI uses clang-format 21. Older versions occasionally disagree on edge cases; if
CI flags a file you already formatted, that is usually why.

Beyond formatting:

- **Portable C.** The build does not pin a `-std=`, so the compiler default
  applies; stick to widely supported standard C and avoid compiler extensions.
  The core must not depend on POSIX, SDL, or any host facility — those belong
  in `hal/`, `gui/` and `interface/`.
- Keep the core allocation-light and predictable. rNES targets constrained
  systems; avoid allocations in per-cycle paths.
- Check every `malloc`/`calloc` result, and free everything you allocate in the
  matching `finalize` path.
- The build must stay warning-free under `-Wall`, and `cppcheck` must report no
  errors — CI runs `cppcheck --error-exitcode=1` over the whole tree.
- Prefer fixed-width types (`uint8_t`, `uint16_t`) for anything modelling
  hardware registers or memory.

When you fix hardware behaviour, cite your source in a comment — a
[NESdev Wiki](https://www.nesdev.org/wiki/) page, a test ROM, or a datasheet.
Existing modules do this (see the reference list at the top of `src/ppu.c`),
and it makes accuracy changes reviewable.


## Source file headers

Every new source file starts with the SPDX identifier and the BSD 3-Clause
notice used across the tree, followed by a Doxygen `@file` block:

```c
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rNES - Nintendo Entertainment System Emulator
 * Copyright 2021-2026 Renê de Souza Pinto
 *
 * ... full BSD 3-Clause text, copied verbatim from an existing file ...
 */
/**
 * @file m5_ExROM.c
 *
 * Short description of what this module implements.
 */
```

Copy the block from an existing file such as `src/mappers/m0_NROM.c` rather than
retyping it.


## Documenting code

Public functions and structures are documented with Doxygen comments, in the
style already used throughout the code:

```c
/**
 * Generic mapper initialization function
 * @param [in,out] m Mapper
 * @param [in,out] c Cartridge
 * @return int 0 on success, error code otherwise
 */
int mapper_init(struct _mapper_t *m, cartridge_t *c)
```

Run `make docs` to check that your comments generate cleanly.


## Commit messages and sign-off

rNES tries to always use the following commit convention:

```
subsystem: Short imperative summary

Optional body explaining what the change does and, more importantly, why.
Wrap at 72 columns. Reference the hardware behaviour or the test ROM that
motivated the change.

Signed-off-by: Your Name <your@email.com>
```

The `subsystem:` prefix is the module or file being changed, as it appears in
the tree. Real examples from the history:

```
ppu.c: Mask palette index
mappers: Implement M1 board variants
romdec: Fix NES 2.0 PRG/CHR size and mapper decoding
github/workflows: Add cpp-check workflow
```

Guidelines:

- One logical change per commit. Formatting churn goes in its own commit,
  separate from behaviour changes.
- Use the imperative mood ("Fix sprite Y evaluation", not "Fixed" or "Fixes").
- **Every commit must be signed off.** Use `git commit -s`, which appends the
  `Signed-off-by:` line. This certifies that you wrote the patch or otherwise
  have the right to submit it under the project's license — the
  [Developer Certificate of Origin](https://developercertificate.org/).


## AI assisted code

rNES was written in @rene's spare time, before the AI explosion, and therefore
without any AI assistance. That is history, not a rule: **AI assisted code is
acceptable and encouraged in rNES today.**

The one requirement is transparency. If you used an AI model to help write a
change, say so clearly by adding a `Co-Authored-By:` line naming the model to
**every** commit it contributed to:

```
ppu.c: Fix sprite zero hit timing

Sprite zero hit was being flagged one cycle early, which made ... .
Verified against blargg's sprite_hit_tests.

Signed-off-by: Your Name <your@email.com>
Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
```

Name the specific model you actually used, not just the vendor or the tool —
`Claude Opus 5`, `Claude Sonnet 5`, and so on — so the history records what was
involved. If more than one model contributed, add one line per model.

A few things do not change when a model is in the loop:

- **You are the author.** Your `Signed-off-by:` still applies, with everything
  it certifies under the Developer Certificate of Origin. The
  `Co-Authored-By:` line documents assistance; it does not transfer
  responsibility.
- **You must understand the code you submit** and be able to explain why it is
  correct during review. "The model wrote it" is not an answer to a review
  question.
- **The same bar applies.** Formatting, warning-free builds, cppcheck, the
  commit conventions above, and real testing against games or test ROMs.
- **Accuracy claims need evidence, not confidence.** Models are good at
  producing plausible NES hardware behaviour that is subtly wrong. Cite the
  NESdev page or the test ROM that backs the change, and verify it yourself
  before sending it.


## Pull requests

1. Fork the repository and create a branch off `main`.
2. Make your change, format it (`make format-src`), and build it.
3. Test with real games — including at least one game that uses the code path
   you touched, and one that does not, to catch regressions. If you changed the
   CPU, PPU or APU, run whatever public accuracy test ROMs apply.
4. Push and open a pull request against `main`.

The pull request description should say what changed, why, which games or test
ROMs you verified it against, and on which platform. For accuracy fixes,
mention the hardware documentation you followed.

Three CI checks run on every pull request and must pass:

- **Build rNES (posix + SDL)** — `make` on Ubuntu 24.04
- **clang-format Check** — style over `src/`, excluding `src/cpu/`
- **cppcheck** — static analysis, failing on any error

Keep pull requests focused. A series of small, reviewable commits is much
easier to merge than one large commit touching several subsystems.


## Adding a new mapper

New mappers are the most useful contribution to rNES, since each one unlocks
another set of games. rNES currently implements mappers 0, 1, 2, 3, 4 and 7.

A mapper is a `mapper_t` (see `src/include/mappers/mapper.h`) providing:

- `init` / `reset` / `finalize` — lifecycle; allocate your private state in
  `init`, release *everything* in `finalize`
- `prg_mem_handler`, `chr_mem_handler`, `vram_mem_handler`,
  `palette_mem_handler` — memory callbacks, each receiving a `CMEM_READ` or
  `CMEM_WRITE` operation, an address, and a value
- `data` — an opaque pointer to your mapper's private state

To add mapper *N*:

1. Create `src/mappers/mN_BOARD.c` (e.g. `m5_ExROM.c`), following the naming and
   structure of `src/mappers/m2_UxROM.c` — it is the smallest complete example.
2. Define your `mapper_t` instance, declare it `extern` in
   `src/mappers/mappers.c`, and place it at index *N* of the `mappers[]` vector,
   keeping the trailing `/* N */` comments aligned.
3. Add the new file to `sources` in `src/Makefile` **and** `src/Makefile.mingw`.
4. If a handler is generic (mirroring and palette handling often are), reuse the
   exported `m0_*` handlers instead of copying them.
5. Test with several games from that board family, ideally from different
   sub-variants, and list them in the pull request.

Document the board variants your implementation covers and the ones it does
not — a partial mapper is fine and welcome, as long as the limits are stated.


## Porting rNES to a new platform

Portability is a core goal, and ports are welcome. The
[Porting Guide](docs/porting-guide.md) is a step-by-step walkthrough: the HAL
functions to implement, the module init/destroy order, and the entry point.
[docs/thread-model.md](docs/thread-model.md) explains the audio-driven
synchronization you will need to reproduce (or replace, on platforms without
threads).

New platform code goes in `gui/<platform>/`, `hal/` and
`interface/<platform>/`. Do not add platform `#ifdef`s to the core modules.


## The CPU core

`src/cpu/` is imported from the separate
[MOS650x](https://github.com/rene/MOS650x) project and is excluded from the
clang-format check for that reason. Please send CPU fixes upstream to MOS650x;
they are then synced into rNES with a `cpu: Update cpu650x emulator code`
commit. If a CPU bug only manifests in rNES, open an issue here and we will
sort out where it belongs.


## ROMs and copyright

**Do not attach, commit, or link to copyrighted game ROMs**, and do not include
them in test fixtures. Issues or pull requests containing them will be edited or
closed.

Freely redistributable homebrew and public accuracy test ROMs (nestest, blargg's
test suites, and similar) are fine to reference by name and link. When reporting
a game-specific bug, identify the game by title and the `-i` output instead.


## Security issues

Please do not report security vulnerabilities in public issues. See
[SECURITY.md](SECURITY.md) for the private reporting process.


## License of contributions

rNES is released under the [BSD 3-Clause License](LICENSE). By contributing —
and by signing off your commits under the Developer Certificate of Origin — you
agree that your contributions are licensed under the same terms.
