CFLAGS   += $(shell pkg-config --cflags sdl2) -I./include/
LDFLAGS  += $(shell pkg-config --libs sdl2) -lSDL2_ttf

sources = gui.c audio.c clock.c

objects = $(sources:.c=.o)

all: $(objects)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

.Makefile.dep: *.c
	@$(CC) $(CFLAGS) -MM *.c > $@

-include .Makefile.dep

##
# clean
#
.PHONY clean:
	@rm -f *.o \#* *~  .Makefile.dep
	@rm -f $(executable)

