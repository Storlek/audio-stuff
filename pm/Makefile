# -*- Makefile -*-

SHELL := /bin/sh
CC := gcc
RM := rm -f

CFLAGS := -Wall -W -Iinclude

#CFLAGS += -O3 -fomit-frame-pointer -funroll-loops -frerun-cse-after-loop -fno-strength-reduce
CFLAGS += -g3

all: pm
clean:
	$(RM) core.* pm *.o *~ fmt/*.o fmt/*~
pm: file.o player.o mixer.o misc.o tables.o main.o itsex.o \
fmt/xm.o fmt/669.o fmt/imf.o fmt/it.o fmt/mod.o fmt/mtm.o fmt/s3m.o fmt/sfx.o \
pmlink.o
	$(CC) -o $@ $^ -lao -lm

# ----------------------------------------------------------------------------

file.o: file.c include/pm.h Makefile
itsex.o: itsex.c Makefile
main.o: main.c include/pm.h Makefile
misc.o: misc.c include/pm.h Makefile
mixer.o: mixer.c include/pm.h Makefile
player.o: player.c include/pm.h Makefile
tables.o: tables.c include/pm.h Makefile

fmt/xm.o: fmt/xm.c include/pm.h Makefile
fmt/669.o: fmt/669.c include/pm.h Makefile
fmt/imf.o: fmt/imf.c include/pm.h Makefile
fmt/it.o: fmt/it.c include/pm.h Makefile
fmt/mod.o: fmt/mod.c include/pm.h Makefile
fmt/mtm.o: fmt/mtm.c include/pm.h Makefile
fmt/s3m.o: fmt/s3m.c include/pm.h Makefile
fmt/sfx.o: fmt/sfx.c include/pm.h Makefile
