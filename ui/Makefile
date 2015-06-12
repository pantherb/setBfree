include ../common.mak
TX=../b_synth/textures/
WINDRES=$(XWIN)-windres

override CFLAGS+= -I../src -I../b_overdrive -I../b_reverb -I../b_whirl -I../b_synth/
override CFLAGS+=`pkg-config --cflags lv2` -DLV2SYNTH -DREQUIRE_UI

JACKEXTRA=
OSXJACKWRAP=

ifeq ($(IS_OSX), yes)
  OSXJACKWRAP=$(RW)jackwrap.mm
  USEWEAKJACK=1
  CC=$(CXX)
else
  ifeq ($(IS_WIN), yes)
    CC=$(CXX)
    UIDEPS+=win_icon.rc.o
    JACKEXTRA+=win_icon.rc.o
    USEWEAKJACK=1
    override CFLAGS+=-mwindows
  else
    LOADLIBES=-lrt
    UILIBS+=-ldl
    endif
    ifneq ($(ENABLE_CONVOLUTION), yes)
      override CFLAGS+=-std=c99
    endif
  endif
endif

ifeq ($(USEWEAKJACK),1)
  UICFLAGS+=-DUSE_WEAK_JACK
  JACKEXTRA+=$(RW)weakjack/weak_libjack.c
else
  UILIBS+=`pkg-config --libs jack`
endif

targets=

DESC=$(abspath desc.h)

ifeq ($(LV2AVAIL)$(HAVE_UI), yesyes)
  targets+=setBfreeUI$(EXE_EXT)
else
  $(warning standalone JACK/GL-GUI will not be built)
endif

all: $(targets)

setBfreeUI$(EXE_EXT): $(RW)jackwrap.c $(LV2OBJ) $(OSXJACKWRAP) $(RW)weakjack/weak_libjack.def $(RW)weakjack/weak_libjack.h $(JACKEXTRA) \
	../b_synth/lv2.c ../b_synth/uris.h \
	../b_synth/ui.c $(UIDEPS) \
	../src/cfgParser.h ../src/midi.h
	$(MAKE) -C ../b_synth $(FONTFILE)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UICFLAGS) $(LIC_CFLAGS) \
		-pthread \
		-DXTERNAL_UI -DJACK_AUTOCONNECT=15 \
		-DAPPNAME="\"setBfree\"" \
		-DJACK_DESCRIPT=\"$(DESC)\" \
		-o setBfreeUI$(EXE_EXT) \
		$(RW)jackwrap.c $(OSXJACKWRAP) $(JACKEXTRA) \
		$(LV2OBJ) ../b_synth/lv2.c ../b_synth/ui.c \
		$(LDFLAGS) $(UILIBS) $(LOADLIBES) $(LIC_LOADLIBES) \
		-lm

# windows icon
.SUFFFIXES: .rc
win_icon.rc.o: win_icon.rc ../img/setbfree.ico
	$(WINDRES) -o $@ $<

install: $(targets)
ifneq ($(targets),)
	install -d $(DESTDIR)$(bindir)
	install -m755 setBfreeUI$(EXE_EXT) $(DESTDIR)$(bindir)
endif

uninstall:
	rm -f $(DESTDIR)$(bindir)/setBfreeUI$(EXE_EXT)

clean:
	rm -f setBfreeUI$(EXE_EXT)
	rm -f win_icon.rc.o

.PHONY: clean all install uninstall
