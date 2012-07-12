PREFIX ?= /usr/local
OPTIMIZATIONS?=-msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only
ENABLE_CONVOLUTION ?= no ## set to 'yes' to enable convolution for speaker cabinet emulation - requires IR files.
VERSION=0.5.0

export PREFIX
export VERSION
export OPTIMIZATIONS

SUBDIRS = b_overdrive b_whirl b_reverb b_conv src b_synth vb3kb

default: all

$(SUBDIRS)::
	$(MAKE) -C $@ $(MAKECMDGOALS)

all clean install uninstall: $(SUBDIRS)

doc:
	help2man -N --help-option=-H -n 'DSP tonewheel organ' -o doc/setBfree.1 src/setBfree
	help2man -N -n 'The B Preamp/Overdrive Emulator' -o doc/jboverdrive.1 b_overdrive/jboverdrive
	VB3KBTCL=vb3kb/vb3kb.tcl help2man -N -n 'Virtual Organ - setBfree control GUI' \
		--help-option=--help --version-option=--version --no-discard-stderr -o doc/vb3kb.1 vb3kb/vb3kb

dist:
	git archive --format=tar --prefix=setbfree-$(VERSION)/ HEAD | gzip -9 > setbfree-$(VERSION).tar.gz

.PHONY: clean all subdirs install uninstall dist doc
