#!/bin/sh

WORKSPACE=$PWD/cross-compile
DIST=$WORKSPACE/dist

check_host() {
	if [ -z "$(which x86_64-w64-mingw32-gcc)" -o -z "$(which x86_64-w64-mingw32-g++)" ]; then
		echo "mingw toolchain not found."
		echo "Install the mingw toolchain and try it again."
		echo "For debian family distros, just run:"
		echo "    sudo apt install -y gcc-mingw-w64 g++-mingw-w64"
		exit 1
	fi
}

fetch_all() {
	git clone https://github.com/libsdl-org/SDL -b SDL2
	git clone https://github.com/libsdl-org/SDL_ttf.git -b SDL2
	cd SDL_ttf/external
	./download.sh
	cd -
}

configure_SDL2() {
	cd SDL/
	mkdir -p build
	cd build
	../configure --host=x86_64-w64-mingw32 --prefix=$DIST
	cd ../..
}

compile_SDL2() {
	cd SDL/build
	make -j$(nproc)
	cd -
}

install_SDL2() {
	cd SDL/build
	make -j$(nproc) install
	cd -
}

configure_SDL_ttf() {
	cd SDL_ttf
	autoreconf -i
	mkdir -p build
	cd build
	../configure --host=x86_64-w64-mingw32 --prefix=$DIST --with-sdl-prefix=$DIST --enable-harfbuzz-builtin --enable-freetype-builtin
	cd ../..
}

compile_SDL_ttf() {
	cd SDL_ttf/build
	make -j$(nproc)
	cd -
}

install_SDL_ttf() {
	cd SDL_ttf/build
	make -j$(nproc) install
	cd -
}

# Check host toolchain
check_host

# Working directory
mkdir -p $WORKSPACE
cd $WORKSPACE

# Final output with all libraries
mkdir -p $DIST

# Fetch all sources
fetch_all

# Build SDL2
configure_SDL2 && \
compile_SDL2 && \
install_SDL2

# Build SDL_ttf
configure_SDL_ttf && \
compile_SDL_ttf && \
install_SDL_ttf

