include ../common.mak

INCLUDEPATHS = -I../b_overdrive/ -I../b_whirl  -I../b_reverb -I../b_conv

override CFLAGS += $(INCLUDEPATHS)
override CFLAGS += -DSHAREDIR="\"$(sharedir)\""
LOADLIBES = -lm -lpthread

BXCC=$(CC)

OBJS= \
	midi.o \
	midi_aseq.o \
	state.o \
	vibrato.o \
	tonegen.o \
	program.o \
	pgmParser.o \
	cfgParser.o \
	main.o \
	midnam.o \
	memstream.o \
	../b_reverb/reverb.c \
	../b_whirl/eqcomp.c \
	../b_whirl/whirl.c \
	../b_overdrive/overdrive.c

HEADERS = \
	cfgParser.h \
	global_inst.h \
	main.h \
	midi.h \
	midi_aseq.h \
	midi_types.h \
	pgmParser.h \
	program.h \
	vibrato.h \
	tonegen.h \
	defaultpgm.h \
	memstream.h \
	../b_reverb/reverb.h \
	../b_whirl/eqcomp.h \
	../b_whirl/whirl.h \
	../b_overdrive/overdrive.h


ifeq ($(shell pkg-config --exists jack || echo no), no)
  $(error "JACK is required - install libjack-dev or libjack-jackd2-dev")
endif

ifeq ($(shell pkg-config --exists alsa && echo yes)$(ENABLE_ALSA), yesyes)
  override CFLAGS+=-DHAVE_ASEQ `pkg-config --cflags alsa`
  LOADLIBES+=`pkg-config --libs alsa`
else
  $(info "compiling w/o ALSA Sequencer - libasound is missing - JACK-midi is always on")
endif


override CFLAGS+=`pkg-config --cflags jack`
ifeq ($(WEAKJACK),)
  LOADLIBES+=`pkg-config --libs jack`
else
  LOADLIBES+=-ldl
endif

ifeq ($(shell pkg-config --exists sndfile \
	&& test -f /usr/include/zita-convolver.h -o -f /usr/local/include/zita-convolver.h \
	&& echo yes), $(ENABLE_CONVOLUTION))
  override CFLAGS+= -DHAVE_ZITACONVOLVE `pkg-config --cflags sndfile`
  LOADLIBES+=-lzita-convolver `pkg-config --libs sndfile`
  OBJS+=../b_conv/convolution.o
  HEADERS+=../b_conv/convolution.h
  BXCC=$(CXX)
else
  $(info "compiling w/o ZITA Convolution Reverb -- needs libzita-convolver3 and libsndfile")
endif

all: setBfree$(EXE_EXT)

setBfree$(EXE_EXT): $(HEADERS) $(OBJS)
	$(BXCC) $(CPPFLAGS) $(CFLAGS) \
		-o $(@) $(OBJS) $(WEAKJACK) \
		$(LDFLAGS) $(LOADLIBES)

programd: program.c pgmParser.c program.h pgmParser.h main.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wall \
		-DPRG_MAIN \
		-o $(@) program.c pgmParser.c

configd: cfgParser.c main.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wall \
		-DCFG_MAIN \
		-o $(@) cfgParser.c

program: programd ../pgm/default.pgm
	$(info re-generating default program include-file)
	./programd ../pgm/default.pgm > defaultpgm.h

program.o: defaultpgm.h

# override .o in parent dirs -- needed for parallel builds

../b_reverb/reverb.o: ../b_reverb/reverb.[ch]
	+$(MAKE) -C ../b_reverb reverb.o

../b_whirl/eqcomp.o: ../b_whirl/eqcomp.[ch]
	+$(MAKE) -C ../b_whirl eqcomp.o

../b_whirl/whirl.o: ../b_whirl/whirl.[ch]
	+$(MAKE) -C ../b_whirl whirl.o

../b_overdrive/overdrive.o: \
  ../b_overdrive/filterTools.[ch] \
  ../b_overdrive/overdrive.h \
  ../b_overdrive/overmaker.[ch] \
  ../b_overdrive/overmakerdefs.h \
  ../b_overdrive/ovt_biased.[ch]
	+$(MAKE) -C ../b_overdrive overdrive.o

../b_conv/convolution.o: ../b_conv/convolution.cc ../b_conv/convolution.h
	+$(MAKE) -C ../b_conv convolution.o

clean:
	rm -f *.o core *.log *~ setBfree$(EXE_EXT) configd programd

install: setBfree
	install -d $(DESTDIR)$(bindir)
	install -m755 setBfree $(DESTDIR)$(bindir)
	install -d $(DESTDIR)$(sharedir)/cfg
	install -d $(DESTDIR)$(sharedir)/pgm
	install -m644 ../cfg/*.cfg $(DESTDIR)$(sharedir)/cfg
	install -m644 ../pgm/*.pgm $(DESTDIR)$(sharedir)/pgm

uninstall:
	rm -f $(DESTDIR)$(bindir)/setBfree
	rm -f $(DESTDIR)$(sharedir)/cfg/*
	rm -f $(DESTDIR)$(sharedir)/pgm/*
	-rmdir $(DESTDIR)$(bindir)
	-rmdir $(DESTDIR)$(sharedir)/cfg
	-rmdir $(DESTDIR)$(sharedir)/pgm
	-rmdir $(DESTDIR)$(sharedir)

.PHONY: clean all install uninstall program
