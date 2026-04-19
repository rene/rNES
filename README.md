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


# Developing rNES

Developers are really encouraged to develop and contribute to rNES. For
detailed information and porting guide, consult the [Developer Documentation](/docs/README.md).

Source code documentation can be generated with Doxygen:

```sh
cd src
make docs
```

The generated documentation will be available in different formats at `src/docs/dist`.

