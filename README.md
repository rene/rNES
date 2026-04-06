![rNES logo](/docs/imgs/rNES.png)
# rNES - Yet another Nintendo Entertainment System emulator

rNES it's yet another NES emulator which has as it main goals:

- Accuracy to real hardware
- Low memory footprint
- High portability across platforms

Currently rNES implements mappers 0, 1, 2, 3 and 7, supporting thousands
of NES games.

![Fig. 1: rNES](/docs/imgs/rNES_games.png)

*Fig. 1: rNES*

# How to build

Currently only a posix system with lib SDL is supported. On a Debian family system, make sure to install the following packages:

- libsdl2
- libsdl2-ttf

rNES can be built by calling *make*:

```sh
cd src
make
```

To run rNES just call:

```sh
./rNES <rom_file>
```

The following options are supported:

```sh
Use: ./rNES [-s <scale_factor>] [-d] [-h] <rom_file>

    -s, --scale      Set screen scale factor (default: 2)
    -d, --debug      Show debug messages (it can be extremely slow!)
    -i, --info       Just show ROM information (don't play the game)
    -v, --version    Show version
    -l, --license    Show license
    -h, --help       Show this help
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

![Fig. 2: PalleteEditor](/docs/imgs/PaletteEditor.png)

*Fig. 2: PalleteEditor interface.*

