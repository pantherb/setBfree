include ../common.mak

ifeq ($(TCLTKPREFIX),)
  $(error "TCL/TK is not available - install tk-dev and tcl-dev to build the GUI")
endif

TCLFILE ?= $(sharedir)/vb3kb.tcl

OBJS=vb3kb.o midi_jack.o

LOADLIBES=-lm -lpthread
override CFLAGS+=-DVKB_TCLFILE=\"$(TCLFILE)\"

override CFLAGS+=-I$(TCLTKPREFIX)/include/tcl`. $(TCLTKPREFIX)/$(TCLTKLIBDIR)/tclConfig.sh && echo \$$TCL_VERSION`
LOADLIBES+=-ltcl`. $(TCLTKPREFIX)/$(TCLTKLIBDIR)/tclConfig.sh && echo \$$TCL_VERSION`
LOADLIBES+=-ltk`. $(TCLTKPREFIX)/$(TCLTKLIBDIR)/tkConfig.sh && echo \$$TK_VERSION`
LOADLIBES+=`. $(TCLTKPREFIX)/$(TCLTKLIBDIR)/tkConfig.sh && echo $$TK_LIBS`

ifeq ($(shell pkg-config --exists alsa && echo yes), yes)
  override CFLAGS+=-DHAVE_ASEQ `pkg-config --cflags alsa`
  LOADLIBES+=`pkg-config --libs alsa`
  OBJS+=midi_alsa.o
else
  $(info "compiling w/o ALSA Sequencer, JACK-midi is always on")
endif

override CFLAGS+=`pkg-config --cflags jack`
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
