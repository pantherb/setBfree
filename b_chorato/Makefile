include ../common.mak
include ../git2lv2.mk

LOADLIBES = -lm
override CFLAGS+= -I../src
override CFLAGS+=`pkg-config --cflags lv2` -fvisibility=hidden

targets=

LV2NAME=b_chorato

ifeq ($(LV2AVAIL), yes)
  targets+=lv2
endif

all: $(targets)

lv2: manifest.ttl $(LV2NAME)$(LIB_EXT) $(LV2NAME).ttl

manifest.ttl:
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/" manifest.ttl.in > manifest.ttl

$(LV2NAME).ttl: $(LV2NAME).ttl.in Makefile
	sed "s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g" \
		$(LV2NAME).ttl.in > $(LV2NAME).ttl

$(LV2NAME)$(LIB_EXT): lv2.c ../src/vibrato.c ../src/vibrato.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-o $(LV2NAME)$(LIB_EXT) lv2.c ../src/vibrato.c \
	  -shared $(LV2LDFLAGS) $(LDFLAGS)

install: $(targets)
ifeq ($(LV2AVAIL)$(INSTALL_EXTRA_LV2), yesyes)
	install -d $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -m755 $(LV2NAME)$(LIB_EXT) $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -m644 manifest.ttl $(LV2NAME).ttl $(DESTDIR)$(lv2dir)/$(LV2NAME)
endif

uninstall:
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/*.ttl
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/$(LV2NAME)$(LIB_EXT)
	-rmdir $(DESTDIR)$(lv2dir)/$(LV2NAME)

clean:
	rm -f *.o manifest.ttl $(LV2NAME)$(LIB_EXT) $(LV2NAME).ttl

.PHONY: clean all install uninstall lv2
