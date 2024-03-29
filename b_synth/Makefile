MODNAME=setBfree
include ../common.mak
include ../git2lv2.mk

LOADLIBES = -lm
override CFLAGS+= -I../src -I../b_overdrive -I../b_reverb -I../b_whirl
override CFLAGS+=`$(PKG_CONFIG) --cflags lv2` -DLV2SYNTH -fvisibility=hidden
targets=

LV2NAME=b_synth
LV2GUI=b_synthUI
TX=textures/

ifeq ($(LV2AVAIL), yes)
  targets+=lv2
  ifeq ($(HAVE_UI), yes)
    targets+=$(LV2GUI)$(LIB_EXT)
  else
    $(warning "Synth GUI will not be built")
    $(warning "either openGL/GLU is not available - install glu-dev, ftgl-dev")
    $(warning "or $(FONTFILE) cannot be found")
  endif
endif

ifeq ($(IS_WIN)$(IS_OSX), yes)
  CC=$(CXX)
else
  ifneq ($(ENABLE_CONVOLUTION), yes)
    override CFLAGS+=-std=c99
  endif
endif

all: $(targets)

lv2: manifest.ttl $(LV2NAME)$(LIB_EXT) $(LV2NAME).ttl

manifest.ttl: manifest.ttl.in manifest.ui.ttl.in
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/;s/@MODGUITTL@/$(MODGUITTL)/" manifest.ttl.in > manifest.ttl
ifeq ($(HAVE_UI), yes)
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LV2GUI@/$(LV2GUI)/;s/@LIB_EXT@/$(LIB_EXT)/;s/@UI_TYPE@/$(UI_TYPE)/" manifest.ui.ttl.in >> manifest.ttl
endif

$(LV2NAME).ttl: $(LV2NAME).ttl.in $(LV2NAME).ui.ttl.in
	sed "s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g;s/@MODBRAND@/$(MODBRAND)/;s/@MODLABEL@/$(MODLABEL)/" \
		$(LV2NAME).ttl.in > $(LV2NAME).ttl
ifeq ($(HAVE_UI), yes)
	sed "s/@UI_TYPE@/$(UI_TYPE)/;s/@UI_REQ@/$(LV2UIREQ)/;" $(LV2NAME).ui.ttl.in >> $(LV2NAME).ttl
endif

$(LV2NAME)$(LIB_EXT): lv2.c $(LV2OBJ) uris.h ../src/cfgParser.h ../src/midi.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIC_CFLAGS) \
		-o $(LV2NAME)$(LIB_EXT) \
		lv2.c $(LV2OBJ) \
	  -shared $(LV2LDFLAGS) $(LDFLAGS) $(LOADLIBES) $(LIC_LOADLIBES)

$(LV2GUI)$(LIB_EXT): ui.c uris.h $(UIDEPS) $(FONTFILE) $(LV2OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UICFLAGS) \
		-o $(LV2GUI)$(LIB_EXT) ui.c -DCONFIGDOCONLY -D XKEYFOCUSGRAB \
		../src/midi.c \
		../src/program.c \
		../src/vibrato.c \
		../src/tonegen.c \
		../b_whirl/whirl.c \
		../b_overdrive/overdrive.c \
		../b_reverb/reverb.c \
		-shared $(LV2LDFLAGS) $(LDFLAGS) $(UILIBS)

verabd.h: VeraBd.ttf
	xxd -i VeraBd.ttf > verabd.h

install: $(targets)
ifeq ($(LV2AVAIL), yes)
	install -d $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -m755 $(LV2NAME)$(LIB_EXT) $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -m644 manifest.ttl $(LV2NAME).ttl $(DESTDIR)$(lv2dir)/$(LV2NAME)
  ifeq ($(HAVE_UI), yes)
	  install -m755 $(LV2GUI)$(LIB_EXT) $(DESTDIR)$(lv2dir)/$(LV2NAME)
  endif
  ifneq ($(MOD),)
	  install -m644 modgui.ttl $(DESTDIR)$(lv2dir)/$(LV2NAME)
	  install -d $(DESTDIR)$(lv2dir)/$(LV2NAME)/modgui
	  install -t $(DESTDIR)$(lv2dir)/$(LV2NAME)/modgui modgui/*
  endif
endif

uninstall:
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/*.ttl
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/$(LV2NAME)$(LIB_EXT)
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/$(LV2GUI)$(LIB_EXT)
	rm -rf $(DESTDIR)$(lv2dir)/$(LV2NAME)/modgui
	-rmdir $(DESTDIR)$(lv2dir)/$(LV2NAME)

clean:
	rm -f *.o manifest.ttl b_synth.ttl
	rm -f $(LV2NAME)$(LIB_EXT)
	rm -f $(LV2GUI)$(LIB_EXT)

.PHONY: clean all install uninstall lv2
