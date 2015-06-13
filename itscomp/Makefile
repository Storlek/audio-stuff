RM := rm -f
CC := gcc
CFLAGS := -g -Os -Wall -Wextra -Wno-unused
LDFLAGS := -lm

.PHONY: all clean
all: itscomp itsex
clean:
	rm -f itscomp itsex *.o *~ core
itscomp: itscomp.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
itsex: itsex.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

