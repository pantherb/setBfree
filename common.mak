# TODO include this only once and export variables

PREFIX ?= /usr/local
OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only
ENABLE_CONVOLUTION ?= no
FONTFILE?=/usr/share/fonts/truetype/ttf-bitstream-vera/VeraBd.ttf
VERSION?=$(shell git describe --tags HEAD 2>/dev/null | sed 's/-g.*$$//;s/^v//' || true)
OSXCOMPAT?=-mmacosx-version-min=10.5
ifeq ($(VERSION),)
  VERSION=$(EXPORTED_VERSION)
endif
ifeq ($(VERSION),)
  $(warning "Cannot determine version information.")
  $(warning "Use the top-level makefile (or full git clone).")
  $(error "-^-")
endif

LV2VERSION=$(VERSION)

bindir = $(PREFIX)/bin
sharedir = $(PREFIX)/share/setBfree
lv2dir = $(PREFIX)/lib/lv2

CFLAGS ?= $(OPTIMIZATIONS) -Wall
ifeq ($(XWIN),)
override CFLAGS += -fPIC
endif
override CFLAGS += -DVERSION="\"$(VERSION)\""

STRIP    ?= strip

CXXFLAGS ?= $(OPTIMIZATIONS) -Wall
GLUICFLAGS=-I. -I.. -Wno-unused-function
STRIPFLAGS=-s

# check for LV2
LV2AVAIL=$(shell pkg-config --exists lv2 && echo yes)

LV2UIREQ=
# check for LV2 idle thread
ifeq ($(shell pkg-config --atleast-version=1.6.0 lv2 || echo no), no)
  override CFLAGS+=-DOLD_SUIL
else
  GLUICFLAGS+=-DHAVE_IDLE_IFACE
  LV2UIREQ=lv2:requiredFeature ui:idleInterface; lv2:extensionData ui:idleInterface;
  HAVE_IDLE=yes
endif

# check for lv2_atom_forge_object  new in 1.8.1 deprecates lv2_atom_forge_blank
ifeq ($(shell pkg-config --atleast-version=1.8.1 lv2 && echo yes), yes)
  override CFLAGS += -DHAVE_LV2_1_8
endif

IS_OSX=
IS_WIN=
PKG_GL_LIBS=
UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  IS_OSX=yes
  LV2LDFLAGS=-dynamiclib
  LIB_EXT=.dylib
  GLUILIBS=-framework Cocoa -framework OpenGL -framework CoreFoundation
  STRIPFLAGS=-u -r -arch all -s $(RW)lv2syms
  UI_TYPE=CocoaUI
  PUGL_SRC=../pugl/pugl_osx.m
  EXTENDED_RE=-E
else
  ifneq ($(XWIN),)
    IS_WIN=yes
    CC=$(XWIN)-gcc
    CXX=$(XWIN)-g++
    STRIP=$(XWIN)-strip
    LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic -Wl,--as-needed -lpthread
    LIB_EXT=.dll
    EXE_EXT=.exe
    GLUILIBS=-lws2_32 -lwinmm -lopengl32 -lglu32 -lgdi32 -lcomdlg32 -lpthread
    UI_TYPE=WindowsUI
    PUGL_SRC=../pugl/pugl_win.cpp
    override CFLAGS+= -DHAVE_MEMSTREAM
    override LDFLAGS += -static-libgcc -static-libstdc++ -DPTW32_STATIC_LIB
  else
    override CFLAGS+= -DHAVE_MEMSTREAM
    LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic
    LIB_EXT=.so
    PKG_GL_LIBS=glu gl
    GLUILIBS=-lX11
    UI_TYPE=X11UI
    PUGL_SRC=../pugl/pugl_x11.c
  endif
  EXTENDED_RE=-r
endif

GLUICFLAGS+=`pkg-config --cflags cairo pango $(PKG_GL_LIBS)`
GLUILIBS+=`pkg-config $(PKG_UI_FLAGS) --libs cairo pango pangocairo $(PKG_GL_LIBS)`

ifneq ($(XWIN),)
GLUILIBS+=-lpthread -lusp10
endif

GLUICFLAGS+=$(LIC_CFLAGS)
GLUILIBS+=$(LIC_LOADLIBES)

ifeq ($(ENABLE_CONVOLUTION), yes)
  CC=$(CXX)
endif

ifneq ($(MOD),)
	MODBRAND=mod:brand \"x42\";
	MODLABEL=mod:label \"$(MODNAME)\";
else
	MODBRAND=
	MODLABEL=
endif

#LV2 / GL-GUI

ifeq ($(FONTFILE),verabd.h)
  FONT_FOUND=yes
else
  ifeq ($(shell test -f $(FONTFILE) || echo no ), no)
    FONT_FOUND=no
  else
    FONT_FOUND=yes
  endif
endif

HAVE_UI=$(shell pkg-config --exists $(PKG_GL_LIBS) ftgl && echo $(FONT_FOUND))

ifeq ($(LV2AVAIL)$(HAVE_UI)$(HAVE_IDLE), yesyesyes)
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
    UILIBS+=`pkg-config --libs zlib`
    UILIBS+=-lm $(OSXCOMPAT)
  else
    ifeq ($(IS_WIN), yes)
      UIDEPS+=../pugl/pugl_win.cpp
      UILIBS=../pugl/pugl_win.cpp
      UILIBS+=`pkg-config --variable=libdir ftgl`/libftgl.a `pkg-config --variable=libdir ftgl`/libfreetype.a
      UILIBS+=`pkg-config --libs zlib`
      UILIBS+=-lws2_32 -lwinmm -lopengl32 -lglu32 -lgdi32 -lcomdlg32 -lpthread
    else
      UIDEPS+=../pugl/pugl_x11.c
      override CFLAGS+=`pkg-config --cflags glu`
      UILIBS=../pugl/pugl_x11.c -lX11
      ifeq ($(STATICBUILD), yes)
        UILIBS+=`pkg-config --libs glu`
        UILIBS+=`pkg-config --variable=libdir ftgl`/libftgl.a `pkg-config --variable=libdir ftgl`/libfreetype.a
        UILIBS+=`pkg-config --libs zlib`
      else
        UILIBS+=`pkg-config --libs glu ftgl`
      endif
      UICFLAGS+=-DFONTFILE=\"$(FONTFILE)\"
    endif
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
