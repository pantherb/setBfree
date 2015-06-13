include ../common.mak

LOADLIBES = -lm

targets=overdrive.o

LV2NAME=b_overdrive

ifeq ($(LV2AVAIL), yes)
  targets+=lv2
endif

ifeq ($(shell pkg-config --exists liblo && echo yes), yes)
  override CFLAGS+=-DHAVE_LIBLO `pkg-config --cflags liblo`
  LOADLIBES+=`pkg-config --libs liblo`
endif

ifeq ($(shell pkg-config --exists jack && echo yes)$(OVERDRIVEJACK), yesyes)
  targets+=jboverdrive
  binaries+=jboverdrive
  override CFLAGS+=-DHAVE_JACK `pkg-config --cflags jack`
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
	$(CC) $(CPPFLAGS) $(CFLAGS) -fvisibility=hidden \
		-o $(LV2NAME)$(LIB_EXT) lv2.c overdrive.o \
	  -shared $(LV2LDFLAGS) $(LDFLAGS)

overdrive.o: overdrive.c overdrive.h ../src/cfgParser.h ../src/midi.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o overdrive.o overdrive.c

overmaker:$(OVMSRC) overmakerdefs.h
	$(CC) -o $(@) $(OVMSRC) -lm

overdrive: overmaker
	./overmaker > overdrive.c

install: $(targets)
ifneq ($(strip $(binaries)),)
	-install -d $(DESTDIR)$(bindir)
	install -m755 $(binaries) $(DESTDIR)$(bindir)
endif
ifeq ($(LV2AVAIL), yes)
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
	rm -f *.o jboverdrive overmaker manifest.ttl $(LV2NAME)$(LIB_EXT)

.PHONY: clean all install uninstall lv2 overdrive
