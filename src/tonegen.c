/* setBfree - DSP tonewheel organ
 *
 * Copyright (C) 2003-2004 Fredrik Kilander <fk@dsv.su.se>
 * Copyright (C) 2008-2012 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2012 Will Panther <pantherb@setbfree.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
 */

/* tonegen.c
 *
 * 13-oct-2004/FK Tentatively added HIPASS_PERCUSSION at the end of the file.
 *                It is a simple integrator that takes the low end out of the
 *                percussion signal. Adjust osc.perc.gain to 10-12 to fit.
 *                Re-read today the A100 manual and found the two magic words
 *                'highpass filtered' in the treatment of the percussion.
 *
 * 30-sep-2004/FK Added transformer crosstalk.
 *
 * 22-sep-2004/FK Added osc.contribution-min parameter.
 *
 * 22-aug-2004/FK (Re)introduced muted percussion triggering bus.
 *
 * 21-aug-2004/FK Changed the signature of setDrawBars() to enable
 *                programme control of lower and pedal drawbars.
 *
 * 19-aug-2004/FK Fixed unwanted clicking. Envelopes converted to float.
 *                Core instructions extended and generalised. Added to
 *                comments for oscGenerateFragment().
 *
 * 15-aug-2004/FK Added call to float vibrato FX.
 *
 * 19-jul-2004/FK Version 0.4.1
 *
 * 23-jun-2004/FK tonegen_new.c
 *
 * 13-jun-2004/FK Added 50 Hz gears.
 *
 * 11-jun-2004/FK Added tuning by gear ratios as an alternative to equal
 *                temperament. Gear ratios were obtained from
 *                http://www.bikexprt.com/tunings/tunings2.htm
 *
 * 04-apr-2004/FK Added MIDI controllers for the drawbars.
 *
 * 23-sep-2003/FK Version 0.3.1 to experiment with stuff.
 *                Upped the precision by a magnitude to get rid of looping
 *                noise in the higher frequencies.
 *
 * 22-sep-2003/FK Fixed a small bug in the click envelope initializer.
 *                Fixed a small bug in the initialization routine.
 *                Added routine dumpOscToText() as a debugging aid.
 *
 * 20-Aug-2003/FK Added additional harmonics to wavetable loading, for
 *                experimental purposes. However, the residual harmonics
 *                in the real instruments are likely to be way low, possibly
 *                below the threshold. Aged capacitors, however, is another
 *                matter.
 *
 * 30-May-2003/FK Removed fixed-point percussion code. Cleanup, commenting.
 *
 * 25-May-2003/FK Redid percussion with a float multiplier that decays expo-
 *                nentially, like it should (or so I think). The quantization
 *                noise disappeared, at last.
 *
 * 07-may-2003/FK Added a single bit to the acx field in the Connection struct
 *                to distinguish between the added and active lists. That
 *                would appear to take care of the crashing bug, although I
 *                did suffer a hung note while testing it. Strange, but not
 *                conclusive.
 *
 * 06-may-2003/FK New way of writing dates. And I've begun looking for a way
 *                to fix another crashing bug: when the same connection is
 *                first closed and then opened in the same call to generate-
 *                Fragment(), the open code thinks the connection is active
 *                and trashes the active connection list, because the con-
 *                nection is still on the added list. This rare condition
 *                (open and close in the same call) happens more often than
 *                you would assume; rapid playing triggers it 5 percent of
 *                the time because fingers bounce off keys that just bounces
 *                down and the up again.
 *
 * 2002-11-26/FK Reinstated the key compression with a fix for the release
 *               click and compensated for mixing at unity. Seems to work
 *               better. Percussion is still noisy (egde noise from steps).
 *
 * 2002-11-23/FK Radical update of the oscillator structure and fragment
 *               generator. In the previous scheme we modelled for each
 *               oscillator the buses it was connected to. In the current
 *               scheme we keep track of the connections and see which
 *               are active. The hung note bug is fixed. The routine
 *               oscGenerateFragment() became simpler and more efficient.
 *               New initialization routines for tapering has been added.
 *               Routine dumpKey() added to dump keyOsc and the connections
 *               it references to text files.
 *
 * 2002-11-15/FK Changed the name from osctest.c to tonegen.c.
 *               Began to change to a single connection event queue.
 *               Removed code conditioned on ENV_COMPUTED.
 *               Made code dependent on ENV_SHAPED unconditioned and removed
 *               the symbol definition.
 *               Removed dead code in COMMENT ifdefs.
 *               Commented out old keyclick code. Setting function remains
 *               but is currently void.
 *
 * 2002-07-02/FK Found the source of the key release click. It was the
 *               key compression feature that was no longer compatible
 *               with the rewritten fragment generator. The reason for
 *               that was in turn that the when a release buffer
 *               is rendered, the key down count is too low, which gives
 *               it the wrong amplification. Percussion was also subject
 *               to the same kind of timing problem. because it was being
 *               reset prior to rendering the release fragment.
 *
 * 2002-06-29/FK Crash bugs found and fixed. Still a bit of gritty sound.
 *               Percussion sounds weird on key release.
 *
 * 2002-06-27/FK First version compiled. Crashed.
 *
 * 2002-06-25/FK Began work on a major rewrite of the fragment generator.
 *
 * 2002-02-23/FK Modified the on-key click to give 9 distinct clicks
 *               rather than one. Slightly better, although I wonder
 *               if one should simulate contact bounce too.
 *
 * 2002-01-21/FK Major rewrite of the wave buffer compiler. The code now
 *               performs a constrained search for the best number of waves
 *               to compile for each frequency in the given tuning. This
 *               pushes the loop artefacts down both in frequency and
 *               amplitude and it sounds better without them.
 *
 *               Also rewired the generator/key contact grid model to
 *               support three different variants of tonegenerator.
 *
 * 2002-01-19/FK Minor adjustments and adding comments.
 *
 * 2002-01-18/FK Fixed a bug in the tone generator initialization. Higher
 *               notes copied the lowest note buffers after they had been
 *               attenuated. As a result, the higher notes had buffers that
 *               were attenuated twice.
 *
 * 2002-01-14/FK Further adjustments to key compression. It must now help
 *               to balance the normalisation of the output value in the
 *               tone generator.
 *
 * 2002-01-06/FK Changed the keycompression to count the number of depressed
 *               keys, rather than just look at the count of active
 *               oscillators. Truly, I do not know what causes this
 *               effect, but my best guess is a decreased signal level as
 *               more and more contacts close with the busbars. The assumption
 *               is of course that just as the line from the TG provides the
 *               bus with signal, its connection with earth also drops the
 *               voltage in the bus slightly, and many parallell connections
 *               only makes the situation worse.
 *
 * 2002-01-05/FK Fixed a bug in the osc-key assignment. 16' and 8' gave the
 *               the same generators.
 *
 *               Added KEYCOMPRESSION as a conditional compilation option.
 *               This simulates a volume drop when several keys on the
 *               manual are depressed simultaneously. Currently it is
 *               dependent on the number of oscillators which probably is
 *               not correct, since the effect very likely is a consequence of
 *               the number of connections to the buses.
 *
 * 2002-01-04/FK Tone generator upgraded to hold individual wave buffers.
 *               We do this in order to support varying volumes in the
 *               different generators. In the process we learned something
 *               interesting. Version 1 of the tonegenerator had 12 sample
 *               buffers, one for each note. Higher octave oscillators reused
 *               those buffers, but had position increments of 2, 4, 8 etc,
 *               thus playing it back at a higher rate. However, since the
 *               samples were the same all oscillators had the same volume.
 *               In order to compensate for this and give each oscillator
 *               its own output level, version 2 modified the oscillator
 *               setup code to give each oscillator its own buffer of three
 *               cycles, scaled in amplitude. This turned out to sound bad,
 *               because of looping clicks that gave rise to odd harmonics.
 *               To get rid of the unwanted artefacts version 3 made sure
 *               that each buffer had at least 512 samples. However, this
 *               only resulted in a click noise that had the same frequency
 *               in the oscillators were it occurred. Version 4, finally,
 *               went back to the version 1 solution, but rather than reusing
 *               the actual buffers in the lowest octave, version 4 made
 *               copies of them and scaled the amplitude of the copy. The
 *               clicks disappeared (almost).
 *
 *               Now, it may appear wasteful to cross a buffer at twice
 *               the original playback speed, because half or more of the
 *               samples will not be ever used. But, that is only true if
 *               the buffer length is even. If the buffer length is odd,
 *               the playback point will start on samples:
 *                               0, 1, 2, ... n-1, 0, 1, ...
 *               where n is the position increment (1, 2, 4, 8, ...).
 *               Thus, all oscillators with odd length buffers make full
 *               use of their buffers at all increments.
 *
 *               It is of course natural to consider ways in which ALL
 *               buffers could be utilized in this way. One way would be
 *               to force all buffers to be odd, but that is not good
 *               for certain frequencies.
 *
 *
 * 2001-11-30/FK Oscillator test using a sample buffer for the basic
 *               sinusoid and 9 individually updated read positions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <assert.h>

#include "main.h"
#include "tonegen.h"
#include "midi.h"
#include "vibrato.h"

/*
 * When HIPASS_PERCUSSION is defined it will do two things:
 *  - Insert a high-pass filter in the percussion signal (at the end of this
 *    file)
 *  - Initialise the variable percEnvScaling to a higher value to compensate
 *    for the psycho-acoustic drop in percussion volume when there is less
 *    bass in the signal.
 */

#define HIPASS_PERCUSSION


/* These are assertion support macros. */
/* In range? : A <= V < B  */
#define inRng(A,V,B) (((A) <= (V)) && ((V) < (B)))
/* Is a B a valid bus number? */
#define isBus(B) ((0 <= (B)) && ((B) < 9))
/* Is O a valid oscillator number? */
#define isOsc(O) ((0 <= (O)) && ((O) < 128))

#ifndef BUFFER_SIZE_SAMPLES
#define BUFFER_SIZE_SAMPLES 128
#endif

#define STRINGIFY(x) #x


/**
 * List element definition for the distribution network specification.
 * These are allocated during the configuration and initialization phase.
 */
typedef struct deflist_element {
  struct deflist_element * next;
  union {
    struct {
      float fa;
      float fb;
    } ff;
    struct {
      short sa;
      short sb;
      float fc;
    } ssf;
  } u;
} ListElement;

#define LE_HARMONIC_NUMBER_OF(LEP) ((LEP)->u.ssf.sa)
#define LE_HARMONIC_LEVEL_OF(LEP) ((LEP)->u.ssf.fc)

#define LE_WHEEL_NUMBER_OF(LEP) ((LEP)->u.ssf.sa)
#define LE_WHEEL_LEVEL_OF(LEP) ((LEP)->u.ssf.fc)

#define LE_TERMINAL_OF(LEP) ((LEP)->u.ssf.sa)
#define LE_BUSNUMBER_OF(LEP) ((LEP)->u.ssf.sb)
#define LE_TAPER_OF(LEP) ((LEP)->u.ssf.fc)
#define LE_LEVEL_OF(LEP) ((LEP)->u.ssf.fc)

/**
 * LE_BLOCKSIZE is the number ListElements we allocate in each call to
 * malloc.
 */

#define LE_BLOCKSIZE 200

/**
 * The leConfig pointer points to ListElements allocated during config.
 * The referenced memory is released once config is complete.
 */

static ListElement * leConfig = NULL;

/** The leRuntime pointer points to ListElements allocated for playing. */

static ListElement * leRuntime = NULL;

/**
 * Wheels refer to number of signal sources in the tonegenerator.
 * Note that the first one is numbered 1 and the last 91, so allocations
 * must add one.
 */
#define NOF_WHEELS 91

/**
 * Keys are numbered thus:
 *   0-- 63, upper manual (  0-- 60 in use)
 *  64--127, lower manual ( 64--124 in use)
 * 128--160, pedal        (128--159 in use)
 */
#define MAX_KEYS 160

/**
 * Buses are numbered like this:
 *  0-- 8, upper manual, ( 0=16',  8=1')
 *  9--17, lower manual, ( 9=16', 17=1')
 * 18--26, pedal         (18=32')
 */
#ifndef NOF_BUSES
#define NOF_BUSES 27		/* Should be in tonegen.h */
#endif /* NOF_BUSES */
#define UPPER_BUS_LO 0
#define UPPER_BUS_END 9
#define LOWER_BUS_LO 9
#define LOWER_BUS_END 18
#define PEDAL_BUS_LO 18
#define PEDAL_BUS_END 27

/**
 * Active oscillator table element.
 */
typedef struct aot_element {
  float busLevel[NOF_BUSES];	/* The signal level from wheel to bus  */
  int   keyCount[NOF_BUSES];	/* The nof keys adding up the level */
  int   refCount;		/* Reference count */
  float sumUpper;		/* The sum of the upper manual buses */
  float sumLower;		/* The sum of the lower manual buses */
  float sumPedal;		/* The sum of the pedal buses */
  float sumPercn;		/* The amount routed to percussion */
  float sumSwell;		/* The sum of U, L and P routed to swell */
  float sumScanr;		/* The sum of U, L and P routed to scanner */
  unsigned int flags;		/* House-holding bits */
} AOTElement;

/**
 * The Active Oscillator Table has one element (struct) for each wheel.
 * When a manual key is depressed, wheel, bus and gain data from the
 * keyContrib array is added to the corresponding rows (index by wheel)
 * and the sums are updated.
 */
static AOTElement aot[NOF_WHEELS + 1];

/**
 * The numbers of sounding oscillators/wheels are placed on this list.
 */
static int activeOscList[NOF_WHEELS + 1];
static int activeOscLEnd = 0; /**< end of activeOscList */

/* Keycompression */

#define KEYCOMPRESSION

#ifdef KEYCOMPRESSION
#define MAXKEYS 128
static float keyCompTable[MAXKEYS];
static int   keyDownCount = 0;
#define KEYCOMP_ZERO_LEVEL 1.0
#endif /* KEYCOMPRESSION */

/**
 * The size of the message queue.
 */
#define MSGQSZ 1024
static unsigned short   msgQueue [MSGQSZ]; /**< Message queue ringbuffer - MIDI->Synth */

static unsigned short * msgQueueWriter = msgQueue; /**< message-queue srite pointer */
static unsigned short * msgQueueReader = msgQueue; /**< message-queue read pointer */
static unsigned short * msgQueueEnd = &(msgQueue[MSGQSZ]);

/*
 * The message layout is:
 *
 * 15 14 13 12  11 10  9  8  7  6  5  4  3  2  1  0
 * [ Message  ] [             Parameter           ]
 * [0  0  0  0] [            Key number           ]    Key off
 * [0  0  0  1] [            Key number           ]    Key on
 *
 */

/* Message field access macros */
#define MSG_MMASK 0xf000
#define MSG_PMASK 0x0fff
/* Retrive message part from a message */
#define MSG_GET_MSG(M) ((M) & MSG_MMASK)
/* Retrieve parameter part from a message */
#define MSG_GET_PRM(M) ((M) & MSG_PMASK)
/* Messages */
#define MSG_MKEYOFF 0x0000
#define MSG_MKEYON  0x1000
/* Key released message, arg is keynumber */
#define MSG_KEY_OFF(K) (MSG_MKEYOFF | ((K) & MSG_PMASK))
/* Key depressed message, arg is keynumber */
#define MSG_KEY_ON(K)  (MSG_MKEYON  | ((K) & MSG_PMASK))



/**
 * This is the instruction format for the core generator.
 */
typedef struct _coreins {
  short   opr;			/**< Instruction */
  int     cnt;			/**< Sample count */
  size_t  off;			/**< Target offset */
  float * src;			/**< Pointer to source buffer */
  float * env;			/**< Pointer to envelope array */
  float * swl;			/**< Pointer into swell buffer */
  float * prc;			/**< Pointer into percussion buffer */
  float * vib;			/**< Pointer into vibrato buffer */
  float sgain;			/**< Gain into swell buffer */
  float nsgain;			/**< Next sgain */
  float pgain;			/**< Gain into percussion buffer */
  float npgain;			/**< Next pgain */
  float vgain;			/**< Gain into vibrato buffer */
  float nvgain;			/**< Next vgain */
} CoreIns;

/*
 * Fixed-point arithmetic - mantissa of 10 bits.
 * ENV_BASE is the scaling applied to the attack and release envelope curves
 * for key-click.
 * ENV_NORM is number of bits of right-shift needed to normalize the result:
 * y[i] = (x[i] * e[j] * ENV_BASE) >> ENV_NORM
 */

#define ENV_NORM 10
#define ENV_BASE (1 << ENV_NORM)

/* Core instruction codes (opr field in struct _coreins). */

#define CR_CPY    0		/* Copy instruction */
#define CR_ADD    1		/* Add instruction */
#define CR_CPYENV 2		/* Copy via envelope instruction */
#define CR_ADDENV 3		/* Add via envelope instruction */

#define CR_PGMMAX 256		/* Max length of a core program */

/*
 * 2002-11-17/FK: It may be prudent to remember that we do not check
 * for overflow. 256/18=14 keys. Worst case scenario should be
 * 128 * 9 * 2 = 2304. Does that length hurt performance? Probably not
 * as much as an overrun will.
 */

static CoreIns   corePgm[CR_PGMMAX];
static CoreIns * coreWriter;
static CoreIns * coreReader;

/* Attack and release buffer envelopes for 9 buses. */

static float attackEnv[9][BUFFER_SIZE_SAMPLES]; /**< Attack envelope buffer for 9 buses */
static float releaseEnv[9][BUFFER_SIZE_SAMPLES];/**< Release envelope buffer for 9 buses */


static int envAttackModel  = ENV_CLICK;
static int envReleaseModel = ENV_LINEAR;
static float envAttackClickLevel  = 0.50;
static float envReleaseClickLevel = 0.25;
/** Minimum random length (in samples) of attack key click noise burst. */
static int envAtkClkMinLength = -1;  //  8 @ 22050
/** Maximum random length (in samples) of attack key click noise burst. */
static int envAtkClkMaxLength = -1;   // 40 @ 22050

/**
 * There is one oscillator struct for each frequency.
 * The wave pointer points to a 16-bit PCM loop which contains the fundamental
 * frequency and harmonics of the tonewheel.
 */
struct _oscillator {

  float * wave;			/**< Pointer to tonewheel 'sample' */

  size_t  lengthSamples;	/**< Nof samples in wave */
  double  frequency;		/**< The frequency (Hertz) */
  double  attenuation;		/**< Signal level (0.0 -- 1.0) */
  int     pos;			/**< Read position */

  int     aclPos;		/**< Position in active list */
  unsigned short rflags;	/**< Rendering flags */

};

/* Rendering flag bits */
#define ORF_MODIFIED 0x0004
#define ORF_ADDED    0x0002
#define ORF_REMOVED  0x0001
/* Composite flag bits */
#define OR_ADD 0x0006
#define OR_REM 0x0005

static unsigned int newRouting = 0;
static unsigned int oldRouting = 0;

#define RT_PERC2ND 0x08
#define RT_PERC3RD 0x04
#define RT_PERC    0x0C
#define RT_UPPRVIB 0x02
#define RT_LOWRVIB 0x01
#define RT_VIB     0x03

static unsigned int percSendBus = 4; /* 3 or 4 */
static unsigned int percSendBusA = 3;
static unsigned int percSendBusB = 4;

static unsigned int upperKeyCount = 0;

/**
 * A matrix of these structs is used in the initialization stage.
 * The matrix is indexed by MIDI note nr and bus number. Each element
 * thus models the connection between a tonegenerator and a bus.
 * Whole or part of the matrix can be loaded during configuration, while
 * uninitialized elements are always set to default values.
 * We load or compute MIDI->oscillator, foldback and tapering.
 * The information is then used to create the wave buffers and
 * runtime version of the keyOsc and connections arrays.
 */
typedef struct _tmbassembly {	/**< Tone generator, Manual and Bus assembly */
  float          taper;		/**< Taper value in dB */
  unsigned char  oscNr;		/**< Oscillator number */
  unsigned char  inited;	/**< 0 = Element not set, 1 = set */
  unsigned char  waveSlot;	/**< Index into oscillator's wavetable */
} TMBAssembly, * TMBAssemblyPtr;

/**
 * Connection descriptor structure. This structure is the runtime description
 * of the connection between a resistance wire and a bus.
 */
typedef struct _connection {
  unsigned char osc;		/**< Oscillator that provides the wire */
  unsigned char tpx;		/**< Taper index in that oscillator */
  unsigned char bus;		/**< Bus number */
  unsigned short acx;		/**< Position in list of active connections */
} Connection, * ConnectionPtr;

/**
 * Swell pedal (volume control)
 */
static float swellPedalGain = 0.07;

/**
 * Output level trim. Used to trim the overall output level.
 */
static float outputLevelTrim = 0.05011872336272722;

/**
 * Our master tuning frequency.
 */
static double tuning = 440.0;

/**
 * When gearTuning is FALSE, the tuning is equal-tempered.
 * When TRUE, the tuning is based on the integer ratio approximations found
 * in the mechanical tone generator of the original instrument. The resulting
 * values are very close to the real thing.
 */
static int gearTuning = 1;

/*
 * Not all of these are used.
 */
static struct _oscillator oscillators [NOF_WHEELS + 1];

/*
 * Vector of active keys, used to correctly manage
 * sounding and non-sounding keys.
 */
static unsigned int activeKeys [MAX_KEYS];

/**
 * The array drawBarGain holds the instantaneous amplification value for
 * each of the drawbars.
 */
static float drawBarGain[NOF_BUSES];

/**
 * The drawBarLevel table holds the possible drawbar amplification values
 * for all drawbars and settings. When a drawbar change is applied, the
 * appropriate value is copied from the table and installed in drawBarGain[].
 */
static float drawBarLevel[NOF_BUSES][9];

/**
 * The drawBarChange flag is set by the routine that effectuates a drawbar
 * change. The oscGenerateFragment routine then checks the flag, computes
 * new composed gain values, and resets the flag.
 */
static unsigned short drawBarChange = 0;

/**
 * True when percussion is enabled.
 */
static int percEnabled = FALSE;

/**
 * With percussion enabled, the output from the this bus is muted, to
 * simulate its operation as the percussion trigger. A value of -1
 * disables the muting feature.
 */
static int percTriggerBus = 8;

/**
 * While percussion AND trigger bus muting are active, this variable
 * holds the drawbar setting to restore to the trigger bus once
 * percussion is disabled.
 */
static int percTrigRestore = 0;

static int percIsSoft;		/**< Runtime toggle */
static int percIsFast;		/**< Runtime toggle */

static double percFastDecaySeconds = 1.0; /**< Config parameter */
static double percSlowDecaySeconds = 4.0; /**< Config parameter */


/* Alternate percussion 25-May-2003/FK */

/**
 * Runtime: The instantaneous gain applied to the percussion signal.
 */
static float percEnvGain;

/**
 * Runtime: The starting value of percEnvGain when all keys are released.
 */
static float percEnvGainReset;

/**
 * Runtime: The percEnvGain value is updated by being multiplied by this
 * variable between each sample. The value is close to, but less than 1.
 */
static float percEnvGainDecay;

/**
 * Config: scaling factor used to raise the percussion gain reset level
 * to audible levels. Can be viewed as overall 'volume' for percussion.
 */
#ifdef HIPASS_PERCUSSION
static float percEnvScaling = 11.0;
#else
static float percEnvScaling =  3.0;
#endif /* HIPASS_PERCUSSION */

/**
 * Runtime: Normal volume reset value, copied to percEnvGainReset when
 * normal percussion is selected.
 */
static float percEnvGainResetNorm     = 1.0;

/**
 * Runtime: Soft volume reset value, copied to percEnvGainReset when
 * soft percussion is selected.
 */
static float percEnvGainResetSoft     = 0.5012;

/**
 * Runtime: Fast and Normal decay multiplier, copied to percEnvGainDecay
 * when that combination is selected.
 */
static float percEnvGainDecayFastNorm = 0.9995; /* Runtime select */

/**
 * Runtime: Fast and Soft decay multiplier, copied to percEnvGainDecay
 * when that combination is selected.
 */
static float percEnvGainDecayFastSoft = 0.9995; /* Runtime select */

/**
 * Runtime: Slow and Normal decay multiplier, copied to percEnvGainDecay
 * when that combination is selected.
 */
static float percEnvGainDecaySlowNorm = 0.9999; /* Runtime select */

/**
 * Runtime: Slow and Soft decay multiplier, copied to percEnvGainDecay
 * when that combination is selected.
 */
static float percEnvGainDecaySlowSoft = 0.9999; /* Runtime select */

/**
 * Drawbar gain when the percussion NORMAL/SOFT is in the NORMAL position.
 */
static float percDrawbarNormalGain = 0.60512;

/**
 * Drawbar gain when the percussion NORMAL/SOFT is in the SOFT position.
 */
static float percDrawbarSoftGain = 1.0;

/**
 * Runtime drawbar gain.
 */
static float percDrawbarGain = 1.0;

/**
 * This variable determines the model simulated by the tonegenerator
 * and the keyboard wiring.
 */
static int tgVariant = TG_91FB12;

/**
 * This variable determines the precision with which the wave buffers
 * for the tonegenerator are sized and created.
 */
static double tgPrecision = 0.001;

/*
 * Equalisation macro selection.
 */
#define EQ_SPLINE 0
#define EQ_PEAK24 1		/* Legacy */
#define EQ_PEAK46 2		/* Legacy */

static int eqMacro = EQ_SPLINE;
static double eqvCeiling = 1.0;	/**< Normalizing manual osc eq. */
static double eqvAtt [128];	/**< Values from config file */
static char   eqvSet [128];	/**< Value is set by a config command */

/*
 * EQ_SPLINE parameters.
 */
static double eqP1y =  1.0;	/* Default is flat */
static double eqR1y =  0.0;
static double eqP4y =  1.0;
static double eqR4y =  0.0;

/**
 * Default value of crosstalk between tonewheels in the same compartment.
 * The value refers to the amount of rogue signal picked up.
 */
static double defaultCompartmentCrosstalk = 0.01; /* -40 dB */

/**
 * Default value of crosstalk between transformers on the top of the tg.
 */
static double defaultTransformerCrosstalk = 0.0;

/**
 * Default value of crosstalk between connection on the terminal strip.
 */
static double defaultTerminalStripCrosstalk = 0.01; /* -40 db */

/**
 * Default throttle on the crosstalk distribution model for wiring.
 */

static double defaultWiringCrosstalk = 0.01; /* -40 dB */

/**
 * Signals weaker than this are not put on the contribution list.
 */

static double contributionFloorLevel = 0.0000158;

/**
 * If non-zero, signals that ARE placed on the contribution have at least
 * this level.
 */

static double contributionMinLevel = 0.0;

/*
 * Default amplitudes of tonewheel harmonics
 */
#define MAX_PARTIALS 12
static double wheel_Harmonics [MAX_PARTIALS] = { 1.0 }; /** < amplitudes of tonewheel harmonics */


/**
 * The wheelHarmonics array is filled in during configuration. The list in
 * index 0 is used as default for non-configured slots.
 * First wheel number is 1, last is 91, hence 92 elements are needed.
 */
static ListElement * wheelHarmonics[NOF_WHEELS + 1];

/**
 * The terminalMix array is filled in during configuration. For each terminal
 * on the tonegenerator is describes the mix between wheels heard on the
 * terminal, and is thus responsible for the crosstalk generated in and on
 * the tonegenerator. The identity value should perhaps be assumed?
 */
static ListElement * terminalMix[NOF_WHEELS + 1];

/**
 * The keyTaper array is filled in during configuration. For each key and
 * bus contact in the key it describes the tonegenerator terminal connected
 * to it and the taper level.
 */
static ListElement * keyTaper[MAX_KEYS];

/* These units are in dB */

#define taperMinusThree -10.0
#define taperMinusTwo    -7.0
#define taperMinusOne    -3.5
#define taperReference    0.0
#define taperPlusOne      3.5
#define taperPlusTwo      7.0

/**
 * The keyCrosstalk array is filled in during configuration. For each key
 * and bus contact in the key it describes the tonegenerator terminal also
 * heard in addition to the specification in the keyTaper array.
 */
static ListElement * keyCrosstalk[MAX_KEYS];

/**
 * The keyContrib array is loaded by routine compilePlayMatrix and is used
 * by the sound runtime to add or remove contribution from wheels to buses
 * as controlled by each key.
 */
static ListElement * keyContrib[MAX_KEYS];

/**
 * Gear ratios for a 60 Hertz organ.
 */
static double gears60ratios [12][2] = {
  {85, 104},			/* c  */
  {71,  82},			/* c# */
  {67,  73},			/* d  */
  {35,  36},			/* d# */
  {69,  67},			/* e  */
  {12,  11},			/* f  */
  {37,  32},			/* f# */
  {49,  40},			/* g  */
  {48,  37},			/* g# */
  {11,   8},			/* a  */
  {67,  46},			/* a# */
  {54,  35}			/* h  */
};

/**
 * Gear ratios for a 50 Hertz organ (estimated).
 */
static double gears50ratios [12][2] = {
  {17, 26},			/* c  */
  {57, 82},			/* c# */
  {11, 15},			/* d  */
  {49, 63},			/* d# */
  {33, 40},			/* e  */
  {55, 63},			/* f  */
  {49, 53},			/* f# */
  {49, 50},			/* g  */
  {55, 53},			/* g# */
  {11, 10},			/* a  */
  { 7,  6},			/* a# */
  {90, 73}			/* h  */
};

/**
 * This table is indexed by frequency number, i.e. the tone generator number
 * on the 91 oscillator generator. The first frequency/generator is numbered 1.
 */
static short wheelPairs [92] = {
  0,				/* 0: not used */
  49, 50, 51, 52,  53, 54, 55, 56,  57, 58, 59, 60, /* 1-12 */
  61, 62, 63, 64,  65, 66, 67, 68,  69, 70, 71, 72, /* 13-24 */
  73, 74, 75, 76,  77, 78, 79, 80,  81, 82, 83, 84, /* 25-36 */
  0,  0,  0,  0,   0,  85, 86, 87,  88, 89, 90, 91, /* 37-48 */
  1,  2,  3,  4,   5,  6,  7,  8,   9,  10, 11, 12, /* 49-60 */
  13, 14, 15, 16,  17, 18, 19, 20,  21, 22, 23, 24, /* 61-72 */
  25, 26, 27, 28,  29, 30, 31, 32,  33, 34, 35, 36, /* 73-84 */
  42, 43, 44, 45,  46, 47, 48	/* 85-91 */
};

/*
 * These two arrays describes two rows of transformers mounted on top of
 * the tonegenerator. The concepts of north and south are simply used
 * to avoid confusion with upper and lower (manuals).
 */

/**
 * description of rows of transformers mounted on top of
 * the tonegenerator for the upper (north) manual
 */
static short northTransformers [] = {
  85, 66, 90, 71, 47, 64, 86, 69, 45, 62, 86, 67, 91, 72, 48, 65, 89, 70,
  46, 63, 87, 68, 44, 61,
  0
};

/**
 * description of rows of transformers mounted on top of
 * the tonegenerator for the lower (south) manual
 */
static short southTransformers [] = {
  78, 54, 83, 59, 76, 52, 81, 57, 74, 50, 79, 55, 84, 60, 77, 53, 82, 58,
  75, 51, 80, 56, 73, 49,
  0
};


/**
 * This array describes how oscillators are arranged on the terminal
 * soldering strip.
 */
static short terminalStrip [] = {
  85, 42, 30, 76, 66, 18,  6, 54, 90, 35, 83, 71, 23, 11, 59, 47, 40,
  28, 76, 64, 16,  4, 52, 88, 33, 81, 69, 21,  9, 57, 45, 34, 26, 74,
  62, 14,  2, 50, 86, 43, 31, 79, 67, 19,  7, 55, 91, 36, 84, 72, 24,
  12, 60, 48, 41, 29, 77, 65, 17,  5, 53, 89, 34, 82, 70, 22, 10, 58,
  46, 39, 27, 75, 63, 15,  3, 51, 87, 32, 80, 68, 20,  8, 56, 44, 37,
  25, 73, 61, 13,  1, 49,
  0
};

/*
 * ================================================================
 */

/**
 * This function converts from a dB value to a fraction of unit gain.
 * Both values describe the relation between two levels.
 */
double dBToGain (double dB) {
  return pow (10.0, (dB / 20.0));
}

/**
 * Return a random double in the range 0-1.
 */
double drnd () {
  return ((double) rand ()) / (double) RAND_MAX;
}

/**
 * Returns a new list element following the chain indicated by the
 * supplied block pointer. When no ListElement can be immediately
 * provided, a new block is allocated with LE_BLOCKSIZE elements. The
 * first element in each block is used to indicate the next block
 * in the list of allocated blocks.
 * The second element in the very first block is used as the start of
 * the free list. Memory block allocations add to this list and
 * ListElement requests (calls to this function) pick elements off
 * the list.
 *
 * @param pple Pointer to the pointer that indicates the start of the
 *             chain of allocated blocks for ListElement.
 */
static ListElement * newListElement (ListElement ** pple) {
  int mustAllocate = 0;
  ListElement * rtn = NULL;

  if ((*pple) == NULL) {
    mustAllocate = 2;		/* Allocate and init */
  }
  else {
    if (((*pple)[1]).next == NULL) { /* Check free list */
      mustAllocate = 1;		/* Free list is empty */
    }
  }

  if (0 < mustAllocate) {
    int i;
    int freeElements = 0;
    ListElement * frp = NULL;
    ListElement * lep =
      (ListElement *) malloc (sizeof (ListElement) * LE_BLOCKSIZE);

    if (lep == NULL) {
      fprintf (stderr, "FATAL: memory allocation failed in ListElement\n");
      exit (2);
    }

    lep->next = NULL;		/* Init list of allocated blocks */
    freeElements = LE_BLOCKSIZE - 1;

    if (mustAllocate == 2) {	/* First block preparation */
      *pple = lep;
      lep[1].next = NULL;	/* Block list is empty */
      frp = &(lep[2]);		/* First free element in first block */
      freeElements -= 1;	/* Decr one for free list */
    }
    else {
      lep->next = (*pple)->next; /* Link in in block chain */
      (*pple)->next = lep;
      frp = &(lep[1]);		/* First free element in subsequent blocks */
    }

    (*pple)[1].next = frp;

    /* Load elements into the free list */
    for (i = 0; i < (freeElements - 1); i++) {
      frp->next = frp + 1;
      frp = frp->next;
    }
    frp->next = NULL;
  }

  /* Return the next element off the free list. */

  rtn = (*pple)[1].next;
  (*pple)[1].next = rtn->next;
  rtn->next = NULL;

  return rtn;
} /* newListElement */

/**
 * Allocates and returns a new configuration list element.
 */
static ListElement * newConfigListElement () {
  return newListElement (&leConfig);
}

/**
 * Allocates and returns a new runtime list element.
 */
static ListElement * newRuntimeListElement () {
  return newListElement (&leRuntime);
}

/**
 * Appends a list element to the end of a list.
 */
static ListElement * appendListElement (ListElement ** pple, ListElement * lep)
{
  if ((*pple) == NULL) {
    (*pple) = lep;
  }
  else {
    appendListElement (&((*pple)->next), lep);
  }
  return lep;
}

/**
 * This routine sets the tonegenerator model. The call must be made
 * before calling initToneGenerator() to have effect and is thus the
 * target for startup configuration values.
 */
void setToneGeneratorModel (int variant) {
  switch (variant) {
  case TG_91FB00:
  case TG_82FB09:
  case TG_91FB12:
    tgVariant = variant;
    break;
  }
}

/**
 * This routine sets the tonegenerator's wave precision. The call must
 * be made before calling initToneGenerator() to have effect. It is the
 * target of startup configuration values.
 */
void setWavePrecision (double precision) {
  if (0.0 < precision) {
    tgPrecision = precision;
  }
}

/**
 * Sets the tuning.
 */
void setTuning (double refA_Hz) {
  if ((220.0 <= refA_Hz) && (refA_Hz <= 880.0)) {
    tuning = refA_Hz;
  }
}

/**
 * This function provides the default tapering model for the upper and
 * lower manuals.
 */
static double taperingModel (int key, int bus) {
  double tapering = taperReference;

  switch (bus) {

  case 0:			/* 16 */
    if (key < 12) {	/* C-1 */
      tapering = taperMinusThree;
    }
    else if (key < 17) {	/* F-1 */
      tapering = taperMinusTwo;
    }
    else if (key < 24) {	/* C0 */
      tapering = taperMinusOne;
    }
    else if (key < 36) {	/* C1 */
      tapering = taperReference;
    }
    else if (key < 48) {	/* C2 */
      tapering = taperPlusOne;
    }
    else {
      tapering = taperPlusTwo;
    }
    break;

  case 1:			/* 5 1/3 */
    if (key < 15) {	/* Eb-1 */
      tapering = taperMinusOne;
    }
    else if (key < 38) {	/* D#1 */
      tapering = taperReference;
    }
    else if (key < 50) {	/* D#2 */
      tapering = taperPlusOne;
    }
    else {
      tapering = taperPlusTwo;
    }
    break;

  case 2:			/* 8 */
    if (key < 17) {	/* F-1 */
      tapering = taperMinusTwo;
    }
    else if (key < 22) {	/* A#-1 */
      tapering = taperMinusOne;
    }
    else if (key < 37) {	/* C#1 */
      tapering = taperReference;
    }
    else if (key < 49) {	/* C2 */
      tapering = taperPlusOne;
    }
    else {
      tapering = taperPlusTwo;
    }
    break;

  case 3:			/* 4 */
    if (key < 17) {	/* F-1 */
      tapering = taperMinusOne;
    }
    else if (key < 39) {	/* C0 */
      tapering = taperReference;
    }
    else {
      tapering = taperMinusOne;
    }
    break;

  case 4:			/* 2 2/3 */
    if (key < 14) {
      tapering = taperPlusTwo;
    }
    else if (key < 20) {
      tapering = taperPlusOne;
    }
    else if (key < 40) {
      tapering = taperReference;
    }
    else if (key < 50) {
      tapering = taperMinusOne;
    }
    else {
      tapering = taperMinusTwo;
    }
    break;

  case 5:			/* 2 */
    if (key < 12) {
      tapering = taperPlusTwo;
    }
    else if (key < 15) {
      tapering = taperPlusOne;
    }
    else if (key < 41) {
      tapering = taperReference;
    }
    else if (key < 54) {
      tapering = taperMinusOne;
    }
    else {
      tapering = taperMinusTwo;
    }
    break;

  case 6:			/* 1 3/5 */
    if (key < 14) {
      tapering = taperPlusOne;
    }
    else if (key < 42) {
      tapering = taperReference;
    }
    else if (key < 50) {
      tapering = taperMinusOne;
    }
    else {
      tapering = taperMinusTwo;
    }
    break;

  case 7:			/* 1 1/3 */
    if (key < 43) {
      tapering = taperReference;
    }
    else if (key < 48) {
      tapering = taperMinusOne;
    }
    else {
      tapering = taperMinusTwo;
    }
    break;

  case 8:			/* 1 */
    if (key < 43) {
      tapering = taperReference;
    }
    else {
      tapering = taperMinusTwo;
    }
    break;
  }

  return dBToGain (tapering);
}

/**
 * Applies the built-in default model to the manual tapering and crosstalk.
 */
static void applyManualDefaults (int keyOffset, int busOffset) {
  int k;
  /* Terminal number distances between buses. */
  int ULoffset[9] = {-12, 7, 0,  12, 19, 24, 28,  31, 36};
  int ULlowerFoldback = 13;
  int ULupperFoldback = 91;
  int leastTerminal = 1;
  ListElement * lep;

  /*
   * In the original instrument, C-based and A-based generators both
   * numbered the first tonegenerator terminal '1'. This becomes impractical
   * when computing terminal numbers, so what we do here is to adhere to
   * the C-based enumeration of the terminals and introduce the variable
   * leastTerminal to indicate the lowest terminal number available. For
   * an A-based generator, the least terminal is thus 10.
   * (A tonegenerator 'terminal' refers to the location on the physical
   *  tonegenerator where the signal from a tonewheel and pickup is made
   *  available. Wires lead from that point to contacts in the manuals.
   *  On later models, these wires have resistance, adjusting the signal
   *  level as a function of key and bus to implement a gross pre-equalization
   *  across the manuals.)
   */

  switch (tgVariant) {
  case TG_91FB00:
    ULlowerFoldback = 1;	/* C-based generator, no foldback */
    leastTerminal = 1;
    break;
  case TG_82FB09:
    ULlowerFoldback = 10;	/* A-based generator, foldback */
    leastTerminal = 10;
    break;
  case TG_91FB12:
    ULlowerFoldback = 13;	/* C-based generator, foldback */
    leastTerminal = 1;
    break;
  }

  for (k = 0; k <= 60; k++) {	/* Iterate over 60 keys */
    int keyNumber = k + keyOffset; /* Determine the key's number */
    if (keyTaper[keyNumber] == NULL) { /* If taper is unset */
      int b;
      for (b = 0; b < 9; b++) {	/* For each bus contact */
	int terminalNumber;
	terminalNumber = (k + 13) + ULoffset[b]; /* Ideal terminal */
	/* Apply foldback rules */
	while (terminalNumber  < leastTerminal)   { terminalNumber += 12;}
	while (terminalNumber  < ULlowerFoldback) { terminalNumber += 12;}
	while (ULupperFoldback < terminalNumber)  { terminalNumber -= 12;}

	lep = newConfigListElement ();
	LE_TERMINAL_OF(lep) = (short) terminalNumber;
	LE_BUSNUMBER_OF(lep) = (short) (b + busOffset);
	LE_LEVEL_OF(lep) = (float) taperingModel (k, b);

	appendListElement (&(keyTaper[keyNumber]), lep);
      }
    }
  }
} /* applyManualDefaults */

/**
 * Wires up the pedals. Each contact is given the signal at reference
 * level, but that may be wrong. We also have no foldback.
 *
 * @param nofPedals The number of pedals to enable.
 */
static void applyPedalDefaults (int nofPedals) {
  int k;
  int PDoffset[9] = {-12, 7, 0, 12, 19, 24, 28, 31, 36};
  ListElement * lep;

  assert (nofPedals <= 128);

  for (k = 0; k < nofPedals; k++) {
    int keyNumber = k + 128;
    if (keyTaper[keyNumber] == NULL) {
      int b;
      for (b = 0; b < 9; b++) {
	int terminalNumber;
	terminalNumber = (k + 13) + PDoffset[b];
	if (terminalNumber < 1) continue;
	if (91 < terminalNumber) continue;
	lep = newConfigListElement ();
	LE_TERMINAL_OF(lep)  = (short) terminalNumber;
	LE_BUSNUMBER_OF(lep) = (short) (b + PEDAL_BUS_LO);
	LE_LEVEL_OF(lep)     = (float) dBToGain (taperReference);
	appendListElement (&(keyTaper[keyNumber]), lep);
      }
    }
  }
} /* applyPedalDefaults */

/**
 * The default crosstalk model.
 * The model is based on the vertical arrangement of keys underneath each key.
 * To the signal wired to each bus (contact) we add the signals wired into
 * the vertically neighbouring contacts, and divide by distance.
 */
static void applyDefaultCrosstalk (int keyOffset, int busOffset) {
  int k;
  int b;

  for (k = 0; k <= 60; k++) {
    int keyNumber = k + keyOffset;
    if (keyCrosstalk[keyNumber] == NULL) {
      for (b = 0; b < 9; b++) {
	int busNumber = busOffset + b;
	ListElement * lep;
	for (lep = keyTaper[keyNumber]; lep != NULL; lep = lep->next) {
	  if (LE_BUSNUMBER_OF(lep) == busNumber) {
	    continue;
	  }
	  ListElement * nlep = newConfigListElement ();
	  LE_TERMINAL_OF(nlep) = LE_TERMINAL_OF(lep);
	  LE_BUSNUMBER_OF(nlep) = busNumber;
	  LE_LEVEL_OF(nlep) =
	    (defaultWiringCrosstalk * LE_LEVEL_OF(lep))
	    /
	    fabs(busNumber - LE_BUSNUMBER_OF(lep));
	  appendListElement (&(keyCrosstalk[keyNumber]), nlep);
	}
      }
    }
  }
}

/**
 * Find east-west neighbours.
 */
static int findEastWestNeighbours (short * v, int w, int * ep, int * wp) {
  int i;

  assert (v  != NULL);
  assert (ep != NULL);
  assert (wp != NULL);

  *ep = 0;
  *wp = 0;

  for (i = 0; 0 < v[i]; i++) {
    if (v[i] == (short) w) {
      if (0 < i) {
	*ep = v[i - 1];
      }
      *wp = v[i + 1];
      return 1;
    }
  }

  return 0;
}

/**
 * Auxilliary function to applyDefaultConfiguration.
 */
static void findTransformerNeighbours (int w, int * ep, int * wp) {
  if (findEastWestNeighbours (northTransformers, w, ep, wp)) {
    return;
  }
  else if (findEastWestNeighbours (southTransformers, w, ep, wp)) {
    return;
  }
  else {
    assert (0);
  }
}

/**
 * This routine applies default models to the configuration.
 */
static void applyDefaultConfiguration () {
  int i;
  ListElement * lep;

  /* Crosstalk at the terminals. Terminal mix. */



  for (i = 1; i <= NOF_WHEELS; i++) {
    if (terminalMix[i] == NULL) {
      lep = newConfigListElement ();
      LE_WHEEL_NUMBER_OF(lep) = (short) i;
      LE_WHEEL_LEVEL_OF(lep) = 1.0 - defaultCompartmentCrosstalk;
      appendListElement (&(terminalMix[i]), lep);
      if (0.0 < defaultCompartmentCrosstalk) {
	if (0 < wheelPairs[i]) {
	  lep = newConfigListElement ();
	  LE_WHEEL_NUMBER_OF(lep) = wheelPairs[i];
	  LE_WHEEL_LEVEL_OF(lep) = defaultCompartmentCrosstalk;
	  appendListElement (&(terminalMix[i]), lep);
	}
      }
    }
  }

  /*
   * The transformer and terminal strip crosstalk computations below are
   * not immediately correct, since only include the primary wheel.
   * That is only correct if compartment crosstalk is zero. If not, the
   * contribution from each neighbour should in reality be the compartment
   * mix of that neighbour.
   */

  if (0.0 < defaultTransformerCrosstalk) {

    for (i = 44; i <= NOF_WHEELS; i++) {
      int east = 0;
      int west = 0;

      findTransformerNeighbours (i, &east, &west);

      if (0 < east) {
	lep = newConfigListElement ();
	LE_WHEEL_NUMBER_OF(lep) = (short) east;
	LE_WHEEL_LEVEL_OF(lep) = defaultTransformerCrosstalk;
	appendListElement (&(terminalMix[i]), lep);
      }

      if (0 < west) {
	lep = newConfigListElement ();
	LE_WHEEL_NUMBER_OF(lep) = (short) west;
	LE_WHEEL_LEVEL_OF(lep) = defaultTransformerCrosstalk;
	appendListElement (&(terminalMix[i]), lep);
      }
    }

  } /* if defaultTransformerCrosstalk */

  if (0.0 < defaultTerminalStripCrosstalk) {
    for (i = 1; i <= NOF_WHEELS; i++) {
      int east = 0;
      int west = 0;
      findEastWestNeighbours (terminalStrip, i, &east, &west);

      if (0 < east) {
	lep = newConfigListElement ();
	LE_WHEEL_NUMBER_OF(lep) = (short) east;
	LE_WHEEL_LEVEL_OF(lep) = defaultTerminalStripCrosstalk;
	appendListElement (&(terminalMix[i]), lep);
      }

      if (0 < west) {
	lep = newConfigListElement ();
	LE_WHEEL_NUMBER_OF(lep) = (short) west;
	LE_WHEEL_LEVEL_OF(lep) = defaultTerminalStripCrosstalk;
	appendListElement (&(terminalMix[i]), lep);
      }

    }
  } /* if defaultTerminalStripCrosstalk */

  /* Key connections and taper */

  applyManualDefaults ( 0, 0);
  applyManualDefaults (64, 9);
  applyPedalDefaults (32);

  /* Key crosstalk */
  applyDefaultCrosstalk ( 0, 0);
  applyDefaultCrosstalk (64, 9);

  /*
   * As yet there is no default crosstalk model for pedals, but they will
   * still benefit from the crosstalk modelled for the tonegenerator
   * terminals.
   */

} /* applyDefaultConfiguration */

/**
 * Support function for function compilePlayMatrix() below.
 * A list element is provided that connects a bus with a terminal using a
 * specific gain; ie the element represents a key contact and a signal path
 * to a tonegenerator terminal, by tapering wire or crosstalk induction.
 * This function resolves the terminal address into the oscillators that
 * leave their signals there. For each such oscillator (wheel) we accumulate
 * its contribution in a dynamic matrix where there is a row for each wheel
 * number and column for each bus to which the wheel contributes.
 * The matrix describes a single key each time it is used.
 *
 * @param lep         List element to insert.
 * @param cpmBus      For each row, describes the bus numbers in the row.
 * @param cpmGain     For each wheel and bus, the gain fed to the bus.
 * @param wheelNumber The wheel number for the row.
 * @param rowLength   The nof columns in each row.
 * @param endRowp
 */
static void cpmInsert (const ListElement * lep,
		       unsigned char cpmBus[NOF_WHEELS + 1][NOF_BUSES],
		       float cpmGain[NOF_WHEELS][NOF_BUSES],
		       short wheelNumber[NOF_WHEELS + 1],
		       short rowLength[NOF_WHEELS],
		       int * endRowp)
{
  int endRow = *endRowp;
  int terminal = LE_TERMINAL_OF(lep);
  unsigned char bus = (unsigned char) LE_BUSNUMBER_OF(lep);
  int r;
  int c;
  int b;
  ListElement * tlep;

  for (tlep = terminalMix[terminal]; tlep != NULL; tlep = tlep->next) {
    float gain = LE_WHEEL_LEVEL_OF(tlep) * LE_LEVEL_OF(lep);
    short wnr = LE_WHEEL_NUMBER_OF(tlep);

    if (gain == 0.0) continue;

    /* Search for the wheel's row */

    wheelNumber[endRow] = wnr;	/* put the wanted wheel in the next free row */
    for (r = 0; wheelNumber[r] != wnr; r++); /* find the row where it is */
    if (r == endRow) {		/* if this is a new wheel */
      rowLength[r] = 0;		/* initialise the column count */
      endRow += 1;		/* and increment the row count */
    }

    /* Search for bus column */

    c = rowLength[r];		/* c = first unused column */
    cpmBus[r][c] = bus; /* put bus nr in next free column */
    for (b = 0; cpmBus[r][b] != bus; b++);	/* b finds the column */
    if (b == c) {		/* if this is a new bus number ... */
      rowLength[r] += 1;	/* ... add a column to this row */
      cpmGain[r][b] = gain;	/* and set gain */
    }
    else {
      cpmGain[r][b] += gain;	/* else add gain */
    }

  }

  *endRowp = endRow;
}

/**
 * This function assembles the information in the configuration lists to
 * the play matrix, a data structure used by the runtime sound production
 * code. The play matrix encodes for each key, the wheels that are heard
 * on each bus, and the level with which they reach it.
 *
 * This function also contains the transition from configuration list elements
 * to runtime list elements.
 */
static void compilePlayMatrix () {

  static unsigned char cpmBus [NOF_WHEELS + 1][NOF_BUSES];
  static float cpmGain [NOF_WHEELS][NOF_BUSES];

  short wheelNumber[NOF_WHEELS + 1]; /* For blind tail-insertion */
  short rowLength[NOF_WHEELS];
  int endRow;
  int k;
  int w;
  int sortMode = 0;

  /* For each playing key */
  for (k = 0; k < MAX_KEYS; k++) {
    ListElement * lep;
    /* Skip unused keys (between manuals) */
    if ((60 < k) && (k < 64)) continue;
    if ((124 < k) && (k < 128)) continue;
    /* Reset the accumulation matrix */
    endRow = 0;
    /* Put in key taper information (info from the wiring model) */
    for (lep = keyTaper[k]; lep != NULL; lep = lep->next) {
      cpmInsert (lep, cpmBus, cpmGain, wheelNumber, rowLength, &endRow);
    }
#if 1				/* Disabled while debugging */
    /* Put in crosstalk information (info from the crosstalk model) */
    for (lep = keyCrosstalk[k]; lep != NULL; lep = lep->next) {
      cpmInsert (lep, cpmBus, cpmGain, wheelNumber, rowLength, &endRow);
    }
#endif
    /* Read the matrix and generate a list entry in the keyContrib table. */
    for (w = 0; w < endRow; w++) {
      int c;
      for (c = 0; c < rowLength[w]; c++) {
#if 0
	/* Original code 22-sep-2004/FK */
	ListElement * rep = newRuntimeListElement ();
	LE_WHEEL_NUMBER_OF(rep) = wheelNumber[w];
	LE_BUSNUMBER_OF(rep) = cpmBus[w][c];
	LE_LEVEL_OF(rep) = cpmGain[w][c];
	ListElement ** P;
#else
	/* Test code 22-sep-2004/FK */
	ListElement ** P;
	ListElement * rep;
	if (cpmGain[w][c] < contributionFloorLevel) continue;
	rep = newRuntimeListElement ();
	LE_WHEEL_NUMBER_OF(rep) = wheelNumber[w];
	LE_BUSNUMBER_OF(rep)    = cpmBus[w][c];
	LE_LEVEL_OF(rep)        = cpmGain[w][c];

	if (LE_LEVEL_OF(rep) < contributionMinLevel) {
	  LE_LEVEL_OF(rep) = contributionMinLevel;
	}

#endif

	/* Insertion sort, first on wheel then on bus. */

	for (P = &(keyContrib[k]); (*P) != NULL; P = &((*P)->next)) {
	  if (sortMode == 0) {
	    if (LE_WHEEL_NUMBER_OF(rep) < LE_WHEEL_NUMBER_OF(*P)) break;
	    if (LE_WHEEL_NUMBER_OF(rep) == LE_WHEEL_NUMBER_OF(*P)) {
	      if (LE_BUSNUMBER_OF(rep) < LE_BUSNUMBER_OF(*P)) {
		break;
	      }
	    }
	  }
	} /* for insertion sort */
	rep->next = *P;
	*P = rep;

      }	/* for each column */
    } /* for each row */
  } /* for each key */
#if 0				/* DEBUG */
  for (k = 0; k < MAX_KEYS; k++) {
    if (keyContrib[k] != NULL) {
      keyContrib[k]->next = NULL;
    }
  }
#endif
}

/**
 * This function models the attenuation of the tone generators.
 * Note that the tone generator parameters expect the first/lowest
 * generator to be numbered 1.
 * The function basically takes the value 1.0 and subtracts the value of the
 * function  -x^2 * w   in the range v..u. Thus, w is a scale function and
 * should be in the range 0..1.
 */
static double damperCurve (int thisTG, /* Number of requested TG */
			   int firstTG,	/* Number of first TG in range */
			   int lastTG, /* Number of last TG in range */
			   double w, /* Weight */
			   double v, /* Lower x of -x^2 */
			   double u) /* Upper x of -x^2 */
{
  double x = ((double) (thisTG - firstTG)) / ((double) (lastTG - firstTG));
  double z = (x * (u - v)) - u;
  return 1.0 - w * z * z;
}

/**
 * Implements a built-in oscillator equalization curve.
 * Applies a constrained spline (p1x=0, p4x=0) to the oscillator's
 * attenuation.
 */
static int apply_CH_Spline (int nofOscillators,
			    double p1y,
			    double r1y,
			    double p4y,
			    double r4y) {
  int i;
  double k = nofOscillators - 1;
  for (i = 1; i <= nofOscillators; i++) {
    double t = ((double) (i - 1)) / k;
    double tSq = t * t;
    double tCb = tSq * t;
    double r;
    double a;

    r = p1y * ( 2.0 * tCb - 3.0 * tSq + 1.0)
      + p4y * (-2.0 * tCb + 3.0 * tSq)
      + r1y * ( tCb - 2.0 * tSq + t)
      + r4y * ( tCb - tSq);

    a = (r < 0.0) ? 0.0 : (1.0 < r) ? 1.0 : r;
    oscillators[i].attenuation = a;
  }

  return 0;
}

/**
 * Implements a built-in oscillator equalization curve.
 */
static int applyOscEQ_peak24 (int nofOscillators) {
  int i;

  for (i = 1; i <= 43; i++) {
    oscillators[i].attenuation = damperCurve (i,  1, 43, 0.2, -0.8,  1.0);
  }

  for (i = 44; i <= 48; i++) {
    oscillators[i].attenuation = damperCurve (i, 44, 48, 1.6, -0.4, -0.3);
  }

  for (i = 49; i <= nofOscillators; i++) {
    oscillators[i].attenuation =
      damperCurve (i, 49, nofOscillators, 0.9, -1.0, -0.7);
  }

  return 0;
}

/**
 * Implements a built-in oscillator equalization curve.
 */
static int applyOscEQ_peak46 (int nofOscillators) {
  int i;

  for (i = 1; i <= 43; i++) {
    oscillators[i].attenuation = damperCurve (i,  1, 43,  0.3, 0.4,  1.0);
  }

  for (i = 44; i <= 48; i++) {
    oscillators[i].attenuation = damperCurve (i, 44, 48,  0.1, -0.4, 0.4);
  }

  for (i = 49; i <= nofOscillators; i++) {
    oscillators[i].attenuation =
      damperCurve (i, 49, nofOscillators, 0.8, -1.0, -0.3);
  }

  return 0;
}

/**
 * This function returns the number of samples required to produce a
 * waveform of the given frequency below the provided precision.
 * The function will not attempt solutions above the maximum number
 * of samples. If no solution is found the best solution is returned.
 * The error/precision value is the positive distance between the
 * ideal number of samples and the nearest integer.
 * As for the maximum number of samples to try, higher frequencies fit
 * more waves into the same memory, but also make longer leaps around
 * the unit circle, making it harder to land close to the starting point.
 * Practical experiments seems to indicate that there is no gain in
 * making the maxSamples parameter dependent on the frequency.
 *
 * @param Hz         The frequency of the wave.
 * @param precision  The absolute value error threshold. Figures in the
 *                   range 0.1 - 0.01 may be adequate. Lower thresholds
 *                   will result in longer (more memory) solutions.
 * @param minSamples The minimum number of samples to use.
 * @param maxSamples The maximum number of samples to use.
 *
 * @return  The number of samples to allocate for the wave.
 */
static size_t fitWave (double Hz,
		       double precision,
		       int minSamples,
		       int maxSamples) {
  double minErr = 99999.9;
  double minSpn = 0.0;
  int    i;
  int minWaves;
  int maxWaves;
  minWaves = ceil  ((Hz * (double) minSamples) / SampleRateD);
  maxWaves = floor ((Hz * (double) maxSamples) / SampleRateD);

  assert (minWaves <= maxWaves);

  for (i = minWaves; i <= maxWaves; i++) {
    double nws = (SampleRateD * i) / Hz; /* Compute ideal nof samples */
    double spn = rint (nws);	/* Round to a discrete nof samples  */
    double err = fabs (nws - spn); /* Compute mismatch */
    if (err < minErr) {		/* Remember best so far */
      minErr = err;
      minSpn = spn;
    }
    if (err < precision) break;	/* If ok, stop searching. */
  }

  assert (0.0 < minSpn);

  return (size_t) minSpn;
}

/**
 * This routine writes the sample buffer for a simulated tone wheel.
 * In addition to the sine wave of the fundamental frequency, the routine
 * also allows the specification of chromatic harmonics.
 * There is no specification of phase.
 * This is an attempt to simulate the effect of transformer distortion and
 * generally make the output more 'warm'. If that really is the shape
 * generated by the real tonewheels remains to be seen (or heard). Anyway,
 * it is my current best guess as to the direction to go. Remember, the
 * chromatic harmonics are different from the harmonics added by the
 * drawbar system. The tonewheels are tuned to the tempered scale, and
 * thus will 'beat' very subtly against the chromatics.
 *
 * @param buf           Pointer to wave buffer
 * @param sampleLength  The number of 16-bit samples in the buffer
 * @param ap            Array of partial amplitudes
 * @param apLen         Nof elements in ap[]
 * @param attenuation   Final volume of wave (0.0 -- 1.0).
 * @param f1Hz          Frequency of the fundamental.
 *
 * Please note that the amplitudes of the fundamental and harmonic frequencies
 * are normalised so that the volume of the composite curve is 1.0. This
 * means that if you supply  f1a=1.0, f2a=0.1 and f3a=0.05 the actual
 * proportions will be:
 *
 *         f1a = 1.00 / 1.15 = 0.8695652173913044
 *         f2a = 0.10 / 1.15 = 0.08695652173913045
 *         f3a = 0.05 / 1.15 = 0.04347826086956522
 *
 * Be aware of this, or make sure that the arguments sum to 1.
 */
static void writeSamples (float * buf,
			  size_t sampleLength,
			  double ap [],
			  size_t apLen,
			  double attenuation,
			  double f1Hz)
{
  static double fullCircle = 2.0 * M_PI;
  double apl[MAX_PARTIALS];
  double plHz[MAX_PARTIALS];
  double aplSum;
  double U;
  float * yp = buf;
  int i;

  for (i = 0, aplSum = 0.0; i < MAX_PARTIALS; i++) {
    /* Select absolute amplitude */
    apl[i] = (i < apLen) ? ap[i] : 0.0;
    /* Accumulate normalization base */
    aplSum += fabs (apl[i]);
    /* Compute harmonic frequency */
    plHz[i] = f1Hz * ((double) (i + 1));
    /* Prevent aliasing; mute just below the Nyquist rate */
    if ((SampleRateD * 0.5) <= plHz[i]) {
      apl[i] = 0.0;
    }
  }

  /* Normalise amplitudes */

  U = attenuation / aplSum;

  for (i = 0; i < sampleLength; i++) {

    int j;
    double s = 0.0;

    for (j = 0; j < MAX_PARTIALS; j++) {
      s +=
	apl[j] * sin (drem ((plHz[j] * fullCircle * (double) i) / SampleRateD,
			    fullCircle));
    }

    /* 24-sep-2003/FK
     * Noise-shaping in an attempt to diffuse the quantization artifacts.
     * It did not work of course, but may add some analogue credibility
     * so it can be in for the moment. We add one bit of noise to the
     * least significant bit of the sample.
     */

#if 1
      *yp = (rand () < (RAND_MAX >> 1)) ? 1.0/32767.0 : 0;
      *yp++ += (U * s);
#else
      *yp++ = (U * s);
#endif

  } /* for */
}


/**
 * This routine initializes the oscillators.
 *
 * @param variant  Selects one of tree modelled tonegenerators:
 *                 0 : 91 generators, lowest generator is C-2
 *                 1 : 82 generators, lowest generator is A-2
 *                 2 : 91 generators, lowest generator is C-2, generators
 *                     1--12 have distinct 2f and 3f harmonics.
 *
 * @param precision  The loop precision value. See function fitWave().
 */
static void initOscillators (int variant, double precision) {
  int i;
  double baseTuning;
  int nofOscillators;
  int tuningOsc = 10;
  struct _oscillator * osp;
  double harmonicsList[MAX_PARTIALS];

  switch (variant) {

  case 0:
    nofOscillators = 91;
    baseTuning     = tuning / 8.0;
    tuningOsc      = 10;
    break;

  case 1:
    nofOscillators = 82;
    baseTuning     = tuning / 8.0;
    tuningOsc      = 1;
    break;

  case 2:
    nofOscillators = 91;
    baseTuning     = tuning / 8.0;
    tuningOsc      = 10;
    break;

  default:
    assert (0);
  } /* switch variant */

  /*
   * Apply equalisation curve. This sets the attenuation field in the
   * oscillator struct.
   */

  switch (eqMacro) {
  case EQ_SPLINE:
    apply_CH_Spline (nofOscillators, eqP1y, eqR1y, eqP4y, eqR4y);
    break;
  case EQ_PEAK24:
    applyOscEQ_peak24 (nofOscillators);
    break;
  case EQ_PEAK46:
    applyOscEQ_peak46 (nofOscillators);
    break;
  }

  for (i = 1; i <= nofOscillators; i++) {
    int j;
    size_t wszs;		/* Wave size samples */
    size_t wszb;		/* Wave size bytes */
    double t;
    ListElement * lep;

    osp = & (oscillators[i]);

    if (eqvSet[i] != 0) {
      osp->attenuation = eqvAtt[i];
    }

    osp->aclPos = -1;
    osp->rflags =  0;
    osp->pos    =  0;

    t = (double) (i - tuningOsc);

    if (gearTuning) {
      /* Frequency number minus one. The frequency number is the number of
	 the oscillator on the tone generator with 91 oscillators. This
	 means that the frequency number here always is 0 for c@37 Hz.
         The first tonegenerator is always numbered one, but depending
         on the organ model generator number 1 may be c@37 Hz (91 osc) or
         a@55 Hz (86 osc).
      */
      int freqNr = i + 9 - tuningOsc;
      int note = freqNr % 12;	/* 0=c, 11=h */
      int octave = freqNr / 12;	/* 0, 1, 2, ... */
      double gearA;
      double gearB;
      double teeth = pow (2.0, (double) (octave + 1));
      int select = note;
      if (84 <= freqNr) {
	select += 5;
	teeth = 192.0;
      }

      assert ((0 <= select) && (select < 12));

      if (gearTuning == 1) {
	gearA = gears60ratios[select][0];
	gearB = gears60ratios[select][1];
	osp->frequency = (20.0 * teeth * gearA) / gearB;
      }
      else {
	gearA = gears50ratios[select][0];
	gearB = gears50ratios[select][1];
	osp->frequency = (25.0 * teeth * gearA) / gearB;
      }
      osp->frequency *= (tuning / 440.0);
    }
    else {
      osp->frequency = baseTuning * pow (2.0, t / 12.0);
    }

    /*
     * The oscGenerateFragment() routine assumes that samples are at least
     * BUFFER_SIZE_SAMPLES long, so we must make sure that they are.
     * If the loop fits in n samples, it certainly will fit in 2n samples.
     * 31-jun-04/FK: Only if the loop is restarted after n samples. The
     *               error minimization effort depends critically on the
     *               value of n. In practice this is a non-issue because
     *               the search will not consider solutions shorter than
     *               the minimum length.
     */
    wszs = fitWave (osp->frequency,
		    precision,
		    3 * BUFFER_SIZE_SAMPLES, /* Was x1 */
		    2048);

    /* Compute the number of bytes needed for exactly one wave buffer. */

    wszb = wszs * 2;
    wszb = wszs * sizeof (float);

    /* Allocate the wave buffer */

    osp->wave = (float *) malloc (wszb);
    if (osp->wave == NULL) {
      fprintf (stderr,
	       "FATAL:Memory allocation failed in initOscillators. Offending request:\n");
      fprintf (stderr,
	       "Wave buffer for osc=%d of size %zu bytes.",
	       i,
	       wszb);
      exit (1);
    }

    /*
     * Make a note of the number of samples.
     */

    osp->lengthSamples = wszs;

    /* Reset the harmonics list to the compile-time value. */

    for (j = 0; j < MAX_PARTIALS; j++) {
      harmonicsList[j] = wheel_Harmonics[j];
    }

    /* Add optional global default from configuration */

    for (lep = wheelHarmonics[0]; lep != NULL; lep = lep->next) {
      int h = LE_HARMONIC_NUMBER_OF(lep) - 1;
      assert (0 <= h);
      if (h < MAX_PARTIALS) {
	harmonicsList[h] += LE_HARMONIC_LEVEL_OF(lep);
      }
    }

    /* Then add any harmonics specific to this wheel. */

    for (lep = wheelHarmonics[i]; lep != NULL; lep = lep->next) {
      int h = LE_HARMONIC_NUMBER_OF(lep) - 1;
      assert (0 <= h);
      if (h < MAX_PARTIALS) {
	harmonicsList[h] += LE_HARMONIC_LEVEL_OF(lep);
      }
    }

    /* Initialize each buffer, multiplying attenuation with taper. */

    writeSamples (osp->wave,
		  osp->lengthSamples,
		  harmonicsList,
		  (size_t) MAX_PARTIALS,
		  osp->attenuation,
		  osp->frequency);


  } /* for each oscillator struct */
}

/**
 * Controls the routing of the upper manual through the vibrato scanner.
 */
void setVibratoUpper (int isEnabled) {
  if (isEnabled) {
    newRouting |= RT_UPPRVIB;
  } else {
    newRouting &= ~RT_UPPRVIB;
  }
}

/**
 * Controls the routing of the lower manual through the vibrato scanner.
 */
void setVibratoLower (int isEnabled) {
  if (isEnabled) {
    newRouting |= RT_LOWRVIB;
  } else {
    newRouting &= ~RT_LOWRVIB;
  }
}

/**
 * This routine sets percussion on or off.
 *
 * @param isEnabled  If true, percussion is enabled. If false, percussion
 *                   is disabled.
 */
void setPercussionEnabled (int isEnabled) {

  if (isEnabled) {
    newRouting |=  RT_PERC;
    if (-1 < percTriggerBus) {
      drawBarGain[percTriggerBus] = 0.0;
      drawBarChange = 1;
    }
  } else {
    newRouting &= ~RT_PERC;
    if (-1 < percTriggerBus) {
      drawBarGain[percTriggerBus] =
	drawBarLevel[percTriggerBus][percTrigRestore];
      drawBarChange = 1;
    }
  }
  percEnabled = isEnabled;
}

/**
 * This routine sets the percCounterReset variable from the current
 * combination of the control flags percIsFast and percIsSoft.
 */
static void setPercussionResets () {
  if (percIsFast) {

    /* Alternate 25-May-2003 */
    percEnvGainDecay =
      percIsSoft ? percEnvGainDecayFastSoft : percEnvGainDecayFastNorm;

  }
  else {			/* Slow */

    /* Alternate 25-May-2003 */
    percEnvGainDecay =
      percIsSoft ? percEnvGainDecaySlowSoft : percEnvGainDecaySlowNorm;

  }
}

/**
 * Selects fast or slow percussion.
 * @param isFast  If true, selects fast percussion decay. If false,
 *                selects slow percussion decay.
 */
void setPercussionFast (int isFast) {
  percIsFast = isFast;
  setPercussionResets ();
}

/**
 * Selects soft or normal percussion volume.
 * @param isSoft  If true, selects soft percussion. If false, selects
 *                normal percussion.
 */
void setPercussionVolume (int isSoft) {
  percIsSoft = isSoft;

  /* Alternate 25-May-2003 */
  percEnvGainReset =
    percEnvScaling * (isSoft ? percEnvGainResetSoft : percEnvGainResetNorm);

  percDrawbarGain = isSoft ? percDrawbarSoftGain : percDrawbarNormalGain;

  setPercussionResets ();
}

/**
 * Selects first or second choice of percussion tone tap.
 */
void setPercussionFirst (int isFirst) {
  if (isFirst) {
    percSendBus = percSendBusA;
  } else {
    percSendBus = percSendBusB;
  }
}

/**
 * Computes the constant value with which the percussion gain is multiplied
 * between each output sample. The gain (and the percussion signal) then
 * suffers an exponential decay similar to that of a capacitor.
 *
 * @param ig  Initial gain (e.g. 1.0), must be non-zero positive.
 * @param tg  Target gain (e.g. 0.001 = -60 dB), must be non-zero positive.
 * @param spls Time expressed as samples, or equivalently, the number of
 *             multiplies that will be applied to the ig
 */
double getPercDecayConst_spl (double ig, double tg, double spls) {
  return exp (log (tg / ig) / spls);
}

/**
 * Computes the constant value with which the percussion gain is multiplied
 * between each output sample. The gain (and the percussion signal) then
 * suffers an exponential decay similar to that of a capacitor.
 *
 * @param ig  Initial gain (e.g. 1.0), must be non-zero positive.
 * @param tg  Target gain (e.g. 0.001 = -60 dB), must be non-zero positive.
 * @param seconds Time expressed as seconds
 */
double getPercDecayConst_sec (double ig, double tg, double seconds) {
  return getPercDecayConst_spl (ig, tg, SampleRateD * seconds);
}


/**
 * This routine is called each time one of the percussion parameters
 * has been updated. It recomputes the reset values for the percussion
 * amplification and the percussion amplification decrement counter.
 */
static void computePercResets () {

  /* Compute the percussion reset values. */

  /* Alternate 25-May-2003 */
  percEnvGainDecayFastNorm = getPercDecayConst_sec (percEnvGainResetNorm,
						    dBToGain (-60.0),
						    percFastDecaySeconds);

  percEnvGainDecayFastSoft = getPercDecayConst_sec (percEnvGainResetSoft,
						    dBToGain (-60.0),
						    percFastDecaySeconds);

  percEnvGainDecaySlowNorm = getPercDecayConst_sec (percEnvGainResetNorm,
						    dBToGain (-60.0),
						    percSlowDecaySeconds);

  percEnvGainDecaySlowSoft = getPercDecayConst_sec (percEnvGainResetSoft,
						    dBToGain (-60.0),
						    percSlowDecaySeconds);

  /* Deploy the computed reset values. */

  setPercussionResets ();
}


/**
 * This routine sets the fast percussion decay time.
 * @param seconds  The percussion decay time.
 */
void setFastPercussionDecay (double seconds) {
  percFastDecaySeconds = seconds;
  if (percFastDecaySeconds <= 0.0) {
    percFastDecaySeconds = 0.1;
  }
  computePercResets ();
}

/**
 * This routine sets the slow percussion decay time.
 * @param seconds  The percussion decay time.
 */
void setSlowPercussionDecay (double seconds) {
  percSlowDecaySeconds = seconds;
  if (percSlowDecaySeconds <= 0.0) {
    percSlowDecaySeconds = 0.1;
  }
  computePercResets ();
}

/**
 * Sets the percussion starting gain of the envelope for normal volume.
 * The expected level is 1.0 with soft volume less than that.
 * @param g  The starting gain.
 */
void setNormalPercussionGain (double g) {
  percEnvGainResetNorm = (float) g;
}

/**
 * Sets the percussion starting gain of the envelope for soft volume.
 * The expected level is less than 1.0.
 * @param g  The starting gain.
 */
void setSoftPercussionGain (double g) {
  percEnvGainResetSoft = (float) g;
}

/**
 * Sets the percussion gain scaling factor.
 * @param s  The scaling factor.
 */
void setPercussionGainScaling (double s) {
  percEnvScaling = (float) s;
}

void setEnvAttackModel (int model) {
  if ((0 <= model) && (model < ENV_CLICKMODELS)) {
    envAttackModel = model;
  }
}

void setEnvReleaseModel (int model) {
  if ((0 <= model) && (model < ENV_CLICKMODELS)) {
    envReleaseModel = model;
  }
}

/**
 * This parameter actually simulates the amount of noise created when the
 * closing/opening contact surfaces move against each other. Setting it
 * to zero will still click, due to the timing differences in the separate
 * envelopes (they close at different times). However, a higher value will
 * simulate more oxidization of the contacts, as in a worn instrument.
 */
void setEnvAttackClickLevel (double u) {
  envAttackClickLevel = u;
}

static void setEnvAtkClkLength (int * p, double u) {
  if (p != NULL) {
    if ((0.0 <= u) && (u <= 1.0)) {
      *p = (int) (((double) BUFFER_SIZE_SAMPLES) * u);
    }
  }
}

/**
 * Sets the minimum duration of a keyclick noise burst. The unit is a fraction
 * (0.0 -- 1.0) of the adjustable range.
 */
void setEnvAtkClkMinLength (double u) {
  setEnvAtkClkLength (&envAtkClkMinLength, u);
}

/**
 * Sets the maximum duration of a keyclick noise burst.
 */
void setEnvAtkClkMaxLength (double u) {
  setEnvAtkClkLength (&envAtkClkMaxLength, u);
}

void setEnvReleaseClickLevel (double u) {
 envReleaseClickLevel = u;
}

#ifdef KEYCOMPRESSION

/**
 * This routine initializes the key compression table. It is in all
 * simplicity a volume control on the output side of the tone generator,
 * controlled by the number of depressed keys. It serves two purposes:
 * (1) on the original instrument, more keys played means more
 *     parallel signal paths and an overall lesser impedance between the
 *     oscillator outputs and ground. This reduces the signal level.
 *     (At least, that is my best guess.)
 * (2) in the digital domain, more keys usually means more sound. The
 *     sound engine is basically a complicated mixer and reducing the
 *     output level as the number of inputs increase makes perfect sense.
 *
 * The first eight positions are handcrafted to sound less obvious.
 */
static void initKeyCompTable () {
  int i;
  float u = -5.0;
  float v = -9.0;
  float m = 1.0 / (MAXKEYS - 12);

 /* The first two entries, 0 and 1, should be equal! */

  keyCompTable[ 0] = keyCompTable[1] = KEYCOMP_ZERO_LEVEL;
  keyCompTable[ 2] = dBToGain ( -1.1598);
  keyCompTable[ 3] = dBToGain ( -2.0291);
  keyCompTable[ 4] = dBToGain ( -2.4987);
  keyCompTable[ 5] = dBToGain ( -2.9952);
  keyCompTable[ 6] = dBToGain ( -3.5218);
  keyCompTable[ 7] = dBToGain ( -4.0823);
  keyCompTable[ 8] = dBToGain ( -4.6815);
  keyCompTable[ 9] = dBToGain ( -4.9975);
  keyCompTable[10] = dBToGain ( -4.9998);

  /* Linear interpolation from u to v. */

  for (i = 11; i < MAXKEYS; i++) {
    float a = (float) (i - 11);
    keyCompTable[i] = dBToGain (u + ((v - u) * a * m));
  }

}

#endif /* KEYCOMPRESSION */

/**
 * Dumps the configuration lists to a text file.
 */
static void dumpConfigLists (char * fname) {
  FILE * fp;
  int i;
  int j;

  if ((fp = fopen (fname, "w")) != NULL) {

    fprintf (fp,
	     "%s\n\n",
	     "Array wheelHarmonics (index is wheel number)");

    for (i = 0; i <= NOF_WHEELS; i++) {
      fprintf (fp, "wheelHarmonics[%2d]=", i);
      if (wheelHarmonics[i] == NULL) {
	fprintf (fp, "NULL");
      }
      else {
	ListElement * lep;
	j = 0;
	for (lep = wheelHarmonics[i]; lep != NULL; lep = lep->next) {
	  if (j++) fprintf (fp, ", ");
	  fprintf (fp,
		   "f%d:%f",
		   LE_HARMONIC_NUMBER_OF(lep),
		   LE_HARMONIC_LEVEL_OF(lep));
	}
      }
      fprintf (fp, "\n");
    }

    fprintf (fp,
	     "\n%s\n\n",
	     "Array terminalMix (index is terminal number)");

    for (i = 0; i <= NOF_WHEELS; i++) {
      fprintf (fp, "terminalMix[%2d]=", i);
      if (terminalMix[i] == NULL) {
	fprintf (fp, "NULL");
      }
      else {
	ListElement * lep;
	j = 0;
	for (lep = terminalMix[i]; lep != NULL; lep = lep->next) {
	  if (j++) fprintf (fp, ", ");
	  fprintf (fp,
		   "w%d:%f",
		   LE_WHEEL_NUMBER_OF(lep),
		   LE_WHEEL_LEVEL_OF(lep));
	}
      }
      fprintf (fp, "\n");
    }

    fprintf (fp, "\n%s\n\n", "Array keyTaper (index is keynumber)");

    for (i = 0; i < MAX_KEYS; i++) {
      fprintf (fp, "keyTaper[%2d]=", i);
      if (keyTaper[i] == NULL) {
	fprintf (fp, "NULL");
      }
      else {
	ListElement * lep;
	j = 0;
	for (lep = keyTaper[i]; lep != NULL; lep = lep->next) {
	  if (j++) fprintf (fp, ", ");
	  fprintf (fp,
		   "t%d:b%d:g%f",
		   LE_TERMINAL_OF(lep),
		   LE_BUSNUMBER_OF(lep),
		   LE_TAPER_OF(lep));
	}
      }
      fprintf (fp, "\n");
    }

    fprintf (fp, "\n%s\n\n", "Array keyCrosstalk (index is keynumber)");

    for (i = 0; i < MAX_KEYS; i++) {
      fprintf (fp, "keyCrosstalk[%2d]=", i);
      if (keyCrosstalk[i] == NULL) {
	fprintf (fp, "NULL");
      }
      else {
	ListElement * lep;
	j = 0;
	for (lep = keyCrosstalk[i]; lep != NULL; lep = lep->next) {
	  if (j++) fprintf (fp, ", ");
	  fprintf (fp,
		   "b%d:t%d:g%f",
		   LE_BUSNUMBER_OF(lep),
		   LE_TERMINAL_OF(lep),
		   LE_LEVEL_OF(lep));
	}
      }
      fprintf (fp, "\n");
    }

    fprintf (fp, "\nEnd of dump\n");

    fclose (fp);
  }
  else {
    perror (fname);
  }
}

/**
 * Dumps the keyContrib table to a text file.
 */
static void dumpRuntimeData (char * fname) {
  FILE * fp;
  int k;
  if ((fp = fopen (fname, "w")) != NULL) {
    fprintf (fp, "%s\n\n", "Array keyContrib (index is key number)");
    for (k = 0; k < MAX_KEYS; k++) {
      fprintf (fp, "keyContrib[%3d]=", k);
      ListElement * rep;
      int j = 0;
      int wcount = 0;
      int lastWheel = -1;
      for (rep = keyContrib[k]; rep != NULL; rep = rep->next) {
	int x;
	double dbLevel = 20.0 * log10 (LE_LEVEL_OF(rep));
	if (j++) {
	  fprintf (fp, "%16c", ' ');
	}
	fprintf (fp, "[w%2d:b%2d:g%f] % 10.6lf dB  ",
		 LE_WHEEL_NUMBER_OF(rep),
		 LE_BUSNUMBER_OF(rep),
		 LE_LEVEL_OF(rep),
		 dbLevel);
	if (-60.0 < dbLevel) {
	  int len = (int) (25.0 * LE_LEVEL_OF(rep) / 3.0);
	  for (x = 0; x < len; x++) fprintf (fp, "I");
	}
	fprintf (fp, "\n");
	if (lastWheel != LE_WHEEL_NUMBER_OF(rep)) {
	  wcount++;
	  lastWheel = LE_WHEEL_NUMBER_OF(rep);
	}
      }
      fprintf (fp, "%2d wheels, %3d entries\n", wcount, j);
    }
    fclose (fp);
  }
  else {
    perror (fname);
  }
}

/* ================================================================
 * 22-sep-2003/FK
 * Dump the oscillator array for diagnostics.
 */
static int dumpOscToText (char * fname) {
  FILE * fp;
  int i;
  size_t bufferSamples = 0;

  if ((fp = fopen (fname, "w")) == NULL) {
    perror (fname);
    return -1;
  }

  fprintf (fp, "Oscillator dump\n");
  fprintf (fp, "[%3s]:%10s:%5s:%6s:%5s\n",
	   "OSC",
	   "Frequency",
	   "Sampl",
	   "Bytes",
	   "Gain");
  for (i = 0; i < 128; i++) {
    fprintf (fp, "[%3d]:%7.2lf Hz:%5zu:%6zu:%5.2lf\n",
	     i,
	     oscillators[i].frequency,
	     oscillators[i].lengthSamples,
	     oscillators[i].lengthSamples * sizeof (float),
	     oscillators[i].attenuation);
    bufferSamples += oscillators[i].lengthSamples;
  }

  fprintf (fp, "TOTAL MEMORY: %zu samples, %zu bytes\n",
	   bufferSamples,
	   bufferSamples * sizeof (float));


  fclose (fp);
  return 0;
}

/**
 * This routine configures this module.
 */
int oscConfig (ConfigContext * cfg) {
  int ack = 0;
  double d;
  int ival;
  if ((ack = getConfigParameter_d ("osc.tuning", cfg, &d)) == 1) {
    setTuning (d);
  }
  else if (!strcasecmp (cfg->name, "osc.temperament")) {
    ack++;
    if (!strcasecmp (cfg->value, "equal")) {
      gearTuning = 0;
    }
    else if (!strcasecmp (cfg->value, "gear60")) {
      gearTuning = 1;
    }
    else if (!strcasecmp (cfg->value, "gear50")) {
      gearTuning = 2;
    }
    else {
      showConfigfileContext (cfg, "'equal', 'gear60', or 'gear50' expected");
    }
  }
  else if ((ack = getConfigParameter_d ("osc.x-precision", cfg, &d)) == 1) {
    setWavePrecision (d);
  }
  else if ((ack = getConfigParameter_d ("osc.perc.fast",
				       cfg,
				       &percFastDecaySeconds))) {
    ;
  }
  else if ((ack = getConfigParameter_d ("osc.perc.slow",
				       cfg,
				       &percSlowDecaySeconds))) {
    ;
  }
  else if ((ack = getConfigParameter_d ("osc.perc.normal", cfg, &d)) == 1) {
    setNormalPercussionGain (d);
  }
  else if ((ack = getConfigParameter_d ("osc.perc.soft", cfg, &d)) == 1) {
    setSoftPercussionGain (d);
  }
  else if ((ack = getConfigParameter_d ("osc.perc.gain", cfg, &d)) == 1) {
    setPercussionGainScaling (d);
  }
  else if ((ack = getConfigParameter_ir ("osc.perc.bus.a",
					 cfg,
					 &ival,
					 0, 8)) == 1) {
    percSendBusA = ival;
  }
  else if ((ack = getConfigParameter_ir ("osc.perc.bus.b",
					 cfg,
					 &ival,
					 0, 8)) == 1) {
    percSendBusB = ival;
  }
  else if ((ack = getConfigParameter_ir ("osc.perc.bus.trig",
					 cfg,
					 &ival,
					 -1, 8)) == 1) {
    percTriggerBus = ival;
  }
  else if (!strcasecmp (cfg->name, "osc.eq.macro")) {
    ack++;
    if (!strcasecmp (cfg->value, "chspline")) {
      eqMacro = EQ_SPLINE;
    } else if (!strcasecmp (cfg->value, "peak24")) {
      eqMacro = EQ_PEAK24;
    } else if (!strcasecmp (cfg->value, "peak46")) {
      eqMacro = EQ_PEAK46;
    } else {
      fprintf (stderr,
	       "%s:line %d:%s expected chspline, peak24 or peak46:%s\n",
	       cfg->fname,
	       cfg->linenr,
	       cfg->name,
	       cfg->value);
    }
  }
  else if ((ack = getConfigParameter_d ("osc.eq.p1y", cfg, &eqP1y)))
    ;
  else if ((ack = getConfigParameter_d ("osc.eq.r1y", cfg, &eqR1y)))
    ;
  else if ((ack = getConfigParameter_d ("osc.eq.p4y", cfg, &eqP4y)))
    ;
  else if ((ack = getConfigParameter_d ("osc.eq.r4y", cfg, &eqR4y)))
    ;
  else if ((ack = getConfigParameter_d ("osc.eqv.ceiling", cfg, &eqvCeiling)))
    ;
  else if (!strncasecmp (cfg->name, "osc.eqv.", 8)) {
    int n;
    double v;
    ack++;
    if (sscanf (cfg->name, "osc.eqv.%d", &n) == 1) {
      if ((0 <= n) && (n <= 127)) {
	if (sscanf (cfg->value, "%lf", &v) == 1) {
	  if ((0.0 <= v) && (v <= eqvCeiling)) {
	    eqvAtt[n] = v / eqvCeiling;
	    eqvSet[n] = 1;
	  }
	}
	else {
	  configDoubleUnparsable (cfg);
	}
      }
      else {
	configIntOutOfRange (cfg, 0, 127);
      }
    }
  }
  else if (!strncasecmp (cfg->name, "osc.harmonic.", 13)) {
    int n;
    int w;
    double v;
    ack++;
    if (sscanf (cfg->name, "osc.harmonic.%d", &n) == 1) {

      if (sscanf (cfg->value, "%lf", &v) == 1) {
	ListElement * lep = newConfigListElement ();
	LE_HARMONIC_NUMBER_OF(lep) = (short) n;
	LE_HARMONIC_LEVEL_OF(lep) = (float) v;
	appendListElement (&(wheelHarmonics[0]), lep);
      }
      else {
	configDoubleUnparsable (cfg);
      }

    }
    else if (sscanf (cfg->name, "osc.harmonic.w%d.f%d", &w, &n) == 2) {
      if ((0 < w) && (w <= NOF_WHEELS)) {
	if (sscanf (cfg->value, "%lf", &v) == 1) {
	  ListElement * lep = newConfigListElement ();
	  LE_HARMONIC_NUMBER_OF(lep) = (short) n;
	  LE_HARMONIC_LEVEL_OF(lep) = (float) v;
	  appendListElement (&(wheelHarmonics[w]), lep);
	}
	else {
	  configDoubleUnparsable (cfg);
	}
      }
      else {
	char buf[128];
	sprintf (buf, "Wheel number must be 1--%d", NOF_WHEELS);
	showConfigfileContext (cfg, buf);
      }
    }
  }
  else if (!strncasecmp (cfg->name, "osc.terminal.", 13)) {
    int n;
    int w;
    double v;
    ack++;
    if (sscanf (cfg->name, "osc.terminal.t%d.w%d", &n, &w) == 2) {
      if ((0 < n) && (n <= NOF_WHEELS) &&
	  (0 < w) && (w <= NOF_WHEELS)) {
	if (sscanf (cfg->value, "%lf", &v) == 1) {
	  ListElement * lep = newConfigListElement ();
	  LE_WHEEL_NUMBER_OF(lep) = (short) w;
	  LE_WHEEL_LEVEL_OF(lep) = (float) v;
	  appendListElement (&(terminalMix[n]), lep);
	}
	else {
	  configDoubleUnparsable (cfg);
	}
      }
      else {
	char buf[128];
	sprintf (buf, "Wheel and terminal numbers must be 1--%d", NOF_WHEELS);
	showConfigfileContext (cfg, buf);
      }
    }
  }
  else if (!strncasecmp (cfg->name, "osc.taper.", 10)) {
    int k;
    int b;
    int t;
    double v;
    char buf[128];
    ack++;
    if (sscanf (cfg->name, "osc.taper.k%d.b%d.t%d", &k, &b, &t) == 3) {
      if ((0 < k) && (k < MAX_KEYS)) {
	if ((0 < b) && (b < NOF_BUSES)) {
	  if ((0 < t) && (t <= NOF_WHEELS)) {
	    if (sscanf (cfg->value, "%lf", &v) == 1) {
	      ListElement * lep = newConfigListElement ();
	      LE_TERMINAL_OF(lep) = t;
	      LE_BUSNUMBER_OF(lep) = b;
	      LE_TAPER_OF(lep) = (float) v;
	      appendListElement (&keyTaper[k], lep);
	    }
	    else {
	      configDoubleUnparsable (cfg);
	    }
	  }
	  else {
	    sprintf (buf, "Terminal numbers must be 1--%d", NOF_WHEELS);
	    showConfigfileContext (cfg, buf);
	  }
	}
	else {
	  sprintf (buf, "Bus number must be 0--%d", NOF_BUSES - 1);
	  showConfigfileContext (cfg, buf);
	}
      }
      else {
	sprintf (buf, "Key number must be 0--%d", MAX_KEYS - 1);
	showConfigfileContext (cfg, buf);
      }
    }
  }
  else if (!strncasecmp (cfg->name, "osc.crosstalk.", 14)) {
    int k;
    char buf[128];
    ack++;
    if (sscanf (cfg->name, "osc.crosstalk.k%d", &k) == 1) {
      if ((0 < k) && (k < MAX_KEYS)) {
	int b;
	int t;
	double v;
	char * s = cfg->value;
	do {
	  if (sscanf (s, "%d:%d:%lf", &b, &t, &v) == 3) {
	    if ((0 < b) && (b < NOF_BUSES)) {
	      if ((0 < t) && (t <= NOF_WHEELS)) {
		ListElement * lep = newConfigListElement ();
		LE_TERMINAL_OF(lep) = t;
		LE_BUSNUMBER_OF(lep) = b;
		LE_LEVEL_OF(lep) = v;
		appendListElement (&(keyCrosstalk[k]), lep);
	      }
	      else {
		sprintf (buf, "Terminal numbers must be 1--%d", NOF_WHEELS);
		showConfigfileContext (cfg, buf);
	      }
	    }
	    else {
	      sprintf (buf, "Bus number must be 0--%d", NOF_BUSES - 1);
	      showConfigfileContext (cfg, buf);
	    }
	  }
	  else {
	    showConfigfileContext (cfg, "Malformed value");
	  }
	  s = strpbrk (s, ",");	/* NULL or ptr to comma */
	  if (s != NULL) {
	    s++;		/* Move past comma */
	  }
	} while (s != NULL);
      }
      else {
	sprintf (buf, "Key number must be 0--%d", MAX_KEYS - 1);
	showConfigfileContext (cfg, buf);
      }
    }
  }
  else if ((ack = getConfigParameter_dr ("osc.compartment-crosstalk",
					 cfg, &d, 0.0, 1.0)) == 1) {
    defaultCompartmentCrosstalk = d;
  }
  else if ((ack = getConfigParameter_dr ("osc.transformer-crosstalk",
					 cfg, &d, 0.0, 1.0)) == 1) {
    defaultTransformerCrosstalk = d;
  }
  else if ((ack = getConfigParameter_dr ("osc.terminalstrip-crosstalk",
					 cfg, &d, 0.0, 1.0)) == 1) {
    defaultTerminalStripCrosstalk = d;
  }
  else if ((ack = getConfigParameter_dr ("osc.wiring-crosstalk",
					 cfg, &d, 0.0, 1.0)) == 1) {
    defaultWiringCrosstalk = d;
  }
  else if ((ack = getConfigParameter_dr ("osc.contribution-floor",
					 cfg, &d, 0.0, 1.0)) == 1) {
    contributionFloorLevel = d;
  }
  else if ((ack = getConfigParameter_dr ("osc.contribution-min",
					 cfg, &d, 0.0, 1.0)) == 1) {
    contributionMinLevel = d;
  }
  else if ((ack = getConfigParameter_dr ("osc.attack.click.level",
					 cfg, &d, 0.0, 1.0)) == 1) {
    setEnvAttackClickLevel (d);
  }
  else if ((ack = getConfigParameter_dr ("osc.attack.click.maxlength",
					 cfg, &d, 0.0, 1.0)) == 1) {
    setEnvAtkClkMaxLength (d);
  }
  else if ((ack = getConfigParameter_dr ("osc.attack.click.minlength",
					 cfg, &d, 0.0, 1.0)) == 1) {
    setEnvAtkClkMinLength (d);
  }
  else if ((ack = getConfigParameter_dr ("osc.release.click.level",
					 cfg, &d, 0.0, 1.0)) == 1) {
    setEnvReleaseClickLevel (d);
  }
  else if (!strcasecmp (cfg->name, "osc.release.model")) {
    ack++;
    if (!strcasecmp (getConfigValue (cfg), "click")) {
      setEnvReleaseModel (ENV_CLICK);
    }
    else if (!strcasecmp (getConfigValue (cfg), "cosine")) {
      setEnvReleaseModel (ENV_COSINE);
    }
    else if (!strcasecmp (getConfigValue (cfg), "linear")) {
      setEnvReleaseModel (ENV_LINEAR);
    }
    else if (!strcasecmp (getConfigValue (cfg), "shelf")) {
      setEnvReleaseModel (ENV_SHELF);
    }
  }
  else if (!strcasecmp (cfg->name, "osc.attack.model")) {
    ack++;
    if (!strcasecmp (getConfigValue (cfg), "click")) {
      setEnvAttackModel (ENV_CLICK);
    }
    else if (!strcasecmp (getConfigValue (cfg), "cosine")) {
      setEnvAttackModel (ENV_COSINE);
    }
    else if (!strcasecmp (getConfigValue (cfg), "linear")) {
      setEnvAttackModel (ENV_LINEAR);
    }
    else if (!strcasecmp (getConfigValue (cfg), "shelf")) {
      setEnvAttackModel (ENV_SHELF);
    }
  }
  return ack;
}

static const ConfigDoc doc[] = {
  {"osc.tuning", CFG_DOUBLE, "440.0", "range: [220..880]"},
  {"osc.temperament", CFG_TEXT, "\"gear60\"", "one of: \"equal\", \"gear60\", \"gear50\""},
  {"osc.x-precision", CFG_DOUBLE, "0.001", ""},
  {"osc.perc.fast", CFG_DOUBLE, "1.0", "Fast Decay (seconds)"},
  {"osc.perc.slow", CFG_DOUBLE, "4.0", "Slow Decay (seconds)"},
  {"osc.perc.normal", CFG_DOUBLE, "1.0", "Sets the percussion starting gain of the envelope for normal volume; range [0..1]"},
  {"osc.perc.soft", CFG_DOUBLE, "0.5012", "Sets the percussion starting gain of the envelope for soft volume. range [0..1[ (less than 1.0)"},
#ifdef HIPASS_PERCUSSION
  {"osc.perc.gain", CFG_DOUBLE, "11.0", "Sets the percussion gain scaling factor"},
#else
  {"osc.perc.gain", CFG_DOUBLE, "3.0", "Sets the percussion gain scaling factor"},
#endif
  {"osc.perc.bus.a", CFG_INT, "3", "range [0..8]"},
  {"osc.perc.bus.b", CFG_INT, "4", "range [0..8]"},
  {"osc.perc.bus.trig", CFG_INT, "8", "range [-1..8]"},
  {"osc.eq.macro", CFG_TEXT, "\"chspline\"", "one of \"chspline\", \"peak24\", \"peak46\""},
  {"osc.eq.p1y", CFG_DOUBLE, "1.0", "EQ spline parameter"},
  {"osc.eq.r1y", CFG_DOUBLE, "0.0", "EQ spline parameter"},
  {"osc.eq.p4y", CFG_DOUBLE, "1.0", "EQ spline parameter"},
  {"osc.eq.r4y", CFG_DOUBLE, "0.0", "EQ spline parameter"},
  {"osc.eqv.ceiling", CFG_DOUBLE, "1.0", "Normalize EQ parameters."},
  {"osc.eqv.<oscnum>", CFG_DOUBLE, "-", "oscnum=[0..127], value: [0..osc.eqv.ceiling]; default values are calculated depending on selected osc.eq.macro and tone-generator-model."},
  {"osc.harmonic.<h>", CFG_DOUBLE, "-", "speficy level of given harmonic number."},
  {"osc.harmonic.w<w>.f<h>", CFG_DOUBLE, "-", "w: number of wheel [0..91], h: harmonic number"},
  {"osc.terminal.t<t>.w<w>", CFG_DOUBLE, "-", "t,w: wheel-number [0..91]"},
  {"osc.taper.k<key>.b<bus>.t<wheel>", CFG_DOUBLE, "-", ""},
  {"osc.crosstalk.k<key>", CFG_TEXT, "-", "value colon-separated: \"<int:bus>:<int:wheel>:<double:level>\""},
  {"osc.compartment-crosstalk", CFG_DOUBLE, "0.01", "crosstalk between tonewheels in the same compartment. The value refers to the amount of rogue signal picked up; range: [0..1]"},
  {"osc.transformer-crosstalk", CFG_DOUBLE, "0", "crosstalk between transformers on the top of the tg; range: [0..1]"},
  {"osc.terminalstrip-crosstalk", CFG_DOUBLE, "0.01", "crosstalk between connection on the terminal strip; range: [0..1]"},
  {"osc.wiring-crosstalk", CFG_DOUBLE, "0.01", " throttle on the crosstalk distribution model for wiring; range: [0..1]"},
  {"osc.contribution-floor", CFG_DOUBLE, "0.0000158", "Signals weaker than this are not put on the contribution list; range: [0..1]"},
  {"osc.contribution-min", CFG_DOUBLE, "0", "If non-zero, signals that are placed on the contribution have at least this level; range: [0..1]"},
  {"osc.attack.click.level", CFG_DOUBLE, "0.5", "range: [0..1]"},
  {"osc.attack.click.maxlength", CFG_DOUBLE, "0.6250", "range: [0..1]. 1.0 corresponds to " STRINGIFY(BUFFER_SIZE_SAMPLES) " audio-samples"},
  {"osc.attack.click.minlength", CFG_DOUBLE, "0.1250", "range: [0..1]. 1.0 corresponds to " STRINGIFY(BUFFER_SIZE_SAMPLES) " audio-samples"},
  {"osc.release.click.level", CFG_DOUBLE, "0.25", "range: [0..1]"},
  {"osc.release.model", CFG_TEXT, "\"linear\"", "one of \"click\", \"cosine\", \"linear\", \"shelf\" "},
  {"osc.attack.model", CFG_TEXT, "\"click\"", "one of \"click\", \"cosine\", \"linear\", \"shelf\" "},
  {NULL}
};

const ConfigDoc *oscDoc () {
  return doc;
}


/**
 * This routine initialises the envelope shape tables.
 */
static void initEnvelopes () {
  int bss = BUFFER_SIZE_SAMPLES;
  int b;
  int i;			/* 0 -- 127 */
  int burst;			/* Samples in noist burst */
  int bound;
  int start;			/* Sample where burst starts */
  double T = (double) (BUFFER_SIZE_SAMPLES - 1); /* 127.0 */

  for (b = 0; b < 9; b++) {

    if (envAttackModel == ENV_CLICK) {
      /* Select a random length of this burst. */
      bound = envAtkClkMaxLength - envAtkClkMinLength;
      if (bound < 1) {
	bound = 1;
      }
      burst = envAtkClkMinLength + (rand () % bound);
      if (bss <= burst) {
	burst = bss - 1;
      }
      /* Select a random start position of the burst. */
      start = (rand () % (bss - burst));
      /* From sample 0 to start the amplification is zero. */
      for (i = 0; i < start; i++) attackEnv[b][i] = 0.0;
      /* In the burst area the amplification is random. */
      for (; i < (start + burst); i++) {
	attackEnv[b][i] = 1.0 - (envAttackClickLevel * drnd ());
      }
      /* From the end of the burst to the end of the envelope the
	 amplification is unity. */
      for (; i < bss; i++) attackEnv[b][i] = 1.0;

#if 1
      /* 2002-08-31/FK EXPERIMENTAL */
      /* Two-point average low-pass filter. */
      {
	attackEnv[b][0] /= 2.0;
	for (i = 1; i < bss; i++) {
	  attackEnv[b][i] = (attackEnv[b][i-1] + attackEnv[b][i]) / 2.0;
	}
      }
#endif

    }

    if (envAttackModel == ENV_SHELF) {
      bound = envAtkClkMaxLength - envAtkClkMinLength;
      if (bound < 1) bound = 1;
      start = rand () % bound;
      if ((bss - 2) <= start) start = bss - 2;
      for (i = 0; i < start; i++) attackEnv[b][i] = 0.0;
      attackEnv[b][i + 0] = 0.33333333;
      attackEnv[b][i + 1] = 0.66666666;
      for (i = i + 2; i < bss; i++) {
	attackEnv[b][i] = 1.0;
      }
    }

    if (envReleaseModel == ENV_SHELF) {
      bound = envAtkClkMaxLength - envAtkClkMinLength;
      if (bound < 1) bound = 1;
      start = rand () % bound;
      if ((bss - 2) <= start) start = bss - 2;
      for (i = 0; i < start; i++) releaseEnv[b][i] = 0.0;
      releaseEnv[b][i + 0] = 0.33333333;
      releaseEnv[b][i + 1] = 0.66666666;
      for (i = i + 2; i < bss; i++) {
	releaseEnv[b][i] = 1.0;
      }
    }

    if (envReleaseModel == ENV_CLICK) {
      burst = 8 + (rand () % 32);
      start = (rand () % (bss - burst));

      for (i = 0; i < start; i++) releaseEnv[b][i] = 0.0;
      for (; i < (start + burst); i++) {
	releaseEnv[b][i] = 1.0 - (envReleaseClickLevel * drnd ());
      }
      for (; i < bss; i++) releaseEnv[b][i] = 1.0;
      /* Filter the envelope */
      releaseEnv[b][0] /= 2.0;
      for (i = 1; i < bss; i++) {
	releaseEnv[b][i] = (releaseEnv[b][i-1] + releaseEnv[b][i]) / 2.0;
      }
    }

    /* cos(0)=1.0, cos(PI/2)=0, cos(PI)=-1.0 */

    if (envAttackModel == ENV_COSINE) {	/* Sigmoid decay */
      for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
	int d = BUFFER_SIZE_SAMPLES - (i + 1);
	double a = (M_PI * (double) d) / T;	/* PI < a <= 0 */
	attackEnv [b][i] = 0.5 + (0.5 * cos (a));
      }
    }

    if (envReleaseModel == ENV_COSINE) {
      for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
	double a = (M_PI * (double) i) / T;	/* 0 < b <= PI */
	releaseEnv[b][i] = 0.5 - (0.5 * cos (a));
      }
    }

    if (envAttackModel == ENV_LINEAR) {	/* Linear decay */
      int k = BUFFER_SIZE_SAMPLES;			/* TEST SPECIAL */

      for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
	if (i < k) {
	  attackEnv[b][i]  = ((float) i) / (float) k;
	} else {
	  attackEnv[b][i] = 1.0;
	}
      }
    }

    if (envReleaseModel == ENV_LINEAR) {
      int k = BUFFER_SIZE_SAMPLES;			/* TEST SPECIAL */

      for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
	if (i < k) {
	  releaseEnv[b][i] = ((float) i) / (float) k;
	} else {
	  releaseEnv[b][i] = 1.0;
	}
      }
    }

  } /* for each envelope buffer */
}

/**
 * Installs the setting for the drawbar on the given bus. The gain value is
 * fetched from the drawBarLevel table where the bus is the row index and the
 * setting is the column index.
 *
 * @param bus      The bus (0--26) for which the drawbar is set.
 * @param setting  The position setting (0--8) of the drawbar.
 */
static void setDrawBar (int bus, unsigned int setting) {
  assert ((0 <= bus) && (bus < NOF_BUSES));
  assert ((0 <= setting) && (setting < 9));
  drawBarChange = 1;
  if (bus == percTriggerBus) {
    percTrigRestore = setting;
    if (percEnabled) return;
  }
  drawBarGain[bus] = drawBarLevel[bus][setting];
}

/**
 * This routine installs the drawbar setting for the tone generator.
 * The argument is an array of 9 integers where index 0 corresponds
 * to the setting of 16', index 1 to 5 1/3', index 2 to 8' etc up to
 * index 8 which corresponds to 1'. The values in each element are
 * expected to be 0 (off), 1 (lowest), ... , 8 (loudest).
 * The values are copied from the argument.
 * @param manual   0=upper, 1=lower, 2=pedals
 * @param setting  Array of 9 integers.
 */
void setDrawBars (unsigned int manual, unsigned int setting []) {
  int i;
  int offset;
  if (manual == 0) {
    offset = UPPER_BUS_LO;
  } else if (manual == 1) {
    offset = LOWER_BUS_LO;
  } else if (manual == 2) {
    offset = PEDAL_BUS_LO;
  } else {
    assert (0);
  }
  for (i = 0; i < 9; i++) {
    setDrawBar (offset + i, setting[i]);
  }
}



/*
 * MIDI controller callbacks
 */

/*
 * Note that the drawbar controllers are inverted so that fader-like
 * controllers work in reverse, like real drawbars. This means that
 * a MIDI controller value of 0 is max and 127 is min. Also note that
 * the controller values are quantized into 0, ... 8 to correspond to
 * the nine discrete positions of the original drawbar system.
 *
 */

static void setMIDIDrawBar (int bus, unsigned char v) {
  setDrawBar (bus, (8 * ((unsigned int) (127 - v))) / 127);
}

static void setDrawbar0 (unsigned char v) { setMIDIDrawBar (0, v);}
static void setDrawbar1 (unsigned char v) { setMIDIDrawBar (1, v);}
static void setDrawbar2 (unsigned char v) { setMIDIDrawBar (2, v);}
static void setDrawbar3 (unsigned char v) { setMIDIDrawBar (3, v);}
static void setDrawbar4 (unsigned char v) { setMIDIDrawBar (4, v);}
static void setDrawbar5 (unsigned char v) { setMIDIDrawBar (5, v);}
static void setDrawbar6 (unsigned char v) { setMIDIDrawBar (6, v);}
static void setDrawbar7 (unsigned char v) { setMIDIDrawBar (7, v);}
static void setDrawbar8 (unsigned char v) { setMIDIDrawBar (8, v);}

static void setDrawbar9  (unsigned char v) { setMIDIDrawBar ( 9, v);}
static void setDrawbar10 (unsigned char v) { setMIDIDrawBar (10, v);}
static void setDrawbar11 (unsigned char v) { setMIDIDrawBar (11, v);}
static void setDrawbar12 (unsigned char v) { setMIDIDrawBar (12, v);}
static void setDrawbar13 (unsigned char v) { setMIDIDrawBar (13, v);}
static void setDrawbar14 (unsigned char v) { setMIDIDrawBar (14, v);}
static void setDrawbar15 (unsigned char v) { setMIDIDrawBar (15, v);}
static void setDrawbar16 (unsigned char v) { setMIDIDrawBar (16, v);}
static void setDrawbar17 (unsigned char v) { setMIDIDrawBar (17, v);}

static void setDrawbar18 (unsigned char v) { setMIDIDrawBar (18, v);}
static void setDrawbar19 (unsigned char v) { setMIDIDrawBar (19, v);}
static void setDrawbar20 (unsigned char v) { setMIDIDrawBar (20, v);}
static void setDrawbar21 (unsigned char v) { setMIDIDrawBar (21, v);}
static void setDrawbar22 (unsigned char v) { setMIDIDrawBar (22, v);}
static void setDrawbar23 (unsigned char v) { setMIDIDrawBar (23, v);}
static void setDrawbar24 (unsigned char v) { setMIDIDrawBar (24, v);}
static void setDrawbar25 (unsigned char v) { setMIDIDrawBar (25, v);}
static void setDrawbar26 (unsigned char v) { setMIDIDrawBar (26, v);}

/**
 * This routine controls percussion from a MIDI controller.
 * It turns percussion on and off and switches between normal and soft.
 * On a slider-type controller you get:
 *
 *  off   on normal       on soft        off
 * 0---16-------------64-------------112------127
 *
 */
static void setPercVolumeFromMIDI (unsigned char u) {
  if (u < 64) {
    if (u < 16) {
      setPercussionEnabled (FALSE); /* off */
    }
    else {
      setPercussionEnabled (TRUE); /* on */
      setPercussionVolume (FALSE); /* normal volume */
    }
  }
  else if (u < 112) {
    setPercussionEnabled (TRUE); /* on */
    setPercussionVolume (TRUE);	/* soft volume */
  }
  else {
    setPercussionEnabled (FALSE); /* off */
  }
}

/**
 * This routine controls percussion from a MIDI controller.
 * It sets fast or slow decay.
 */
static void setPercDecayFromMIDI (unsigned char u) {
  if (u < 64) {
    setPercussionFast (TRUE);
  }
  else {
    setPercussionFast (FALSE);
  }
}

/**
 * This routine controls percussion from a MIDI controller.
 * It sets the third or second harmonic.
 */
static void setPercHarmonicFromMIDI (unsigned char u) {
  if (u < 64) {
    setPercussionFirst (FALSE);
  }
  else {
    setPercussionFirst (TRUE);
  }
}

/**
 * This routine controls the swell pedal from a MIDI controller.
 */
static void setSwellPedalFromMIDI (unsigned char u) {
  swellPedalGain = (outputLevelTrim * ((double) u)) / 127.0;
}

/**
 * This routine initialises this module. When we come here during startup,
 * configuration files have already been read, so parameters should already
 * be set.
 */
void initToneGenerator () {
  int i;

  /* init global variables */
  percIsSoft=percIsFast=0;
  percEnvGain=0;
  for (i=0; i< NOF_BUSES; ++i) {
    int j;
    drawBarGain[i]=0;
    for (j=0; j< 9; ++j) {
      drawBarLevel[i][j] = 0;
    }
  }
  for (i=0; i< MAX_KEYS; ++i)
    activeKeys[i] = 0;
  for (i=0; i< CR_PGMMAX; ++i)
    memset((void*)&corePgm[i], sizeof(CoreIns), 0);
  for (i=0; i< NOF_WHEELS + 1; ++i)
    memset((void*)&oscillators[i], sizeof(struct _oscillator), 0);
  for (i=0; i< 128; ++i) {
    eqvAtt[i]=0.0; eqvSet[i]='\0';
  }

  if (envAtkClkMinLength<0) {
    envAtkClkMinLength = floor(SampleRateD * 8.0 / 22050.0);
  }
  if (envAtkClkMaxLength<0) {
    envAtkClkMaxLength = ceil(SampleRateD * 40.0 / 22050.0);
  }

  if (envAtkClkMinLength > BUFFER_SIZE_SAMPLES) {
    envAtkClkMinLength = BUFFER_SIZE_SAMPLES;
  }
  if (envAtkClkMaxLength > BUFFER_SIZE_SAMPLES) {
    envAtkClkMaxLength = BUFFER_SIZE_SAMPLES;
  }

  applyDefaultConfiguration ();

#if 0
  dumpConfigLists ("osc_cfglists.txt");
#endif

  compilePlayMatrix ();

#if 0
  dumpRuntimeData ("osc_runtime.txt");
#endif

  /* Allocate taper buffers, initialize oscillator structs, build keyOsc. */
  initOscillators (tgVariant, tgPrecision);

#ifdef KEYCOMPRESSION

  initKeyCompTable ();

#endif /* KEYCOMPRESSION */

  initEnvelopes ();

  /* Initialise drawbar gain values */

  for (i = 0; i < NOF_BUSES; i++) {
    int setting;
    for (setting = 0; setting < 9; setting++) {
      float u = (float) setting;
      drawBarLevel[i][setting] = u / 8.0;
    }
  }

#if 1
  /* Gives the lower drawbars a temporary initial value */
  setMIDIDrawBar ( 9, 8);
  setMIDIDrawBar (10, 3);
  setMIDIDrawBar (11, 8);

  setMIDIDrawBar (18, 8);
  setMIDIDrawBar (20, 6);
#endif

  setPercussionFirst (FALSE);
  setPercussionVolume (FALSE);
  setPercussionFast (TRUE);
  setPercussionEnabled (FALSE);

  useMIDIControlFunction ("swellpedal1", setSwellPedalFromMIDI);
  useMIDIControlFunction ("swellpedal2", setSwellPedalFromMIDI);

  useMIDIControlFunction ("upper.drawbar16",  setDrawbar0);
  useMIDIControlFunction ("upper.drawbar513", setDrawbar1);
  useMIDIControlFunction ("upper.drawbar8",   setDrawbar2);
  useMIDIControlFunction ("upper.drawbar4",   setDrawbar3);
  useMIDIControlFunction ("upper.drawbar223", setDrawbar4);
  useMIDIControlFunction ("upper.drawbar2",   setDrawbar5);
  useMIDIControlFunction ("upper.drawbar135", setDrawbar6);
  useMIDIControlFunction ("upper.drawbar113", setDrawbar7);
  useMIDIControlFunction ("upper.drawbar1",   setDrawbar8);

  useMIDIControlFunction ("lower.drawbar16",  setDrawbar9);
  useMIDIControlFunction ("lower.drawbar513", setDrawbar10);
  useMIDIControlFunction ("lower.drawbar8",   setDrawbar11);
  useMIDIControlFunction ("lower.drawbar4",   setDrawbar12);
  useMIDIControlFunction ("lower.drawbar223", setDrawbar13);
  useMIDIControlFunction ("lower.drawbar2",   setDrawbar14);
  useMIDIControlFunction ("lower.drawbar135", setDrawbar15);
  useMIDIControlFunction ("lower.drawbar113", setDrawbar16);
  useMIDIControlFunction ("lower.drawbar1",   setDrawbar17);

  useMIDIControlFunction ("pedal.drawbar16",  setDrawbar18);
  useMIDIControlFunction ("pedal.drawbar513", setDrawbar19);
  useMIDIControlFunction ("pedal.drawbar8",   setDrawbar20);
  useMIDIControlFunction ("pedal.drawbar4",   setDrawbar21);
  useMIDIControlFunction ("pedal.drawbar223", setDrawbar22);
  useMIDIControlFunction ("pedal.drawbar2",   setDrawbar23);
  useMIDIControlFunction ("pedal.drawbar135", setDrawbar24);
  useMIDIControlFunction ("pedal.drawbar113", setDrawbar25);
  useMIDIControlFunction ("pedal.drawbar1",   setDrawbar26);

  useMIDIControlFunction ("percussion.enable",   setPercVolumeFromMIDI);
  useMIDIControlFunction ("percussion.decay",    setPercDecayFromMIDI);
  useMIDIControlFunction ("percussion.harmonic", setPercHarmonicFromMIDI);

#if 0
  dumpOscToText ("osc.txt");
#endif
}

void freeListElements (ListElement *lep) {
  ListElement *l = lep;
  while (l) {
    ListElement *t = l;
    l=l->next;
    free(t);
  }
}

void freeToneGenerator () {
  freeListElements(leConfig);
  freeListElements(leRuntime);
  int i;
  for (i=1; i <= NOF_WHEELS + 1; i++) {
    if (oscillators[i].wave) free(oscillators[i].wave);
  }
}


/**
 * This function is the entry point for the MIDI parser when it has received
 * a NOTE OFF message on a channel and note number mapped to a playing key.
 */
void oscKeyOff (unsigned char keyNumber) {
  if (MAX_KEYS <= keyNumber) return;
  /* The key must be marked as on */
  if (activeKeys[keyNumber] != 0) {
    /* Flag the key as inactive */
    activeKeys[keyNumber] = 0;
    /* Track upper manual keys for percussion trigger */
    if (keyNumber < 64) {
      upperKeyCount--;
    }
#ifdef KEYCOMPRESSION
    keyDownCount--;
    assert (0 <= keyDownCount);
#endif /* KEYCOMPRESSION */
    /* Write message saying that the key is released */
    *msgQueueWriter++ = MSG_KEY_OFF(keyNumber);
    /* Check for wrap on message queue */
    if (msgQueueWriter == msgQueueEnd) {
      msgQueueWriter = msgQueue;
    }
  } /* if key was active */

  /*  printf ("\rOFF:%3d", keyNumber); fflush (stdout); */

}

/**
 * This function is the entry point for the MIDI parser when it has received
 * a NOTE ON message on a channel and note number mapped to a playing key.
 */
void oscKeyOn (unsigned char keyNumber) {
  if (MAX_KEYS <= keyNumber) return;
  /* If the key is already depressed, release it first. */
  if (activeKeys[keyNumber] != 0) {
    oscKeyOff (keyNumber);
  }
  /* Mark the key as active */
  activeKeys[keyNumber] = 1;
  /* Track upper manual for percussion trigger */
  if (keyNumber < 64) {
    upperKeyCount++;
  }
#ifdef KEYCOMPRESSION
  keyDownCount++;
#endif /* KEYCOMPRESSION */
  /* Write message */
  *msgQueueWriter++ = MSG_KEY_ON(keyNumber);
  /* Check for wrap on message queue */
  if (msgQueueWriter == msgQueueEnd) {
    msgQueueWriter = msgQueue;
  }

  /*  printf ("\rON :%3d", keyNumber); fflush (stdout); */

}

/* ----------------------------------------------------------------
 * Tonegenerator version 3, 16-jul-2004
 * ----------------------------------------------------------------*/

/*
 * This routine is where the next buffer of output sound is assembled.
 * The routine goes through the following phases:
 *
 *   Process the message queue
 *   Process the activated list
 *   Process the removal list
 *   Execute the core program interpreter
 *   Mixdown
 *
 * The message queue holds the numbers of keys (MIDI playing keys)
 * that has been closed or released since the last time this function
 * was called. For each key, its contributions of how oscillators are
 * fed to buses (drawbar rails) is analysed. The changes that typically
 * occur are:
 *   (a) Oscillators that are not already sounding are activated.
 *   (b) Oscillators that are already sounding have their volumes altered.
 *   (c) Oscillators that are already sounding are deactivated.
 *
 * The list of active oscillators is processed and instructions for
 * the core interpreter are written. Each instruction refers to a single
 * oscillator and specifies how its samples should be written to the
 * three mixing buffers: swell, vibrato and percussion. Oscillators
 * that alter their volume as a result of key action picked up from the
 * message queue, are modulated by an envelope curve. Sometimes an extra
 * instruction is needed to manage a wrap of the oscillator's sample buffer.
 *
 * The removal list contains deactivated oscillators that are to be removed
 * from the list of active oscillators. This phase takes care of that.
 *
 * The core interpreter runs the core program which mixes the proper
 * number of samples from each active oscillator into the swell, vibrato
 * and percussion buffers, while applying envelope.
 *
 * The mixdown phase runs the vibrato buffer through the vibrato FX (when
 * activated), and then mixes the swell, vibrato output and percussion
 * buffers to the output buffer. The percussion buffer is enveloped by
 * the current percussion envelope and the whole mix is subject to the
 * swell pedal volume control.
 *
 * As a side note, the above sounds like a lot of work, but the most common
 * case is either complete silence (in which case the activated list is
 * empty) or no change to the activated list. Effort is only needed when
 * there are changes to be made, and human fingers are typically quite
 * slow. Sequencers, however, may put some strain on things.
 */
void oscGenerateFragment (float * buf, size_t lengthSamples) {

  int i;
  float * yptr = buf;
  struct _oscillator * osp;
  unsigned int copyDone = 0;
  unsigned int recomputeRouting;
  static unsigned short removedList[NOF_WHEELS + 1]; /* Flags modified aots */
  static float swlBuffer[BUFFER_SIZE_SAMPLES];
  static float vibBuffer[BUFFER_SIZE_SAMPLES];
  static float vibYBuffr[BUFFER_SIZE_SAMPLES];
  static float prcBuffer[BUFFER_SIZE_SAMPLES];
  int removedEnd = 0;

#ifdef KEYCOMPRESSION

  static float keyCompLevel = KEYCOMP_ZERO_LEVEL;
  float keyComp = keyCompTable[keyDownCount];
  float keyCompDelta;
#define KEYCOMPCHASE() {keyCompLevel += keyCompDelta;}

#else

#define KEYCOMPCHASE()

#endif /* KEYCOMPRESSION */

  /* End of declarations */

#ifdef KEYCOMPRESSION

  keyCompDelta = (keyComp - keyCompLevel) / (float) BUFFER_SIZE_SAMPLES;

#endif /* KEYCOMPRESSION */

  /* Reset the core program */
  coreWriter = coreReader = corePgm;

  /* ================================================================
		     M E S S S A G E   Q U E U E
     ================================================================ */

  while (msgQueueReader != msgQueueWriter) {

    unsigned short msg = *msgQueueReader++; /* Read next message */
    int keyNumber;
    ListElement * lep;

    /* Check wrap on message queue */
    if (msgQueueReader == msgQueueEnd) {
      msgQueueReader = msgQueue;
    }

    if (MSG_GET_MSG(msg) == MSG_MKEYON) {
      keyNumber = MSG_GET_PRM(msg);
      for (lep = keyContrib[keyNumber]; lep != NULL; lep = lep->next) {
	int wheelNumber = LE_WHEEL_NUMBER_OF(lep);
	osp = &(oscillators[wheelNumber]);

	if (aot[wheelNumber].refCount == 0) {
	  /* Flag the oscillator as added and modified */
	  osp->rflags = OR_ADD;
	  /* If not already on the active list, add it */
	  if (osp->aclPos == -1) {
	    osp->aclPos = activeOscLEnd;
	    activeOscList[activeOscLEnd++] = wheelNumber;
	  }
	}
	else {
	  osp->rflags |= ORF_MODIFIED;
	}

	aot[wheelNumber].busLevel[LE_BUSNUMBER_OF(lep)] += LE_LEVEL_OF(lep);
	aot[wheelNumber].keyCount[LE_BUSNUMBER_OF(lep)] += 1;
	aot[wheelNumber].refCount += 1;
      }

    }
    else if (MSG_GET_MSG(msg) == MSG_MKEYOFF) {
      keyNumber = MSG_GET_PRM(msg);
      for (lep = keyContrib[keyNumber]; lep != NULL; lep = lep->next) {
	int wheelNumber = LE_WHEEL_NUMBER_OF(lep);
	osp = &(oscillators[wheelNumber]);

	aot[wheelNumber].busLevel[LE_BUSNUMBER_OF(lep)] -= LE_LEVEL_OF(lep);
	aot[wheelNumber].keyCount[LE_BUSNUMBER_OF(lep)] -= 1;
	aot[wheelNumber].refCount -= 1;

	assert (0 <= aot[wheelNumber].refCount);
	assert (-1 < osp->aclPos); /* Must be on the active osc list */

	if (aot[wheelNumber].refCount == 0) {
	  osp->rflags = OR_REM;
	}
	else {
	  osp->rflags |= ORF_MODIFIED;
	}
      }
    }
    else {
      assert (0);
    }
  } /* while message queue reader */

  /* ================================================================
		     A C T I V A T E D   L I S T
     ================================================================ */

  if ((recomputeRouting = (oldRouting != newRouting))) {
    oldRouting = newRouting;
  }


  /*
   * At this point, new oscillators has been added to the active list
   * and removed oscillators are still on the list.
   */

  for (i = 0; i < activeOscLEnd; i++) {

    int oscNumber = activeOscList[i]; /* Get the oscillator number */
    AOTElement * aop = &(aot[oscNumber]); /* Get a pointer to active struct */
    osp = &(oscillators[oscNumber]); /* Point to the oscillator */

    if (osp->rflags & ORF_REMOVED) { /* Decay instruction for removed osc. */
      /* Put it on the removal list */
      removedList[removedEnd++] = oscNumber;

      /* All envelopes, both attack and release must traverse 0-1. */

      coreWriter->env = releaseEnv[i & 7];

      if (copyDone) {
	coreWriter->opr = CR_ADDENV;
      } else {
	coreWriter->opr = CR_CPYENV;
	copyDone = 1;
      }

      coreWriter->src = osp->wave + osp->pos;
      coreWriter->off = 0;	/* Start at the beginning of target buffers */
      coreWriter->sgain = aop->sumSwell;
      coreWriter->pgain = aop->sumPercn;
      coreWriter->vgain = aop->sumScanr;
      /* Target gain is zero */
      coreWriter->nsgain = coreWriter->npgain = coreWriter->nvgain = 0.0;

      if (osp->lengthSamples < (osp->pos + BUFFER_SIZE_SAMPLES)) {
	/* Need another instruction because of wrap */
	CoreIns * prev = coreWriter;
	coreWriter->cnt = osp->lengthSamples - osp->pos;
	osp->pos = BUFFER_SIZE_SAMPLES - coreWriter->cnt;
	coreWriter += 1;
	coreWriter->opr = prev->opr;
	coreWriter->src = osp->wave;
	coreWriter->off = prev->cnt;
	coreWriter->env = prev->env + prev->cnt;

	coreWriter->sgain = prev->sgain;
	coreWriter->pgain = prev->pgain;
	coreWriter->vgain = prev->vgain;

	coreWriter->nsgain = prev->nsgain;
	coreWriter->npgain = prev->npgain;
	coreWriter->nvgain = prev->nvgain;

	coreWriter->cnt = osp->pos;
      }
      else {
	coreWriter->cnt = BUFFER_SIZE_SAMPLES;
	osp->pos += BUFFER_SIZE_SAMPLES;
      }

      coreWriter += 1;
    }
    else {			/* ADD or MODIFIED */
      int reroute = 0;

      /*
       * Copy the current gains. For unmodified oscillators these will be
       * used. For modified oscillators we provide the update below.
       */
      if (osp->rflags & ORF_ADDED) {
	coreWriter->sgain = coreWriter->pgain = coreWriter->vgain = 0.0;
      }
      else {
	coreWriter->sgain = aop->sumSwell;
	coreWriter->pgain = aop->sumPercn;
	coreWriter->vgain = aop->sumScanr;
      }

      /* Update the oscillator's contribution to each busgroup mix */

      if ((osp->rflags & ORF_MODIFIED) || drawBarChange) {
	int d;
	float sum = 0.0;

	for (d = UPPER_BUS_LO; d < UPPER_BUS_END; d++) {
	  sum += aop->busLevel[d] * drawBarGain[d];
	}
	aop->sumUpper = sum;
	sum = 0.0;
	for (d = LOWER_BUS_LO; d < LOWER_BUS_END; d++) {
	  sum += aop->busLevel[d] * drawBarGain[d];
	}
	aop->sumLower = sum;
	sum = 0.0;
	for (d = PEDAL_BUS_LO; d < PEDAL_BUS_END; d++) {
	  sum += aop->busLevel[d] * drawBarGain[d];
	}
	aop->sumPedal = sum;
	reroute = 1;
      }

      /* If the group mix or routing has changed */

      if (reroute || recomputeRouting) {

	if (oldRouting & RT_PERC) { /* Percussion */
	  aop->sumPercn = aop->busLevel[percSendBus];
	}
	else {
	  aop->sumPercn = 0.0;
	}

	aop->sumScanr = 0.0;	/* Initialize scanner level */
	aop->sumSwell = aop->sumPedal; /* Initialize swell level */

	if (oldRouting & RT_UPPRVIB) { /* Upper manual ... */
	  aop->sumScanr += aop->sumUpper; /* ... to vibrato */
	}
	else {
	  aop->sumSwell += aop->sumUpper; /* ... to swell pedal */
	}

	if (oldRouting & RT_LOWRVIB) { /* Lower manual ... */
	  aop->sumScanr += aop->sumLower; /* ... to vibrato */
	}
	else {
	  aop->sumSwell += aop->sumLower; /* ... to swell pedal */
	}
      }	/* if rerouting */

      /* Emit instructions for oscillator */
      if (osp->rflags & OR_ADD) {
	/* Envelope attack instruction */
	coreWriter->env = attackEnv[i & 7];
	/* Next gain values */
	coreWriter->nsgain = aop->sumSwell;
	coreWriter->npgain = aop->sumPercn;
	coreWriter->nvgain = aop->sumScanr;

	if (copyDone) {
	  coreWriter->opr = CR_ADDENV;
	}
	else {
	  coreWriter->opr = CR_CPYENV;
	  copyDone = 1;
	}
      }
      else {
	if (copyDone) {
	  coreWriter->opr = CR_ADD;
	}
	else {
	  coreWriter->opr = CR_CPY;
	  copyDone = 1;
	}
      }

      /* The source is the wave of the oscillator at its current position */
      coreWriter->src = osp->wave + osp->pos;
      coreWriter->off = 0;


      if (osp->lengthSamples < (osp->pos + BUFFER_SIZE_SAMPLES)) {
	/* Instruction wraps source buffer */
	CoreIns * prev = coreWriter; /* Refer to the first instruction */
	coreWriter->cnt = osp->lengthSamples - osp->pos; /* Set len count */
	osp->pos = BUFFER_SIZE_SAMPLES - coreWriter->cnt; /* Updat src pos */

	coreWriter += 1;	/* Advance to next instruction */

	coreWriter->opr = prev->opr; /* Same operation */
	coreWriter->src = osp->wave; /* Start of wave because of wrap */
	coreWriter->off = prev->cnt;
	if (coreWriter->opr & 2) {
	  coreWriter->env = prev->env + prev->cnt; /* Continue envelope */
	}
	/* The gains are identical to the previous instruction */
	coreWriter->sgain = prev->sgain;
	coreWriter->pgain = prev->pgain;
	coreWriter->vgain = prev->vgain;

	coreWriter->nsgain = prev->nsgain;
	coreWriter->npgain = prev->npgain;
	coreWriter->nvgain = prev->nvgain;

	coreWriter->cnt = osp->pos; /* Up to next read position */
      }
      else {
	coreWriter->cnt = BUFFER_SIZE_SAMPLES;
	osp->pos += BUFFER_SIZE_SAMPLES;
      }

      coreWriter += 1;	/* Advance to next instruction */


    } /* else aot element not removed, ie modified or added */

    /* Clear rendering flags */
    osp->rflags = 0;

  } /* for the active list */

  drawBarChange = 0;

  /* ================================================================
		       R E M O V A L   L I S T
     ================================================================ */

  /*
   * Core instructions are now written.
   * Process the removed entries list. [Could action be merged above?]
   */
  for (i = 0; i < removedEnd; i++) {
    int vicosc = removedList[i]; /* Victim oscillator number */
    int actidx = oscillators[vicosc].aclPos; /* Victim's active index */
    oscillators[vicosc].aclPos = -1; /* Reset victim's backindex */
    activeOscLEnd--;

    assert (0 <= activeOscLEnd);

    if (0 < activeOscLEnd) {	/* If list is not yet empty ... */
      int movosc = activeOscList[activeOscLEnd]; /* Fill hole w. last entry */
      if (movosc != vicosc) {
	activeOscList[actidx] = movosc;
	oscillators[movosc].aclPos = actidx;
      }
    }
  }

  /* ================================================================
		   C O R E   I N T E R P R E T E R
     ================================================================ */

  /*
   * Special case: silence. If the vibrato scanner is used we must run zeros
   * through it because it is stateful (has a delay line).
   * We could possibly be more efficient here but for the moment we just zero
   * the input buffers to the mixing stage and reset the percussion.
   */

  if (coreReader == coreWriter) {
    float * ys = swlBuffer;
    float * yv = vibBuffer;
    float * yp = prcBuffer;

    for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
      *ys++ = 0.0;
      *yv++ = 0.0;
      *yp++ = 0.0;
    }

  }

  for (; coreReader < coreWriter; coreReader++) {
    short opr = coreReader->opr;
    float * ys = swlBuffer + coreReader->off;
    float * yv = vibBuffer + coreReader->off;
    float * yp = prcBuffer + coreReader->off;
    float   gs = coreReader->sgain;
    float   gv = coreReader->vgain;
    float   gp = coreReader->pgain;
    float   ds = coreReader->nsgain - gs; /* Move these three down */
    float   dv = coreReader->nvgain - gv;
    float   dp = coreReader->npgain - gp;
    float * ep  = coreReader->env;
    float * xp  = coreReader->src;
    int     n  = coreReader->cnt;

    if (opr & 1) {		/* ADD and ADDENV */
      if (opr & 2) {		/* ADDENV */
	for (; 0 < n; n--) {
/*	  float x = (float) ((((int) (*xp++)) * (*ep++)) >> ENV_NORM); */
	  float x = (float) (*xp++);
	  float e = *ep++;
	  *ys++ += x * (gs + (e * ds));
	  *yv++ += x * (gv + (e * dv));
	  *yp++ += x * (gp + (e * dp));
	}
      } else {			/* ADD */
	for (; 0 < n; n--) {
	  float x = (float) (*xp++);
	  *ys++ += x * gs;
	  *yv++ += x * gv;
	  *yp++ += x * gp;
	}
      }

    } else {

      if (opr & 2) {		/* CPY and CPYENV */
	for (; 0 < n; n--) {	/* CPYENV */
/*	  float x = (float) ((((int) (*xp++)) * (*ep++)) >> ENV_NORM); */
	  float x = (float) (*xp++);
	  float e = *ep++;
	  *ys++ = x * (gs + (e * ds));
	  *yv++ = x * (gv + (e * dv));
	  *yp++ = x * (gp + (e * dp));
	}

      } else {

	for (; 0 < n; n--) {	/* CPY */
	  float x = (float) (*xp++);
	  *ys++ = x * gs;
	  *yv++ = x * gv;
	  *yp++ = x * gp;
	}

      }
    }
  } /* while there are core instructions */

  /* ================================================================
			    M I X D O W N
     ================================================================ */

  /*
   * The percussion, sweel and scanner buffers are now written.
   */

  /* If anything is routed through the scanner, apply FX and get outbuffer */

  if (oldRouting & RT_VIB) {
#if 1
    vibratoProc (vibBuffer, vibYBuffr, BUFFER_SIZE_SAMPLES);
#else
    size_t ii;
    for (ii=0;ii< BUFFER_SIZE_SAMPLES;++ii) vibYBuffr[ii]=0.0;
#endif

  }

  /* Mix buffers, applying percussion and swell pedal. */

  {
    float * xp = swlBuffer;
    float * vp = vibYBuffr;
    float * pp = prcBuffer;
    static float outputGain = 1.0;


#ifdef HIPASS_PERCUSSION
    static float pz;
#endif /* HIPASS_PERCUSSION */

    if (oldRouting & RT_PERC) {	/* If percussion is on */
#ifdef HIPASS_PERCUSSION
      float * tp = &(prcBuffer[BUFFER_SIZE_SAMPLES - 1]);
      float temp = *tp;
      pp = tp - 1;
      for (i = 1; i < BUFFER_SIZE_SAMPLES; i++) {
	*tp = *pp - *tp;
	tp--;
	pp--;
      }
      *tp = pz - *tp;
      pz = temp;
      pp = prcBuffer;
#endif /* HIPASS_PERCUSSION */
      outputGain = swellPedalGain * percDrawbarGain;
      if (oldRouting & RT_VIB) { /* If vibrato is on */
	for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) { /* Perc and vibrato */
	  *yptr++ =
	    (outputGain * keyCompLevel *
	     ((*xp++) + (*vp++) + ((*pp++) * percEnvGain)));
	  percEnvGain *= percEnvGainDecay;
	  KEYCOMPCHASE();
	}
      } else {			/* Percussion only */
	for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
	  *yptr++ =
	    (outputGain * keyCompLevel * ((*xp++) + ((*pp++) * percEnvGain)));
	  percEnvGain *= percEnvGainDecay;
	  KEYCOMPCHASE();
	}
      }

    } else if (oldRouting & RT_VIB) { /* No percussion and vibrato */

      for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
	*yptr++ =
	  (swellPedalGain * keyCompLevel * ((*xp++) + (*vp++)));
	KEYCOMPCHASE();
      }
    } else {			/* No percussion and no vibrato */
      for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
	*yptr++ =
	  (swellPedalGain * keyCompLevel * (*xp++));
	KEYCOMPCHASE();
      }
    }
  }

  if (upperKeyCount == 0) {
    percEnvGain = percEnvGainReset;
  }
} /* oscGenerateFragment */

/* vi:set ts=8 sts=2 sw=2: */
