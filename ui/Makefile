include ../common.mak
TX=../b_synth/textures/

override CFLAGS+= -I../src -I../b_overdrive -I../b_reverb -I../b_whirl -I../b_synth/
override CFLAGS+=`pkg-config --cflags lv2` -DLV2SYNTH

ifeq ($(IS_OSX), yes)
  JACKWRAP=jackwrap.c jackwrap.mm
else
  JACKWRAP=jackwrap.c
endif

targets=

ifeq ($(LV2AVAIL)$(HAVE_UI), yesyes)
  targets+=setBfreeUI
else
  $(warning standalone JACK/GL-GUI will not be built)
endif

all: $(targets)

setBfreeUI: jackwrap.c $(LV2OBJ) \
	../b_synth/lv2.c ../b_synth/uris.h \
	../b_synth/ui.c $(UIDEPS) \
	../src/cfgParser.h ../src/midi.h
	$(CXX) $(CPPFLAGS) $(CFLAGS) $(UICFLAGS) $(JACKCFLAGS) \
		-DXTERNAL_UI -DHAVE_IDLE_IFACE \
		-DJACK_DESCRIPT=\"desc.h\" \
		-o setBfreeUI \
		$(JACKWRAP) \
		$(LV2OBJ) ../b_synth/lv2.c ../b_synth/ui.c \
		$(LDFLAGS) $(UILIBS) $(LOADLIBES) \
		-ljack -pthread


install: $(targets)
ifneq ($(targets),)
	install -d $(DESTDIR)$(bindir)
	install -m755 setBfreeUI $(DESTDIR)$(bindir)
endif

uninstall:
	rm -f $(DESTDIR)$(bindir)/setBfreeUI

clean:
	rm -f setBfreeUI

.PHONY: clean all install uninstall
