include ../common.mak

LOADLIBES = -lm
CFLAGS+= -I../src -I../b_overdrive -I../b_reverb -I../b_whirl -std=c99
CFLAGS+=`pkg-config --cflags lv2 lv2core`

targets=

ifeq ($(IS_OSX), yes)
  HAVE_UI=yes
else
  HAVE_UI=$(shell pkg-config --exists glu && echo yes)
endif

ifeq ($(LV2AVAIL), yes)
  LV2NAME=b_synth
  targets+=lv2

  ifeq ($(HAVE_UI), yes)
    #UICFLAGS=`pkg-config --cflags pugl-0`
    #UILIBS=`pkg-config --libs pugl-0`
    UICFLAGS=-I..
    UIDEPS=../pugl/pugl.h ../pugl/pugl_internal.h ui_model.h
    UIDEPS+=drawbar.c wood.c dial.c
    UIDEPS+=btn_vibl.c btn_vibu.c
    UIDEPS+=btn_perc.c btn_perc_decay.c btn_perc_harmonic.c btn_perc_volume.c
    UIDEPS+=btn_overdrive.c
    UIDEPS+=bg_right_ctrl.c bg_left_ctrl.c bg_leslie_drum.c bg_leslie_horn.c
    UIDEPS+=help_screen_image.c
    LV2GUI=b_synthUI
    ifeq ($(IS_OSX), yes)
      UIDEPS+=../pugl/pugl_osx.m
      UILIBS=../pugl/pugl_osx.m -framework Cocoa -framework OpenGL
      UI_TYPE=CocoaUI
    else
      UIDEPS+=../pugl/pugl_x11.c
      CFLAGS+=`pkg-config --cflags glu`
      UILIBS=../pugl/pugl_x11.c -lX11 `pkg-config --libs glu`
      UI_TYPE=X11UI
    endif
    ifeq ($(shell pkg-config --exists ftgl && echo yes), yes)
      CFLAGS+=`pkg-config --cflags ftgl` -DHAVE_FTGL
      UILIBS+=`pkg-config --libs ftgl`
    endif
    targets+=$(LV2GUI)$(LIB_EXT)
  else
    $(warning "openGL/GLU is not available - install glu-dev to include LV2 GUI")
  endif
endif

#NOTE: midi.c and cfgParser.c needs to be re-compiled w/o HAVE_ASEQ
# and HAVE_ZITACONVOLVE. Other objects are identical.
OBJ= \
  ../src/midi.c \
  ../src/cfgParser.c \
  ../src/program.o \
  ../src/vibrato.o \
  ../src/state.o \
  ../src/tonegen.o \
  ../src/pgmParser.o \
  ../b_whirl/eqcomp.o \
  ../b_whirl/whirl.o \
  ../b_overdrive/overdrive.o \
  ../b_reverb/reverb.o \

all: $(targets)

lv2: manifest.ttl $(LV2NAME)$(LIB_EXT) $(LV2NAME).ttl

manifest.ttl: manifest.ttl.in manifest.ui.ttl.in
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/" manifest.ttl.in > manifest.ttl
ifeq ($(HAVE_UI), yes)
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LV2GUI@/$(LV2GUI)/;s/@LIB_EXT@/$(LIB_EXT)/;s/@UI_TYPE@/$(UI_TYPE)/" manifest.ui.ttl.in >> manifest.ttl
endif

$(LV2NAME).ttl: $(LV2NAME).ttl.in $(LV2NAME).ui.ttl.in
	cat $(LV2NAME).ttl.in > $(LV2NAME).ttl
ifeq ($(HAVE_UI), yes)
	sed "s/@UI_TYPE@/$(UI_TYPE)/;s/@UI_REQ@/$(LV2UIREQ)/;" $(LV2NAME).ui.ttl.in >> $(LV2NAME).ttl
endif

$(LV2NAME)$(LIB_EXT): lv2.c $(OBJ) uris.h ../src/cfgParser.h ../src/midi.h
	$(CC) -o $(LV2NAME)$(LIB_EXT) $(CFLAGS) -shared $(LV2LDFLAGS) lv2.c $(OBJ)

$(LV2GUI)$(LIB_EXT): ui.c uris.h $(UIDEPS)
	$(CC) $(CFLAGS) \
		-o $(LV2GUI)$(LIB_EXT) ui.c \
		$(LDFLAGS) $(UICFLAGS) $(UILIBS) -shared $(LV2LDFLAGS)

%.o: %.c %.h
	+$(MAKE) -C ../src $(subst ../src/,,$@)

install: $(targets)
ifneq ($(strip $(LV2NAME)),)
	install -d $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -m755 $(LV2NAME)$(LIB_EXT) $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -m644 manifest.ttl $(LV2NAME).ttl $(DESTDIR)$(lv2dir)/$(LV2NAME)
  ifeq ($(HAVE_UI), yes)
	install -m755 $(LV2GUI)$(LIB_EXT) $(DESTDIR)$(lv2dir)/$(LV2NAME)
  endif
endif

uninstall:
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/*.ttl
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/$(LV2NAME)$(LIB_EXT)
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/$(LV2GUI)$(LIB_EXT)
	-rmdir $(DESTDIR)$(lv2dir)/$(LV2NAME)

clean:
	rm -f *.o manifest.ttl b_synth.ttl
	rm -f $(LV2NAME)$(LIB_EXT)
	rm -f $(LV2GUI)$(LIB_EXT)

.PHONY: clean all install uninstall lv2
