# TODO include this only once and export variables

PREFIX ?= /usr/local
OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only
ENABLE_CONVOLUTION ?= no
FONTFILE?=/usr/share/fonts/truetype/ttf-bitstream-vera/VeraBd.ttf

bindir = $(PREFIX)/bin
sharedir = $(PREFIX)/share/setBfree
lv2dir = $(PREFIX)/lib/lv2

CFLAGS ?= $(OPTIMIZATIONS) -Wall
override CFLAGS += -fPIC
override CFLAGS += -DVERSION="\"$(VERSION)\""

CXXFLAGS = $(OPTIMIZATIONS) -Wall

# check for LV2
LV2AVAIL=$(shell pkg-config --exists lv2 && echo yes)

LV2UIREQ=
# check for LV2 idle thread -- requires 'lv2', atleast_version='1.4.1
ifeq ($(shell pkg-config --atleast-version=1.4.6 lv2 || echo no), no)
  override CFLAGS+=-DOLD_SUIL
else
  LV2UIREQ=lv2:requiredFeature ui:idleInterface; lv2:extensionData ui:idleInterface;
endif

# check for lv2_atom_forge_object  new in 1.8.1 deprecates lv2_atom_forge_blank
ifeq ($(shell pkg-config --atleast-version=1.8.1 lv2 && echo yes), yes)
  override CFLAGS += -DHAVE_LV2_1_8
endif

IS_OSX=
UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  IS_OSX=yes
  LV2LDFLAGS=-dynamiclib
  LIB_EXT=.dylib
else
  override CFLAGS+= -DHAVE_MEMSTREAM
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic
  LIB_EXT=.so
endif

ifeq ($(ENABLE_CONVOLUTION), yes)
  CC=$(CXX)
endif

#LV2 / GL-GUI

ifeq ($(shell test -f $(FONTFILE) || echo no ), no)
  FONT_FOUND=no
else
  FONT_FOUND=yes
endif

ifeq ($(IS_OSX), yes)
  HAVE_UI=$(shell pkg-config --exists ftgl && echo yes)
else
  HAVE_UI=$(shell pkg-config --exists glu ftgl && echo $(FONT_FOUND))
endif

ifeq ($(LV2AVAIL)$(HAVE_UI), yesyes)
  UICFLAGS=-I..
  UIDEPS=../pugl/pugl.h ../pugl/pugl_internal.h ../b_synth/ui_model.h
  UIDEPS+=$(TX)drawbar.c $(TX)wood.c $(TX)dial.c
  UIDEPS+=$(TX)btn_vibl.c $(TX)btn_vibu.c $(TX)btn_overdrive.c $(TX)btn_perc_volume.c
  UIDEPS+=$(TX)btn_perc.c $(TX)btn_perc_decay.c $(TX)btn_perc_harmonic.c
  UIDEPS+=$(TX)bg_right_ctrl.c $(TX)bg_left_ctrl.c $(TX)bg_leslie_drum.c $(TX)bg_leslie_horn.c
  UIDEPS+=$(TX)help_screen_image.c
  UIDEPS+=$(TX)ui_button_image.c $(TX)ui_proc_image.c
  UIDEPS+=$(TX)uim_background.c $(TX)uim_cable1.c $(TX)uim_cable2.c $(TX)uim_caps.c
  UIDEPS+=$(TX)uim_tube1.c $(TX)uim_tube2.c
  ifeq ($(IS_OSX), yes)
    UIDEPS+=../pugl/pugl_osx.m
    UILIBS=../pugl/pugl_osx.m -framework Cocoa -framework OpenGL
    UILIBS+=`pkg-config --variable=libdir ftgl`/libftgl.a `pkg-config --variable=libdir ftgl`/libfreetype.a
    UILIBS+=-lm -mmacosx-version-min=10.5
    UI_TYPE=CocoaUI
    UICFLAGS+=-DBUILTINFONT
    override FONTFILE=verabd.h
  else
    UIDEPS+=../pugl/pugl_x11.c
    override CFLAGS+=`pkg-config --cflags glu` -std=c99
    UILIBS=../pugl/pugl_x11.c -lX11 `pkg-config --libs glu ftgl`
    UI_TYPE=X11UI
    UICFLAGS+=-DFONTFILE=\"$(FONTFILE)\"
  endif
  UICFLAGS+=`pkg-config --cflags freetype2` `pkg-config --cflags ftgl` -DHAVE_FTGL -DUINQHACK=Sbf
endif

#NOTE: midi.c and cfgParser.c needs to be re-compiled w/o HAVE_ASEQ
# and HAVE_ZITACONVOLVE. Other objects are identical.
LV2OBJ= \
  ../src/midi.c \
  ../src/cfgParser.c \
  ../src/program.c \
  ../src/vibrato.c \
  ../src/state.c \
  ../src/tonegen.c \
  ../src/pgmParser.c \
  ../src/memstream.c \
  ../b_whirl/eqcomp.c \
  ../b_whirl/whirl.c \
  ../b_overdrive/overdrive.c \
  ../b_reverb/reverb.c \
