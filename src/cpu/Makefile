#############################################################
CC = gcc

LDFLAGS = -lpthread -lrt
CFLAGS = -Wall -DCPU_TEST_CODE -Iinclude/

executable = cpu6502
sources = cpu650x.c
#############################################################

##
# General rules
#

objects = $(sources:.c=.o)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

$(executable) : $(objects)
	$(CC) $(objects) $(LDFLAGS) -o $(executable)

.Makefile.dep: *.c
	@$(CC) $(CFLAGS) $(CPP_FLAGS) -MM *.c > $@

-include .Makefile.dep

##
# clean
#
.PHONY clean:
	@rm -f *.o \#* *~  .Makefile.dep
	@rm -f $(executable)

