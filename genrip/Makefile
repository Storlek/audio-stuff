CC := gcc
RM := rm -f
CFLAGS := -std=gnu99 -g -Wall -Werror -Wextra -Wno-unused
LDFLAGS :=


.PHONY: all clean

all: genrip
clean:
	$(RM) -f genrip *.o *~ core
genrip: genrip.o
	$(CC) $(LDFLAGS) -o $@ $^

