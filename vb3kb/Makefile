PREFIX ?= /usr/local
OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only

bindir = $(PREFIX)/bin
sharedir = $(PREFIX)/share/setBfree

TCLFILE ?= $(sharedir)/vb3kb.tcl

OBJS=vb3kb.o midi_jack.o

LOADLIBES=-lm -lpthread
CFLAGS = $(OPTIMIZATIONS) -Wall
CFLAGS+=-DVKB_TCLFILE=\"$(TCLFILE)\"
CFLAGS+=-DVERSION="\"$(VERSION)\""

TCLPREFIX=/usr /usr/local
TCLLIBDIR=lib64 lib

$(foreach tprefix,$(TCLPREFIX), \
  $(foreach tlibdir,$(TCLLIBDIR), \
    $(if $(shell test -f $(tprefix)/$(tlibdir)/tclConfig.sh -a $(tprefix)/$(tlibdir)/tkConfig.sh && echo yes), \
		  $(eval TCLTKPREFIX=$(tprefix))\
		  $(eval TCLTKLIBDIR=$(tlibdir))\
		)\
  )\
)

ifeq ($(TCLTKPREFIX),)
  # TODO make GUI optional...
  $(error "TCL/TK is required - install tk-dev and tcl-dev")
endif

CFLAGS+=-I$(TCLTKPREFIX)/include/tcl`. $(TCLTKPREFIX)/tclConfig.sh && echo \$$TCL_VERSION`
LOADLIBES+=-ltcl`. $(TCLTKPREFIX)/$(TCLTKLIBDIR)/tclConfig.sh && echo \$$TCL_VERSION`
LOADLIBES+=-ltk`. $(TCLTKPREFIX)/$(TCLTKLIBDIR)/tkConfig.sh && echo \$$TK_VERSION`
LOADLIBES+=`. $(TCLTKPREFIX)/$(TCLTKLIBDIR)/tkConfig.sh && echo $$TK_LIBS`

ifeq ($(shell pkg-config --exists alsa && echo yes), yes)
  CFLAGS+=-DHAVE_ASEQ `pkg-config --cflags alsa`
  LOADLIBES+=`pkg-config --libs alsa`
  OBJS+=midi_alsa.o
else
  $(info "compliling w/o ALSA Sequencer, JACK-midi is alwasy on")
endif

CFLAGS+=`pkg-config --cflags jack`
LOADLIBES+=`pkg-config --libs jack`

all: vb3kb

vb3kb: $(OBJS)

clean:
	rm -f *.o vb3kb

install: vb3kb
	install -d $(DESTDIR)$(bindir)
	install -m755 vb3kb $(DESTDIR)$(bindir)
	install -d $(DESTDIR)$(sharedir)
	install -m644 vb3kb.tcl $(DESTDIR)$(sharedir)

uninstall:
	rm -f $(DESTDIR)$(bindir)/vb3kb
	rm -f $(DESTDIR)$(sharedir)/vb3kb.tcl
	-rmdir $(DESTDIR)$(bindir)
	-rmdir $(DESTDIR)$(sharedir)

.PHONY: clean all install uninstall
