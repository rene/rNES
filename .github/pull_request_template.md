# Description

Provide a clear and concise description of the changes in this PR and
explain why they are necessary.

Reference any related issue (e.g. `Closes #123`).

rNES aims for accuracy to real hardware, a low memory footprint and high
portability. If this change trades one of those for another, say so here.

## How to test and validate this PR

Tell the reviewer how to reproduce your results, and what you verified yourself:

- **Build**: `cd src && make`, plus any new dependency or build step this PR
  introduces.
- **Games**: which games you played and what to look at. Include at least one
  that exercises the code you touched and one that does not, to catch
  regressions. Identify games by title — **do not attach a ROM**.
- **Test ROMs**: for CPU, PPU, APU or mapper changes, which public accuracy
  test ROMs you ran (nestest, blargg's suites, ...) and their results before
  and after the change.
- **Platform**: OS and version, compiler version, SDL2 version, and whether it
  was a native build or the MinGW-w64 cross-build.
- **Evidence**: a screenshot or short video for graphical changes; the relevant
  `./rNES -i <rom_file>` output for ROM decoding and mapper changes.

For accuracy fixes, cite the hardware documentation that backs the new
behaviour — a [NESdev Wiki](https://www.nesdev.org/wiki/) page, a datasheet, or
the test ROM that fails without the fix.

## Checklist

- [ ] I've provided a proper description
- [ ] All my commits are signed off (`git commit -s`)
- [ ] If a model helped write this, every commit it contributed to names it in a `Co-Authored-By:` line
- [ ] I've added the proper documentation
- [ ] No copyrighted ROMs are attached or committed
- [ ] I've tested my PR on Linux
- [ ] I've tested my PR on Windows (optional)

See [CONTRIBUTING.md](https://github.com/rene/rNES/blob/main/CONTRIBUTING.md)
for the details behind each item.
