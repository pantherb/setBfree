PREFIX ?= /usr/local
OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only
ENABLE_CONVOLUTION ?= no

sharedir = $(PREFIX)/share/setBfree

IRPATH ?= $(sharedir)/ir

CXXFLAGS = $(OPTIMIZATIONS) -Wall
CXXFLAGS+= -DIRPATH=\"$(IRPATH)\"
LOADLIBES = -lm

# nothing to compile here. ../src will call make convolution.o
targets=
#targets=convolution.o

all: $(targets)

%.o: %.h ../src/cfgParser.h ../src/midi.h

clean:
	rm -f *.o

install:
ifeq ($(ENABLE_CONVOLUTION), yes)
	install -d $(DESTDIR)$(sharedir)/ir
	-install -m644 ir/*.wav $(DESTDIR)$(sharedir)/ir
endif

uninstall:
	-rmdir $(DESTDIR)$(sharedir)/ir
	-rmdir $(DESTDIR)$(sharedir)

.PHONY: clean all install uninstall
