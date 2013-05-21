include ../common.mak

IRPATH ?= $(sharedir)/ir

override CXXFLAGS += -DIRPATH=\"$(IRPATH)\"
LOADLIBES = -lm

# nothing to compile here. ../src will call make convolution.o
targets=

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
