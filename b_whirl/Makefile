MODNAME=Whirl Speaker
include ../common.mak
include ../git2lv2.mk

ifneq ($(MOD),)
	MODLABEL=mod:label \"$(MODNAME)\";
endif

LOADLIBES = -lm
override CFLAGS+=`$(PKG_CONFIG) --cflags lv2` -fvisibility=hidden

targets=whirl.o eqcomp.o

LV2NAME=b_whirl
LV2GUI=b_whirlUI_gl

HAVE_RTK = no

ifeq ($(LV2AVAIL), yes)
  targets+=manifest.ttl $(LV2NAME)$(LIB_EXT)

  ifeq ($(MOD),)
    targets+=b_whirl-configurable.ttl b_whirl.ttl
    ifeq ($(wildcard $(RW)robtk.mk),)
      $(warning This plugin GUI needs https://github.com/x42/robtk)
      $(info set the RW environment variale to the location of the robtk headers)
    else
      HAVE_RTK = $(shell $(PKG_CONFIG) --exists pango cairo $(PKG_GL_LIBS) && echo yes)
    endif

    ifeq ($(HAVE_RTK), yes)
      targets+=$(LV2GUI)$(LIB_EXT)
      targets+=x42-whirl$(EXE_EXT)
      override CFLAGS+=-DRTK_STATIC_INIT
    else
      $(warning "whirl/leslie GUI will not be built")
      $(warning "robtk or its dependencies are missing")
    endif
  else
    targets+=b_whirl-mod.ttl
  endif
endif

all: $(targets)

manifest.ttl: manifest.ttl.in manifest.ui.ttl.in
ifneq ($(MOD),)
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/" manifest.mod.in > manifest.ttl
else
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/" manifest.ttl.in > manifest.ttl
endif
ifeq ($(HAVE_RTK), yes)
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LV2GUI@/$(LV2GUI)/;s/@LIB_EXT@/$(LIB_EXT)/;s/@UI_TYPE@/$(UI_TYPE)/" manifest.ui.ttl.in >> manifest.ttl
endif

b_whirl-configurable.ttl: b_whirl-configurable.ttl.in b_whirl-configurable.ui.ttl.in
	sed "s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g" \
		b_whirl-configurable.ttl.in > b_whirl-configurable.ttl
ifeq ($(HAVE_RTK), yes)
	sed "s/@UI_TYPE@/$(UI_TYPE)/;s/@UI_REQ@/$(LV2UIREQ)/" \
		b_whirl-configurable.ui.ttl.in >> b_whirl-configurable.ttl
endif

b_whirl.ttl: b_whirl.ttl.in
	sed "s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g" \
		b_whirl.ttl.in > b_whirl.ttl

b_whirl-mod.ttl: b_whirl-mod.ttl.in
	sed "s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g;s/@MODBRAND@/$(MODBRAND)/;s/@MODLABEL@/$(MODLABEL)/" \
		b_whirl-mod.ttl.in > b_whirl-mod.ttl

DSP_SRC = lv2.c
DSP_DEPS = whirl.o eqcomp.o whirl.h eqcomp.h ../src/cfgParser.h
GUI_DEPS = gui/b_whirl.c

$(LV2NAME)$(LIB_EXT): $(DSP_SRC) $(DSP_DEPS) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-o $(LV2NAME)$(LIB_EXT) lv2.c whirl.o eqcomp.o \
		-shared $(LV2LDFLAGS) $(LDFLAGS)

JACKCFLAGS=$(CFLAGS) $(GLUICFLAGS) $(LIC_CFLAGS) -Wno-unused-function
JACKCFLAGS+=`$(PKG_CONFIG) --cflags jack lv2 pango pangocairo $(PKG_GL_LIBS)`
JACKLIBS=-lm $(GLUILIBS) $(LIC_LOADLIBES)

$(eval x42_whirl_JACKSRC = lv2.c whirl.c)
x42_whirl_JACKGUI = gui/b_whirl.c
x42_whirl_LV2HTTL = b_whirl.h
x42_whirl_JACKDESC = lv2ui_descriptor
x42-whirl$(EXE_EXT): $(DSP_DEPS) $(GUI_DEPS) \
	        $(x42_whirl_JACKGUI) $(x42_whirl_LV2HTTL)

-include $(RW)robtk.mk

$(LV2GUI)$(LIB_EXT): gui/b_whirl.c gui/rtk_lever.h

install: $(targets)
ifeq ($(LV2AVAIL), yes)
	install -d $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -m755 $(LV2NAME)$(LIB_EXT) $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -m644 manifest.ttl $(LV2NAME)*.ttl $(DESTDIR)$(lv2dir)/$(LV2NAME)
  ifneq ($(MOD),)
	  install -d $(DESTDIR)$(lv2dir)/$(LV2NAME)/modgui
	  install -t $(DESTDIR)$(lv2dir)/$(LV2NAME)/modgui modgui/*
  endif
endif
ifeq ($(HAVE_RTK), yes)
	install -m755 $(LV2GUI)$(LIB_EXT) $(DESTDIR)$(lv2dir)/$(LV2NAME)
	install -d $(DESTDIR)$(bindir)
	install -m755 x42-whirl$(EXE_EXT) $(DESTDIR)$(bindir)
endif

uninstall:
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/*.ttl
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/$(LV2NAME)$(LIB_EXT)
	rm -f $(DESTDIR)$(lv2dir)/$(LV2NAME)/$(LV2GUI)$(LIB_EXT)
	rm -f $(DESTDIR)$(bindir)/x42-whirl$(EXE_EXT)
	rm -rf $(DESTDIR)$(lv2dir)/$(LV2NAME)/modgui
	-rmdir $(DESTDIR)$(lv2dir)/$(LV2NAME)
	-rmdir $(DESTDIR)$(bindir)

clean:
	rm -f *.o manifest.ttl b_whirl.ttl b_whirl-mod.ttl $(LV2NAME)$(LIB_EXT) b_whirl-configurable.ttl $(LV2GUI)$(LIB_EXT) x42-whirl$(EXE_EXT)

.PHONY: clean all install uninstall
