include ../common.mak

LOADLIBES = -lm
override CFLAGS+=`pkg-config --cflags lv2` -fvisibility=hidden

targets=reverb.o

LV2NAME=b_reverb

ifeq ($(LV2AVAIL), yes)
  targets+=lv2
endif

all: $(targets)

lv2: manifest.ttl $(LV2NAME)$(LIB_EXT) 

manifest.ttl:
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/" manifest.ttl.in > manifest.ttl

$(LV2NAME)$(LIB_EXT): lv2.c reverb.o
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-o $(LV2NAME)$(LIB_EXT) lv2.c reverb.o \
	  -shared $(LV2LDFLAGS) $(LDFLAGS)

%.o: %c %.h ../src/cfgParser.h

install: $(targets)
ifeq ($(LV2AVAIL), yes)
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
