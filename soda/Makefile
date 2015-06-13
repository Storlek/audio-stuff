CC := gcc

CFLAGS := -g -Wall -Wextra -Wno-unused

LDFLAGS := -lm

AO_CFLAGS :=
AO_LDFLAGS := -lao

OBJECTS := soda.o waveforms.o readmidi.o

SFILES := $(wildcard examples/*.s)
SBFILES := $(patsubst %.s,%.sb,$(SFILES))

CFLAGS += $(AO_CFLAGS)

.PHONY: all examples clean
all: soda readmidi examples

clean:
	$(RM) soda readmidi *.o *~ core $(SBFILES)

soda: soda.o waveforms.o
	$(CC) -o soda soda.o waveforms.o $(CFLAGS) $(LDFLAGS) $(AO_LDFLAGS)
readmidi: readmidi.o
	$(CC) -o readmidi readmidi.o $(CFLAGS) $(LDFLAGS)

examples: $(SBFILES)
%.sb: %.s
	./sasm $< $@
