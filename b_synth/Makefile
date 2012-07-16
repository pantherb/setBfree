include ../common.mak

LOADLIBES = -lm
CFLAGS+= -I../src -I../b_overdrive -I../b_reverb -I../b_whirl

targets=

ifeq ($(LV2AVAIL), yes)
  targets+=lv2
  LIB_EXT=.so
  LV2NAME=b_synth
endif

#NOTE: midi.c and cfgParser.c needs to be re-compiled w/o HAVE_ASEQ
# and HAVE_ZITACONVOLVE. Other objects are identical.
OBJ= \
  ../src/midi.c \
  ../src/cfgParser.c \
  ../src/program.o \
  ../src/vibrato.o \
  ../src/tonegen.o \
  ../src/pgmParser.o \
  ../b_whirl/eqcomp.o \
  ../b_whirl/whirl.o \
  ../b_overdrive/overdrive.o \
  ../b_reverb/reverb.o \

all: $(targets)

lv2: manifest.ttl $(LV2NAME)$(LIB_EXT) 

manifest.ttl: manifest.ttl.in
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/" manifest.ttl.in > manifest.ttl

$(LV2NAME)$(LIB_EXT): lv2.c $(OBJ)  ../src/cfgParser.h ../src/midi.h
	$(CC) -o $(LV2NAME)$(LIB_EXT) $(CFLAGS) -shared -Wl,-Bstatic -Wl,-Bdynamic lv2.c $(OBJ)

%.o: %.c %.h
	+$(MAKE) -C ../src $(subst ../src/,,$@)

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
