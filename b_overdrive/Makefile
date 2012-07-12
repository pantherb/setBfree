PREFIX ?= /usr/local
OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only

bindir = $(PREFIX)/bin
lv2dir = $(PREFIX)/lib/lv2

CFLAGS = $(OPTIMIZATIONS) -Wall -fPIC
CFLAGS+= -DVERSION="\"$(VERSION)\""
LOADLIBES = -lm

targets=overdrive.o

ifeq ($(shell pkg-config --exists lv2 lv2core && echo yes), yes)
  targets+=lv2
  LIB_EXT=.so
  LV2NAME=b_overdrive
endif

ifeq ($(shell pkg-config --exists liblo && echo yes), yes)
  CFLAGS+=-DHAVE_LIBLO `pkg-config --cflags liblo`
  LOADLIBES+=`pkg-config --libs liblo`
endif

ifeq ($(shell pkg-config --exists jack && echo yes), yes)
  targets+=jboverdrive
  binaries+=jboverdrive
  CFLAGS+=-DHAVE_JACK `pkg-config --cflags jack`
  LOADLIBES+=`pkg-config --libs jack`
endif

OVMSRC=\
	overmaker.c \
	filterTools.c \
	ovt_biased.c

all: $(targets)

jboverdrive: jboverdrive.c overdrive.o

lv2: manifest.ttl $(LV2NAME)$(LIB_EXT)

manifest.ttl:
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/" manifest.ttl.in > manifest.ttl

$(LV2NAME)$(LIB_EXT): lv2.c overdrive.o
	$(CC) -o $(LV2NAME)$(LIB_EXT) $(CFLAGS) -shared -Wl,-Bstatic -Wl,-Bdynamic lv2.c overdrive.o

overdrive.o: overmaker overdrive.h ../src/cfgParser.h ../src/midi.h
	./overmaker > overdrive.c
	$(CC) -Wall $(OPTIMIZATIONS) -fPIC -c -o overdrive.o overdrive.c

overmaker:$(OVMSRC) overmakerdefs.h
	$(CC) -o $(@) $(OVMSRC) -lm

install: $(targets)
ifneq ($(strip $(binaries)),)
	-install -d $(DESTDIR)$(bindir)
	install -m755 $(binaries) $(DESTDIR)$(bindir)
endif
ifneq ($(strip $(LV2NAME)),)
	install -d $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -m755 $(LV2NAME)$(LIB_EXT) $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -m644 manifest.ttl $(LV2NAME).ttl $(DESTDIR)$(lv2dir)/$(LV2NAME)
endif

uninstall:
ifneq ($(strip $(binaries)),)
	rm -f $(foreach fname,$(binaries),$(DESTDIR)$(bindir)/$(fname))
	-rmdir $(DESTDIR)$(bindir)
endif
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/*.ttl
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/$(LV2NAME)$(LIB_EXT)
	-rmdir $(DESTDIR)$(lv2dir)/$(LV2NAME)

clean:
	rm -f *.o jboverdrive overmaker manifest.ttl $(LV2NAME)$(LIB_EXT) overdrive.c

.PHONY: clean all install uninstall lv2
