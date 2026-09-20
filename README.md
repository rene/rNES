![rNES logo](/docs/imgs/rNES.png)
# rNES - Yet another Nintendo Entertainment System emulator

rNES it's yet another NES emulator which has as it main goals:

- Accuracy to real hardware
- Low memory footprint
- High portability across platforms

Currently rNES implements mappers 0, 1, 2, 3, 4 and 7, supporting thousands
of NES games.

![rNES](/docs/imgs/rNES_games.png)

# How to build

## POSIX systems

Currently only an interface using lib SDL is implemented. On a Debian family system, make sure to install the following packages:

- libsdl2-dev
- libsdl2-ttf-dev

rNES can be built by calling *make* from *src* directory:

```sh
cd src
make
```

To run rNES just call:

```sh
./rNES <rom_file>
```

The following options are supported:

```text
Use: ./rNES [-s <scale_factor>] [-d] [-h] <rom_file>

    -s, --scale      Set screen scale factor (default: 2)
    -d, --debug      Show debug messages (it can be extremely slow!)
    -i, --info       Just show ROM information (don't play the game)
    -v, --version    Show version
    -l, --license    Show license
    -h, --help       Show this help
```

## Windows (cross-compile)

A static version for Windows can be cross-compiled on Linux using the MinGW-w64 toolchain.

The script *build-win32-cross.sh* builds the cross-compiler environment
with *SDL2* and *SDL2_ttf* libraries.

First, ensure you have the MinGW-w64 toolchain installed (*x86_64-w64-mingw32-gcc* and
*x86_64-w64-mingw32-g++* must be available on the host system).

On Debian the following packages must be installed: *gcc-mingw-w64* and
*g++-mingw-w64*:

```sh
sudo apt install -y gcc-mingw-w64 g++-mingw-w64
```

Run the *build-win32-cross.sh* script to create the build environment:

```sh
cd src
./scripts/build-win32-cross.sh
```

Once the environment is ready, rNES.exe can be built using *make*:

```sh
make -f Makefile.mingw
```

# Tools

## Palette Editor

rNES provides an easy to use PalleteEditor that can be found in [tools/PaletteEditor](./tools/PaletteEditor). The tool be built with *cmake* (Qt6 is required):

```sh
cd tools/PaletteEditor/
cmake .
make
./PaletteEditor
```

The tool is straightforward to use, you can create a new palette or load an existent one (.pal file, no headers, just a sequence of RGB color data for all 64 entries). Just click on the squares to change the color.

![PaletteEditor](/docs/imgs/PaletteEditor.png)

*PaletteEditor interface.*


# Testing

rNES comes with a headless ROM test framework, located at
[test-framework](./test-framework), which runs a batch of NES ROMs without a
display or a sound card and flags the obviously broken ones: crashes, hangs,
blank or frozen screens and CPU jams.

First, build the headless harness (it links the emulator core against a null
backend, so no SDL is needed):

```sh
make -C test-framework
```

Then run it over a list of ROM paths (one per line, *#* comments allowed):

```sh
python3 test-framework/run_tests.py --rom-list ~/my-roms.txt
```

Each ROM gets a **PASS**, **FAIL** or **SKIP** verdict (SKIP means the mapper
isn't implemented yet). A single ROM can also be inspected directly with the
harness, which prints a JSON report with per-frame metrics:

```sh
./test-framework/rnes_headless --frames 300 <rom_file> | python3 -m json.tool
```

Results can be compared against a committed baseline, so that only regressions
(PASS to FAIL) are reported. This is what CI runs on every pull request:

```sh
python3 test-framework/run_tests.py --rom-list test-framework/roms-ci.txt \
    --baseline test-framework/baseline.json
```

Note that no commercial ROM is (or should ever be) committed to the repository,
so the list used by CI only contains license-clean ROMs. Point the framework at
your own local collection when testing a change.

See the [test framework documentation](./test-framework/README.md) for the
complete list of options, the failure heuristics and the CI setup.

# Developing rNES

Developers are really encouraged to develop and contribute to rNES. For
detailed information and porting guide, consult the [Developer Documentation](/docs/README.md).

Source code documentation can be generated with Doxygen:

```sh
cd src
make docs
```

The generated documentation will be available in different formats at `src/docs/dist`.


# Contributing

Contributions of all kinds are welcome: bug reports, accuracy fixes, new
mappers, ports to new platforms and documentation.

Please read [CONTRIBUTING.md](./CONTRIBUTING.md) before opening an issue or a
pull request. It covers:

- How to report a game-specific bug in a way that can be acted on.
- The coding style (enforced by clang-format: `cd src && make format-src`) and
  the source file headers.
- Commit conventions, including the mandatory `Signed-off-by:` line
  (`git commit -s`) and how to disclose AI assisted code.
- What CI checks on every pull request, and how to test your change.
- Step-by-step guides for adding a new mapper and for porting rNES to a new
  platform.

Everyone participating in this project is expected to follow the
[Code of Conduct](./CODE_OF_CONDUCT.md). Security issues should **not** be
reported in public issues, see [SECURITY.md](./SECURITY.md) for the private
reporting process.
