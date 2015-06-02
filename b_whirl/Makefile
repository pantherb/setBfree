include ../common.mak

LOADLIBES = -lm
override CFLAGS+=`pkg-config --cflags lv2` -fvisibility=hidden

targets=whirl.o eqcomp.o

LV2NAME=b_whirl
LV2GUI=b_whirlUI_gl

HAVE_RTK = no

ifeq ($(LV2AVAIL), yes)
  targets+=lv2

  ifeq ($(wildcard $(RW)robtk.mk),)
    $(warning This plugin GUI needs https://github.com/x42/robtk)
    $(info set the RW environment variale to the location of the robtk headers)
  else
    HAVE_RTK = $(shell pkg-config --exists pango cairo $(PKG_GL_LIBS) && echo yes)
  endif

  # WORK IN PROGRESS
  #ifeq ($(HAVE_RTK), yes)
  #  targets+=$(LV2GUI)$(LIB_EXT)
  #else
  #  #$(warning "whirl/leslie GUI will not be built")
  #  #$(warning "robtk or its dependencies are missing")
  #endif
endif

all: $(targets)

lv2: manifest.ttl $(LV2NAME)$(LIB_EXT) b_whirl-configurable.ttl

manifest.ttl: manifest.ttl.in manifest.ui.ttl.in
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/" manifest.ttl.in > manifest.ttl
ifeq ($(HAVE_RTK), yes)
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LV2GUI@/$(LV2GUI)/;s/@LIB_EXT@/$(LIB_EXT)/;s/@UI_TYPE@/$(UI_TYPE)/" manifest.ui.ttl.in >> manifest.ttl
endif

b_whirl-configurable.ttl: b_whirl-configurable.ttl.in b_whirl-configurable.ui.ttl.in
	cat b_whirl-configurable.ttl.in > b_whirl-configurable.ttl
ifeq ($(HAVE_RTK), yes)
	sed "s/@UI_TYPE@/$(UI_TYPE)/;s/@UI_REQ@/$(LV2UIREQ)/;" b_whirl-configurable.ui.ttl.in >>  b_whirl-configurable.ttl 
endif

$(LV2NAME)$(LIB_EXT): lv2.c whirl.o eqcomp.o whirl.h eqcomp.h ../src/cfgParser.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-o $(LV2NAME)$(LIB_EXT) lv2.c whirl.o eqcomp.o \
	  -shared $(LV2LDFLAGS) $(LDFLAGS)


-include $(RW)robtk.mk

$(LV2GUI)$(LIB_EXT): gui/b_whirl.c

install: $(targets)
ifeq ($(LV2AVAIL), yes)
	install -d $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -m755 $(LV2NAME)$(LIB_EXT) $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -m644 manifest.ttl $(LV2NAME)*.ttl $(DESTDIR)$(lv2dir)/$(LV2NAME)
endif
ifeq ($(HAVE_RTK), yes)
	install -m755 $(LV2GUI)$(LIB_EXT) $(DESTDIR)$(lv2dir)/$(LV2NAME)
endif

uninstall:
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/*.ttl
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/$(LV2NAME)$(LIB_EXT)
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/$(LV2GUI)$(LIB_EXT)
	-rmdir $(DESTDIR)$(lv2dir)/$(LV2NAME)

clean:
	rm -f *.o manifest.ttl $(LV2NAME)$(LIB_EXT) b_whirl-configurable.ttl $(LV2GUI)$(LIB_EXT)

.PHONY: clean all install uninstall lv2
