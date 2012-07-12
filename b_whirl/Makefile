PREFIX ?= /usr/local
OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only

bindir = $(PREFIX)/bin
lv2dir = $(PREFIX)/lib/lv2

CFLAGS = $(OPTIMIZATIONS) -Wall -fPIC
LOADLIBES = -lm

targets=whirl.o eqcomp.o

ifeq ($(shell pkg-config --exists lv2 lv2core && echo yes), yes)
  targets+=lv2
  LIB_EXT=.so
  LV2NAME=b_whirl
endif

all: $(targets)

lv2: manifest.ttl $(LV2NAME)$(LIB_EXT) 

manifest.ttl:
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/" manifest.ttl.in > manifest.ttl

$(LV2NAME)$(LIB_EXT): lv2.c whirl.o eqcomp.o whirl.h eqcomp.h ../src/cfgParser.h
	$(CC) -o $(LV2NAME)$(LIB_EXT) $(CFLAGS) -shared -Wl,-Bstatic -Wl,-Bdynamic lv2.c whirl.o eqcomp.o

install: $(targets)
ifneq ($(strip $(LV2NAME)),)
	install -d $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -m755 $(LV2NAME)$(LIB_EXT) $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -m644 manifest.ttl $(LV2NAME).ttl $(DESTDIR)$(lv2dir)/$(LV2NAME)
endif

uninstall:
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/*.ttl
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/$(LV2NAME)$(LIB_EXT)
	-rmdir $(DESTDIR)$(lv2dir)/$(LV2NAME)

clean:
	rm -f *.o manifest.ttl $(LV2NAME)$(LIB_EXT)

.PHONY: clean all install uninstall lv2
