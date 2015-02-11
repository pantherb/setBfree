setBfree
========

A DSP Tonewheel Organ emulator.

setBfree is a MIDI-controlled, software synthesizer designed to imitate the
sound and properties of the electromechanical organs and sound modification
devices that brought world-wide fame to the names and products of Laurens
Hammond and Don Leslie.

*   http://setbfree.org
*   https://github.com/pantherb/setBfree


Quick-start
-----------

 - start jackd [http://jackaudio.org] or configure JACK autostart
 - run `setBfreeUI`
 - connect JACK Audio/MIDI ports (using qjackctl or you favorite JACK
   connection manager -- note: enable JACK-MIDI or use `a2jmidid` to expose
	 MIDI-devices to JACK, alternatively run `setBfree midi.driver=alsa`)

Usage
-----

Run `setBfree -h` for a quick overview. `setBfree --help` will output a
lengthy list of available options and properties than can be modified.

`setBfree` is the synthesizer engine. It responds to MIDI messages (JACK-MIDI
or ALSA-sequencer) and outputs audio to JACK. The engine does not have any
graphical user interface (GUI). It is usually started from the commandline.

The GUI `setBfreeUI` is a standalone application that wraps the LV2 plugin.

The organ itself, as well as broken out parts (leslie, reverv, overdrive) are
available as LV2 plugins.

Examples:

	setBfree midi.driver="alsa" midi.port="129" jack.connect="jack_rack:in_"
	setBfree jack.out.left="system:playback_7" jack.out.left="system:playback_8"
	setBfreeUI
	jalv.gtk http://gareus.org/oss/lv2/b_synth # LV2 in jalv-host


Getting started - standalone app
--------------------------------

You'll want reliable, low-latency, real-time audio. Therefore you want
[JACK](http://jackaudio.org/). On GNU/Linux we recommend `qjackctl` to start the
jack-audio-server, on OSX jack comes with a GUI called JACK-pilot.

To be continued..

*   http://qjackctl.sf.net/
*   http://jackaudio.org/faq
*   aim for low-latency (256 frames/period or less) - except if you are church
		organist, whom we believe are awesome latency-compensation organic systems
*   http://home.gna.org/a2jmidid/


Getting started - LV2 plugins
-----------------------------

To be continued..


Internal Signal Flow
--------------------

	     +--------+    /-----------\    /--------\
	     |        |    |           |    |        |
	MIDI | Synth- |    |  Preamp/  |    |        |
	--=->|        +--->|           +--->| Reverb +--\
	     | Engine |    | Overdrive |    |        |  |
	     |        |    |           |    |        |  |
	     +--------+    \-----------/    \--------/  |
	                                                |
	  /---------------------------------------------/
	  |
	  |  /--------\ Horn L/R  /-----------\
	  |  |        +---------->|  Cabinet  +-----*--> Audio-Out Left
	  |  |        +---------->| Emulation +--\  |
	  \->| Leslie |           \-----------/  |  |
	     |        +--------------------------|--/
	     |        +--------------------------*-----> Audio-Out Right
	     \--------/ Drum L/R

Render diagram with http://ditaa.org/
A pre-rendered image is available in the doc/ folder.

Each of the stages - except the synth-engine itself - can be bypassed. The
effects are available as standalone LV2 plugins which provides for creating
custom effect chains and use 3rd party effects.

The preamp/overdrive is disabled by default, reverb is set to 30% (unless
overridden with `reverb.mix`, `reverb.dry` or `reverb.wet`). Note that a
stopped leslie will still modify the sound (horn-speaker characteristics,
angle-dependent reflections). Bypassing the leslie (`whirl.bypass=1`) will mute
the drum-output signals and simply copy the incoming audio-signal to the horn
L/R outputs. The cabinet-emulation is an experimental convolution engine and
bypassed by default.

The LV2-synth includes the first three effects until and including the Leslie.
The effects can be triggered via MIDI just as with the standalone JACK
application. The cabinet-emulation is not included in the LV2-synth because it
depends on impulse-response files which are not shipped with the plugin.

The Vibrato and Chorus effects are built into the synth-engine itself, as are
key-click and percussion modes. These features are linked to the tone
generation itself and can not be broken-out to standalone effects.

Summary of Changes since Beatrix
--------------------------------

*   native JACK application (JACK Audio, JACK or ALSA MIDI in)
*   synth engine: variable sample-rate, floating point (beatrix is 22050 Hz, 16bit only)
*   broken-out effects as LV2 plugins; LV2 wrapper around synth-engine
*   built-in documentation

see the ChangeLog and git log for details.


Compile
-------

Install the dependencies and simply call `make` followed by `sudo make install`.

*   libjack-dev - **required** - http://jackaudio.org/ - used for audio I/O
*   tcl-dev, tk-dev - optional, recommended - http://tcl.sf.net/ - virtual Keyboard GUI
*   libasound2-dev - optional, recommended - ALSA MIDI
*   lv2-dev - optional, recommended - build effects as LV2 plugins
*   libftgl-dev, libglu1-mesa-dev, ttf-bitstream-vera - optional, recommended for the LV2 GUI
*   libzita-convolver-dev - optional - IR leslie cabinet-emulation
*   libsndfile1-dev - optional - needed to load IR samples for zita-convolver
*   liblo-dev - optional - http://opensoundcontrol.org/ - used only in standalone preamp/overdrive app.
*   help2man - optional - re-create the man pages
*   doxygen - optional - create source-code annotations


If zita-convolver and libsndfile1-dev are available you can use

	make clean
	make ENABLE_CONVOLUTION=yes

to enable experimental built-in convolution reverb used for leslie cabinet
simulation (at some point down the road this will be enabled the default).


The Makefile understands PREFIX and DESTDIR variables:

	make clean
	make ENABLE_CONVOLUTION=yes PREFIX=/usr
	make install ENABLE_CONVOLUTION=yes PREFIX=/usr DESTDIR=mypackage/setbfree/

**Packagers**: see debian/rules in the git-repository. LDFLAGS can be passed as is,
CFLAGS should be specified by overriding the OPTIMIZATIONS variable.
The packaging also includes a desktop-file to launch setBfree from the
application-menu which is not included in the release.

**Mac/OSX**: The same instructions apply. Tcl/Tk is included with OSX. The JackOSX
packages available from jackaudio.org provide neccesary header and development
files. The setBfree git-repository includes a script to build universal binary
and create a DMG.  However this script assumes that universal (PPC, i386,
x86-64) versions of the JACK-libraries as well as zita-convolver and libsndfile
are available in /usr/local/ on the build-host.

Thanks
------

Many thanks to all who contributed ideas, bug-reports, patches and feedback. In
Particular (in alphabetical order): Dominique Michel, Fons Adriaensen, Jeremy
Jongepier, Julien Claasen and Ken Restivo.
