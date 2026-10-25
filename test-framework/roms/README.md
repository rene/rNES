# CI ROMs

Drop **license-clean** `.nes` files here for the automated ROM tests that run
in GitHub CI, and list them in [`../roms-ci.txt`](../roms-ci.txt).

> ⚠️ **Do not commit commercial ROMs.** Only add ROMs that are legally
> redistributable, for example:
> - Homebrew games released under a permissive/open license.
> - Public-domain NES test ROMs (e.g. from the NESdev community) whose license
>   allows redistribution — check each one.

For each ROM you add:

1. Copy the `.nes` file into this directory.
2. Add its path (relative to `test-framework/`, e.g. `roms/mygame.nes`) to
   `../roms-ci.txt`.
3. Regenerate the baseline so CI knows the expected result:
   ```sh
   make -C test-framework
   python3 test-framework/run_tests.py \
       --rom-list test-framework/roms-ci.txt \
       --baseline test-framework/baseline.json --update-baseline
   ```
4. Commit the ROM, the updated `roms-ci.txt`, and `baseline.json`.

While this directory is empty, the CI job is a green no-op.

Your **private / commercial** ROM collection is never committed — run those
locally against your own list file:

```sh
python3 test-framework/run_tests.py --rom-list ~/my-roms.txt
```

## `external/` — the downloaded test ROMs

CI also runs blargg's NES test suites and kevtris's **nestest**, which check
the emulator against documented hardware behaviour rather than just looking at
the screen. Those ROMs live in `external/`, are **not** committed, and are
git-ignored. Fetch them with:

```sh
make -C test-framework fetch-testroms      # or just: make -C test-framework testroms
```

which downloads every file listed in [`../testroms.json`](../testroms.json)
from [christopherpow/nes-test-roms](https://github.com/christopherpow/nes-test-roms)
at a **pinned commit** and verifies each against a recorded SHA-256.

### Why they are downloaded instead of committed

rNES is BSD-3-Clause, and everything in this repository has to be compatible
with that. These ROMs are not:

- **blargg's NES test ROMs** (Shay Green) ship a `readme.txt` describing the
  test and nothing else. There is no copyright notice, no licence, and no
  statement of permission to redistribute.
- **ppu_read_buffer** (Joel Yliluoma, "Bisqwit") is the same: a long
  `readme.txt` and source, no licence.
- **nestest** (Kevin Horton) is the same: `nestest.txt` documents the failure
  codes and says nothing about licensing.
- The upstream archive that hosts them all has no repository-wide `LICENSE` file
  either; it is a community mirror, not a relicensing.

Both are freely downloadable and are the standard way to validate an NES
emulator, so *running* them in CI is uncontroversial. **Redistributing** them
from a BSD-3-Clause repository is a different act, and one nobody has given
permission for. Fetching at test time keeps rNES's own licensing clean: the
repository ships a URL, a commit hash and a checksum — never the bytes.

The pinned commit and the per-file SHA-256 are what make this reproducible: a
force-push upstream, a replaced file or a truncated download fails the fetch
instead of quietly changing a verdict.

### Adding another test ROM

Only add ROMs that are downloadable from a **GitHub** repository, and check
what the licence actually permits before deciding whether to fetch it (like
these) or commit it (like a permissively-licensed homebrew ROM).

1. Add an entry to `../testroms.json` with its upstream `path`, the `dest` it
   should land under, its `author`, `size` and `sha256`.
2. List it in [`../testroms-ci.txt`](../testroms-ci.txt), with per-ROM options
   after `" | "` if it needs any.
3. Regenerate the baseline and commit it:
   ```sh
   make -C test-framework testroms-baseline
   ```
