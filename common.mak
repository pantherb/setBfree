# TODO include this only once and export variables

PREFIX ?= /usr/local
OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only
ENABLE_CONVOLUTION ?= no

bindir = $(PREFIX)/bin
sharedir = $(PREFIX)/share/setBfree
lv2dir = $(PREFIX)/lib/lv2

CFLAGS = $(OPTIMIZATIONS) -Wall -fPIC
CFLAGS+= -DVERSION="\"$(VERSION)\""

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
LV2AVAIL=$(shell pkg-config --exists lv2 lv2core && echo yes)
