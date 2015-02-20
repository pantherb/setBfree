EXPORTED_VERSION=0.7.5
export EXPORTED_VERSION

ifneq ($(FONTFILE),)
export FONTFILE
endif

SUBDIRS = b_overdrive b_whirl b_reverb b_conv src b_synth ui

default: all

$(SUBDIRS)::
	$(MAKE) -C $@ $(MAKECMDGOALS)

all clean install uninstall: $(SUBDIRS)

doc:
	help2man -N --help-option=-H -n 'DSP tonewheel organ' -o doc/setBfree.1 src/setBfree
	help2man -N -n 'The B Preamp/Overdrive Emulator' -o doc/jboverdrive.1 b_overdrive/jboverdrive

dist:
	git archive --format=tar --prefix=setbfree-$(VERSION)/ HEAD | gzip -9 > setbfree-$(VERSION).tar.gz

.PHONY: clean all subdirs install uninstall dist doc
