DMD := dmd
DFLAGS := -g

.PHONY: all clean
all: gym2it
clean:
	$(RM) gym2it *.o *~ core

SRCFILES := gym2it.d itwriter.d

OBJFILES := $(patsubst %.d,%.o,$(SRCFILES))

%.o: %.d
	$(DMD) $(DFLAGS) -c -of$@ $<
gym2it: $(OBJFILES)
	$(DMD) $(DFLAGS) -of$@ $^

$(OBJFILES): Makefile
