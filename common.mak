# TODO include this only once and export variables

PREFIX ?= /usr/local
OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only
ENABLE_CONVOLUTION ?= no

bindir = $(PREFIX)/bin
sharedir = $(PREFIX)/share/setBfree
lv2dir = $(PREFIX)/lib/lv2

CFLAGS ?= $(OPTIMIZATIONS) -Wall
override CFLAGS += -fPIC
override CFLAGS += -DVERSION="\"$(VERSION)\""

CXXFLAGS = $(OPTIMIZATIONS) -Wall

# detect Tcl/Tk
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

# check for LV2
LV2AVAIL=$(shell pkg-config --exists lv2 && echo yes)

LV2UIREQ=
# check for LV2 idle thread -- requires 'lv2', atleast_version='1.4.1
ifeq ($(shell pkg-config --atleast-version=1.4.6 lv2 || echo no), no)
  override CFLAGS+=-DOLD_SUIL
else
  LV2UIREQ=lv2:requiredFeature ui:idleInterface;\\n\\tlv2:extensionData ui:idleInterface;
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
