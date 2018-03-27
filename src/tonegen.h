/* setBfree - DSP tonewheel organ
 *
 * Copyright (C) 2003-2004 Fredrik Kilander <fk@dsv.su.se>
 * Copyright (C) 2008-2015 Robin Gareus <robin@gareus.org>
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * tonegen.h
 */
#ifndef TONEGEN_H
#define TONEGEN_H

#ifndef M_PI
# define M_PI		3.14159265358979323846	/* pi */
#endif

#include "cfgParser.h"
#include "vibrato.h"

#define TG_91FB00 0		/* 91 wheels no lower foldback */
#define TG_82FB09 1		/* 82 wheels, 9 note lower folback */
#define TG_91FB12 2		/* 91 wheels, 12 note lower foldback */

#define firstMIDINote 36
#define lastMIDINote 108

#define ENV_CLICK  0		/* Key-click spark noise simulation */
#define ENV_COSINE 1		/* Cosine, bell, sigmoid fade */
#define ENV_LINEAR 2		/* Linear fade */
#define ENV_SHELF  3		/* Step */
#define ENV_CLICKMODELS 4

#define NOF_BUSES 27		/* Nof of drawbars/buses */

#define BUFFER_SIZE_SAMPLES 128


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
  size_t  pos;			/**< Read position */

  int     aclPos;		/**< Position in active list */
  unsigned short rflags;	/**< Rendering flags */

};


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


struct b_tonegen {

/**
 * The leConfig pointer points to ListElements allocated during config.
 * The referenced memory is released once config is complete.
 */

ListElement * leConfig;

/** The leRuntime pointer points to ListElements allocated for playing. */

ListElement * leRuntime;

/**
 * The Active Oscillator Table has one element (struct) for each wheel.
 * When a manual key is depressed, wheel, bus and gain data from the
 * keyContrib array is added to the corresponding rows (index by wheel)
 * and the sums are updated.
 */
AOTElement aot[NOF_WHEELS + 1];

/**
 * The numbers of sounding oscillators/wheels are placed on this list.
 */
int activeOscList[NOF_WHEELS + 1];
int activeOscLEnd; /**< end of activeOscList */


/**
 * The size of the message queue.
 */
#define MSGQSZ 1024
unsigned short   msgQueue [MSGQSZ]; /**< Message queue ringbuffer - MIDI->Synth */

unsigned short * msgQueueWriter; /**< message-queue srite pointer */
unsigned short * msgQueueReader; /**< message-queue read pointer */
unsigned short * msgQueueEnd;

/*
 * When HIPASS_PERCUSSION is defined it will do two things:
 *  - Insert a high-pass filter in the percussion signal
 *  - Initialise the variable percEnvScaling to a higher value to compensate
 *    for the psycho-acoustic drop in percussion volume when there is less
 *    bass in the signal.
 */

#define HIPASS_PERCUSSION



/* Keycompression */

#define KEYCOMPRESSION

#ifdef KEYCOMPRESSION
#define MAXKEYS 128
float keyCompTable[MAXKEYS];
int   keyDownCount;
#define KEYCOMP_ZERO_LEVEL 1.0
#endif /* KEYCOMPRESSION */


#define CR_PGMMAX 256		/* Max length of a core program */

/*
 * 2002-11-17/FK: It may be prudent to remember that we do not check
 * for overflow. 256/18=14 keys. Worst case scenario should be
 * 128 * 9 * 2 = 2304. Does that length hurt performance? Probably not
 * as much as an overrun will.
 */

CoreIns   corePgm[CR_PGMMAX];
CoreIns * coreWriter;
CoreIns * coreReader;

/* Attack and release buffer envelopes for 9 buses. */

float attackEnv[9][BUFFER_SIZE_SAMPLES]; /**< Attack envelope buffer for 9 buses */
float releaseEnv[9][BUFFER_SIZE_SAMPLES];/**< Release envelope buffer for 9 buses */


int envAttackModel;
int envReleaseModel;
float envAttackClickLevel;
float envReleaseClickLevel;

/** Minimum random length (in samples) of attack key click noise burst. */
int envAtkClkMinLength;
/** Maximum random length (in samples) of attack key click noise burst. */
int envAtkClkMaxLength;

unsigned int newRouting;
unsigned int oldRouting;

unsigned int percSendBus;
unsigned int percSendBusA;
unsigned int percSendBusB;

unsigned int upperKeyCount;


/**
 * Swell pedal (volume control)
 */
float swellPedalGain;

/**
 * Output level trim. Used to trim the overall output level.
 */
float outputLevelTrim;

/**
 * Our master tuning frequency.
 */
double tuning;

/**
 * When gearTuning is FALSE, the tuning is equal-tempered.
 * When TRUE, the tuning is based on the integer ratio approximations found
 * in the mechanical tone generator of the original instrument. The resulting
 * values are very close to the real thing.
 */
int gearTuning;

/*
 * Not all of these are used.
 */
struct _oscillator oscillators [NOF_WHEELS + 1];

/*
 * Vector of active keys, used to correctly manage
 * sounding and non-sounding keys.
 * boolean 0,1
 */
unsigned int activeKeys [MAX_KEYS];

/* bitwise compact active keys - used for GUI comm.
 * these are real non-transposed keys (for GUI)
 */
unsigned int _activeKeys [MAX_KEYS/32];

/**
 * The array drawBarGain holds the instantaneous amplification value for
 * each of the drawbars.
 */
float drawBarGain[NOF_BUSES];

/**
 * The drawBarLevel table holds the possible drawbar amplification values
 * for all drawbars and settings. When a drawbar change is applied, the
 * appropriate value is copied from the table and installed in drawBarGain[].
 */
float drawBarLevel[NOF_BUSES][9];

/**
 * The drawBarChange flag is set by the routine that effectuates a drawbar
 * change. The oscGenerateFragment routine then checks the flag, computes
 * new composed gain values, and resets the flag.
 */
unsigned short drawBarChange;

/**
 * True when percussion is enabled.
 */
int percEnabled;

/**
 * With percussion enabled, the output from the this bus is muted, to
 * simulate its operation as the percussion trigger. A value of -1
 * disables the muting feature.
 */
int percTriggerBus;

/**
 * While percussion AND trigger bus muting are active, this variable
 * holds the drawbar setting to restore to the trigger bus once
 * percussion is disabled.
 */
int percTrigRestore;

int percIsSoft;		/**< Runtime toggle */
int percIsFast;		/**< Runtime toggle */

double percFastDecaySeconds; /**< Config parameter */
double percSlowDecaySeconds; /**< Config parameter */

/* Alternate percussion 25-May-2003/FK */

/**
 * Runtime: The instantaneous gain applied to the percussion signal.
 */
float percEnvGain;

/**
 * Runtime: The starting value of percEnvGain when all keys are released.
 */
float percEnvGainReset;

/**
 * Runtime: The percEnvGain value is updated by being multiplied by this
 * variable between each sample. The value is close to, but less than 1.
 */
float percEnvGainDecay;

/**
 * Config: scaling factor used to raise the percussion gain reset level
 * to audible levels. Can be viewed as overall 'volume' for percussion.
 */
float percEnvScaling;

/**
 * Runtime: Normal volume reset value, copied to percEnvGainReset when
 * normal percussion is selected.
 */
float percEnvGainResetNorm;

/**
 * Runtime: Soft volume reset value, copied to percEnvGainReset when
 * soft percussion is selected.
 */
float percEnvGainResetSoft;

/**
 * Runtime: Fast and Normal decay multiplier, copied to percEnvGainDecay
 * when that combination is selected.
 */
float percEnvGainDecayFastNorm;

/**
 * Runtime: Fast and Soft decay multiplier, copied to percEnvGainDecay
 * when that combination is selected.
 */
float percEnvGainDecayFastSoft;

/**
 * Runtime: Slow and Normal decay multiplier, copied to percEnvGainDecay
 * when that combination is selected.
 */
float percEnvGainDecaySlowNorm;

/**
 * Runtime: Slow and Soft decay multiplier, copied to percEnvGainDecay
 * when that combination is selected.
 */
float percEnvGainDecaySlowSoft;

/**
 * Drawbar gain when the percussion NORMAL/SOFT is in the NORMAL position.
 */
float percDrawbarNormalGain;

/**
 * Drawbar gain when the percussion NORMAL/SOFT is in the SOFT position.
 */
float percDrawbarSoftGain;

/**
 * Runtime drawbar gain.
 */
float percDrawbarGain;

/**
 * This variable determines the model simulated by the tonegenerator
 * and the keyboard wiring.
 */
int tgVariant;

/**
 * This variable determines the precision with which the wave buffers
 * for the tonegenerator are sized and created.
 */
double tgPrecision;


int eqMacro;
double eqvCeiling;	/**< Normalizing manual osc eq. */
double eqvAtt [128];	/**< Values from config file */
char   eqvSet [128];	/**< Value is set by a config command */

/*
 * EQ_SPLINE parameters.
 */
double eqP1y;
double eqR1y;
double eqP4y;
double eqR4y;

/**
 * Default value of crosstalk between tonewheels in the same compartment.
 * The value refers to the amount of rogue signal picked up.
 */
double defaultCompartmentCrosstalk;

/**
 * Default value of crosstalk between transformers on the top of the tg.
 */
double defaultTransformerCrosstalk;

/**
 * Default value of crosstalk between connection on the terminal strip.
 */
double defaultTerminalStripCrosstalk;

/**
 * Default throttle on the crosstalk distribution model for wiring.
 */

double defaultWiringCrosstalk;

/**
 * Signals weaker than this are not put on the contribution list.
 */

double contributionFloorLevel;

/**
 * If non-zero, signals that ARE placed on the contribution have at least
 * this level.
 */

double contributionMinLevel;

/*
 * Default amplitudes of tonewheel harmonics
 */
#define MAX_PARTIALS 12
double wheel_Harmonics [MAX_PARTIALS]; /** < amplitudes of tonewheel harmonics */


/**
 * The wheelHarmonics array is filled in during configuration. The list in
 * index 0 is used as default for non-configured slots.
 * First wheel number is 1, last is 91, hence 92 elements are needed.
 */
ListElement * wheelHarmonics[NOF_WHEELS + 1];

/**
 * The terminalMix array is filled in during configuration. For each terminal
 * on the tonegenerator is describes the mix between wheels heard on the
 * terminal, and is thus responsible for the crosstalk generated in and on
 * the tonegenerator. The identity value should perhaps be assumed?
 */
ListElement * terminalMix[NOF_WHEELS + 1];

/**
 * The keyTaper array is filled in during configuration. For each key and
 * bus contact in the key it describes the tonegenerator terminal connected
 * to it and the taper level.
 */
ListElement * keyTaper[MAX_KEYS];

/**
 * The keyCrosstalk array is filled in during configuration. For each key
 * and bus contact in the key it describes the tonegenerator terminal also
 * heard in addition to the specification in the keyTaper array.
 */
ListElement * keyCrosstalk[MAX_KEYS];

/**
 * The keyContrib array is loaded by routine compilePlayMatrix and is used
 * by the sound runtime to add or remove contribution from wheels to buses
 * as controlled by each key.
 */
ListElement * keyContrib[MAX_KEYS];

unsigned short removedList[NOF_WHEELS + 1];
float swlBuffer[BUFFER_SIZE_SAMPLES];
float vibBuffer[BUFFER_SIZE_SAMPLES];
float vibYBuffr[BUFFER_SIZE_SAMPLES];
float prcBuffer[BUFFER_SIZE_SAMPLES];

float outputGain;

#ifdef HIPASS_PERCUSSION
float pz;
#endif

#ifdef KEYCOMPRESSION
  float keyCompLevel;
#endif

struct b_vibrato inst_vibrato;

void *midi_cfg_ptr;
};

extern void setToneGeneratorModel (struct b_tonegen *t, int variant);
extern void setWavePrecision (struct b_tonegen *t, double precision);
extern void setTuning (struct b_tonegen *t, double refA_Hz);
extern void setVibratoUpper (struct b_tonegen *t, int isEnabled);
extern void setVibratoLower (struct b_tonegen *t, int isEnabled);
extern int  getVibratoRouting (struct b_tonegen *t);
extern void setPercussionEnabled (struct b_tonegen *t, int isEnabled);
extern void setPercussionVolume (struct b_tonegen *t, int isSoft);
extern void setPercussionFast (struct b_tonegen *t, int isFast);
extern void setPercussionFirst (struct b_tonegen *t, int isFirst);
extern void setNormalPercussionPercent (struct b_tonegen *t, int percent);
extern void setSoftPercussionPercent (struct b_tonegen *t, int percent);
extern void setFastPercussionDecay (struct b_tonegen *t, double seconds);
extern void setSlowPercussionDecay (struct b_tonegen *t, double seconds);
extern void setEnvAttackModel (struct b_tonegen *t, int model);
extern void setEnvReleaseModel (struct b_tonegen *t, int model);
extern void setEnvAttackClickLevel (struct b_tonegen *t, double u);
extern void setEnvReleaseClickLevel (struct b_tonegen *t, double u);
extern void setKeyClick (struct b_tonegen *t, int v);
extern int  oscConfig (struct b_tonegen *t, ConfigContext * cfg);
extern const ConfigDoc *oscDoc ();
extern void initToneGenerator (struct b_tonegen *t, void *m);
extern void freeToneGenerator (struct b_tonegen *t);

extern void oscKeyOff (struct b_tonegen *t, unsigned char midiNote, unsigned char realKey);
extern void oscKeyOn (struct b_tonegen *t, unsigned char midiNote, unsigned char realKey);
extern void setDrawBars (void *inst, unsigned int manual, unsigned int setting []);
extern void oscGenerateFragment (struct b_tonegen *t, float * buf, size_t lengthSamples);

struct b_tonegen *allocTonegen();

#endif /* TONEGEN_H */
