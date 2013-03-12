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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <limits.h>
#include <assert.h>

#include "main.h"
#include "midi.h"
#include "midi_types.h"
#include "program.h"
#include "global_inst.h"

/*
 * Symbolic names for functions that can be governed by MIDI controllers.
 * These names are used by the configuration machinery to allow arbitrary
 * mappings of controllers. This is how it works:
 * Other modules, like the tonegenerator and the effects, call the function
 *   useMIDIControlFunction (name, function)
 * with a name and a pointer to a function. The function implements some
 * reaction to a MIDI controller, e.g. a drawbar setting. The name is a
 * symbolic name that identifies the function. The name must be defined
 * in array ccFuncNames[] (immediately below) and its position in the array
 * is used as an opaque numeric id for the reaction. That numeric id is then
 * used to examine the three arrays ctrlUseA, ctrlUseB and ctrlUseC in turn.
 * When the accessed element contain a value less than 128 the function is
 * entered in the corresponding ctrlvecA, ctrlvecB and ctrlvecC table, which
 * are the runtime maps from MIDI controller numbers to functions.
 *
 * If you are with me so far, the tables cltrUse* define how reactions are
 * mapped to MIDI controllers. The ctrlUse* tables are filled in on two
 * occasions. The first, that always take place, is the function
 *   midiPrimeControllerMapping()
 * which simplay provides the default initialization.
 * The second, which MAY happen, is the configuration processing where the
 * user can provide config options on the format:
 *   midi.controller.{upper,lower,pedals}.<cc>=<name>
 * Here, again, the <name> is defined in ccFuncNames and the keywords upper,
 * lower and pedals will determine which of the three ctrlUse* tables that are
 * updated with the controller number <cc>.
 */

static const char * ccFuncNames[] = {
  "upper.drawbar16",
  "upper.drawbar513",
  "upper.drawbar8",
  "upper.drawbar4",
  "upper.drawbar223",
  "upper.drawbar2",
  "upper.drawbar135",
  "upper.drawbar113",
  "upper.drawbar1",

  "lower.drawbar16",
  "lower.drawbar513",
  "lower.drawbar8",
  "lower.drawbar4",
  "lower.drawbar223",
  "lower.drawbar2",
  "lower.drawbar135",
  "lower.drawbar113",
  "lower.drawbar1",

  "pedal.drawbar16",
  "pedal.drawbar513",
  "pedal.drawbar8",
  "pedal.drawbar4",
  "pedal.drawbar223",
  "pedal.drawbar2",
  "pedal.drawbar135",
  "pedal.drawbar113",
  "pedal.drawbar1",

  "percussion.enable",		/* off/normal/soft/off */
  "percussion.decay",		/* fast/slow */
  "percussion.harmonic",	/* 3rd/2nd */

  "vibrato.knob",		/* off/v1/c1/v2/c2/v3/c3 */
  "vibrato.routing",		/* off/lower/upper/both */

  "swellpedal1",		/* Volume, for primary ctrlr (mod wheel) */
  "swellpedal2",		/* Volume, for secondary ctrlr (expression) */

  "rotary.speed-preset",	/* stop, slow, fast, stop */
  "rotary.speed-toggle",	/* sustain pedal */
  "rotary.speed-select",	/* 0..8 (3^2 combinations) [stop/slow/fast]^[horn|drum] */

  "whirl.horn.filter.a.type",
  "whirl.horn.filter.a.hz",
  "whirl.horn.filter.a.q",
  "whirl.horn.filter.a.gain",

  "whirl.horn.filter.b.type",
  "whirl.horn.filter.b.hz",
  "whirl.horn.filter.b.q",
  "whirl.horn.filter.b.gain",

#ifdef HORN_COMB_FILTER // disabled in b_whirl/whirl.c
  "whirl.horn.comb.a.feedback",
  "whirl.horn.comb.a.delay",

  "whirl.horn.comb.b.feedback",
  "whirl.horn.comb.b.delay",
#endif

  "whirl.drum.filter.type",
  "whirl.drum.filter.hz",
  "whirl.drum.filter.q",
  "whirl.drum.filter.gain",

  "whirl.horn.breakpos",
  "whirl.drum.breakpos",

  "whirl.horn.acceleration",
  "whirl.horn.deceleration",
  "whirl.drum.acceleration",
  "whirl.drum.deceleration",

  "overdrive.enable",
  "overdrive.character",
  "overdrive.inputgain",
  "overdrive.outputgain",

  "xov.ctl_biased_fb2",
  "xov.ctl_biased_fb", // XXX should be unique prefix code
  "xov.ctl_biased_gfb",
  "xov.ctl_biased", // XXX should be unique prefix code
  "xov.ctl_sagtobias",

  "convolution.mix",

  NULL
};

enum parserState {
  stIgnore,     dtaIgnore,
  stNoteOff,    dtaNoteOff1,    dtaNoteOff2,
  stNoteOn,     dtaNoteOn1,     dtaNoteOn2,
  stControlChg, dtaControlChg1, dtaControlChg2,
  stProgramChg, dtaProgramChg
};

#define CTRL_USE_MAX 128

/* Arrays of pointers to functions that handle controller values. */

typedef struct {
  void (*fn)(void *, unsigned char);
  void *d;
} ctrl_function;

typedef uint8_t midiccflags_t;

enum { // 1,2,4,8,.. - adjust ctrlflg once >8 to uint16_t
  MFLAG_INV = 1,
};

/* ---------------------------------------------------------------- */

struct b_midicfg {

/* Used by the MIDI parser to record message bytes */

unsigned char rcvChA; /* MIDI receive channel */
unsigned char rcvChB; /* MIDI receive channel */
unsigned char rcvChC; /* MIDI receive channel */

/*
 * The all channel transpose is used to transpose the entire instrument
 * into a different mode and is a common function on synthesizers.
 * The channel- and split-dependent transposes are for adjusting the
 * reach of the user's MIDI controller(s) to the desired range.
 */

int transpose;	/* All channel transpose */

int nshA;	/* Channel A transpose (no split) */
int nshA_U;	/* Channel A upper region transpose */
int nshA_PL;	/* Channel A pedal region transpose */
int nshA_UL;	/* Channel A lower region transpose */
int nshB;	/* Channel B transpose */
int nshC;	/* Channel C transpose */

int splitA_PL;	/* A channel pedal region */
int splitA_UL;	/* A channel lower region */

/*
 * This flag controls how to map MIDI input that falls outside of
 * the virtual instrument's manuals. A value of zero means such MIDI notes
 * make no sound. A value of one means that the MIDI notes map back
 * into the nearest octave with a playable key, a mechanism similar to
 * the foldback used in some organ models.
 */
int userExcursionStrategy;

unsigned char keyTableA[128]; /**< MIDI note to key transl. tbl */
unsigned char keyTableB[128]; /**< MIDI note to key transl. tbl */
unsigned char keyTableC[128]; /**< MIDI note to key transl. tbl */

unsigned char * keyTable[16]; /**< Tables per MIDI channel */


unsigned char ctrlUseA[CTRL_USE_MAX];
unsigned char ctrlUseB[CTRL_USE_MAX];
unsigned char ctrlUseC[CTRL_USE_MAX];

ctrl_function ctrlvecA[128];
ctrl_function ctrlvecB[128];
ctrl_function ctrlvecC[128];

ctrl_function *ctrlvec[16]; /**< control function table per MIDI channel */

midiccflags_t ctrlflg[16][128]; /**< binary flags for each control  -- binary OR */
};

static void resetMidiCfg(void *mcfg) {
  struct b_midicfg * m = (struct b_midicfg *) mcfg;

  m->rcvChA = 0; /* MIDI receive channel */
  m->rcvChB = 1; /* MIDI receive channel */
  m->rcvChC = 2; /* MIDI receive channel */


  m->transpose = 0;	/* All channel transpose */

  m->nshA    = 0;	/* Channel A transpose (no split) */
  m->nshA_U  = 0;	/* Channel A upper region transpose */
  m->nshA_PL = 0;	/* Channel A pedal region transpose */
  m->nshA_UL = 0;	/* Channel A lower region transpose */
  m->nshB = 0;		/* Channel B transpose */
  m->nshC = 0;		/* Channel C transpose */

  m->splitA_PL = 0;	/* A channel pedal region */
  m->splitA_UL = 0;	/* A channel lower region */

  m->userExcursionStrategy = 0;
}

void *allocMidiCfg() {
  struct b_midicfg * mcfg = calloc(1, sizeof(struct b_midicfg));
  resetMidiCfg(mcfg);
  return mcfg;
}

void freeMidiCfg(void *mcfg) {
  struct b_midicfg * m = (struct b_midicfg *) mcfg;
  free(m);
}

/* ---------------------------------------------------------------- */

/*
 *
 */
int getCCFunctionId (const char * name) {
  int i;
  assert (name != NULL);
  for (i = 0; ccFuncNames[i] != NULL; i++) {
    if (0 == strncmp (name, ccFuncNames[i], strlen(ccFuncNames[i]))) {
      return i;
    }
  }
  return -1;
}

static void parseCCFlags(midiccflags_t *f, char *param) {
  int l = strlen(param);
  if (param[l-1] == '-') *f |= MFLAG_INV;
}

/*
 * This is the default (empty) controller function. It does nothing at all
 * and therefore we initialize the ctrlvec tables with pointers to it.
 */
static void emptyControlFunction (void *d, unsigned char uc) {}

/*
 * Assigns a control function to a MIDI controller.
 * This function is programmed to recognise multiple allocations that do
 * not refer to the no-op function. It MAY be an error if a controller
 * is allocated twice, so the code just prints a warning.
 *
 * @param vec        Array of pointers to control functions.
 * @param controller MIDI controller number
 * @param f          Pointer to control function to assign to controller
 */
static void assignMIDIControllerFunction (ctrl_function *vec,
					  unsigned char controller,
					  void (*f) (void *, unsigned char),
					  void *d) {
  assert (vec != NULL);
  if (f != NULL) {
    if ((vec[controller].fn != emptyControlFunction) &&
	(vec[controller].fn != NULL)) {
      fprintf (stderr,
	       "midi.c:WARNING, multiple allocation of controller %d!\n",
	       (int) controller);
    }
    vec[controller].fn = f;
    vec[controller].d = d;

  }
  else {
    vec[controller].fn = emptyControlFunction;
    vec[controller].d = NULL;
  }
}

/*
 * 26-sep-2004/FK This is the entry point for modules that wish to register
 * functions that accept MIDI controller data. Functions entered through this
 * interface can be freely remapped by the user via configuration files.
 *
 * @param cfname    The symbolic name (defined in ccFuncNames) of the reaction
 *                  implemented by the function pointed to by the f parameter.
 * @param f         Pointer to function that acts on the controller message.
 */
void useMIDIControlFunction (void *mcfg, char * cfname, void (* f) (void *, unsigned char), void *d) {
  struct b_midicfg * m = (struct b_midicfg *) mcfg;

  int x = getCCFunctionId (cfname);

  assert (-1 < x);

  if (m->ctrlUseA[x] < 128) {
    assignMIDIControllerFunction (m->ctrlvecA, m->ctrlUseA[x], f, d);
  }
  if (m->ctrlUseB[x] < 128) {
    assignMIDIControllerFunction (m->ctrlvecB, m->ctrlUseB[x], f, d);
  }
  if (m->ctrlUseC[x] < 128) {
    assignMIDIControllerFunction (m->ctrlvecC, m->ctrlUseC[x], f, d);
  }

}

/*
 * This initializes the MIDI controller vector tables.
 */
void initControllerTable (void *mcfg) {
  struct b_midicfg * m = (struct b_midicfg *) mcfg;
  int i;
  for (i = 0; i < 128; i++) {
    int chn;
    for (chn = 0; chn < 16; chn++) {
      m->ctrlflg[chn][i] = 0;
    }
    m->ctrlvecA[i].fn = emptyControlFunction;
    m->ctrlvecB[i].fn = emptyControlFunction;
    m->ctrlvecC[i].fn = emptyControlFunction;
    m->ctrlvecA[i].d = NULL;
    m->ctrlvecB[i].d = NULL;
    m->ctrlvecC[i].d = NULL;
  }

  for (i = 0; i < CTRL_USE_MAX; i++) {
    m->ctrlUseA[i] = 255;
    m->ctrlUseB[i] = 255;
    m->ctrlUseC[i] = 255;
  }
}

/*
 * This function is used to map a region of the MIDI keyboard to a
 * region of setBfree keys. The supplied translation table will only be
 * written in the specified MIDI range.
 *
 * @param translationTable  Pointer to lookup table used by parser.
 * @param firstMIDINote     MIDI note number of the first note in the input.
 * @param lastMIDINote      MIDI note number of the last note in the input.
 * @param firstKey          First key number in the target region.
 * @param lastKey           Last key number in the target region.
 * @param transpose         Applies a small transpose to the target region.
 * @param excursionStrategy If zero, MIDI notes mapped outside the target
 *                          become silent. If 1, notes outside the target
 *                          wraps around to the closest octave key.
 */
static void loadKeyTableRegion (unsigned char * translationTable,
				int first_MIDINote,
				int last_MIDINote,
				int firstKey,
				int lastKey,
				int transpose,
				int excursionStrategy)
{
  int note;
  int offset = transpose + firstKey - first_MIDINote;
  int firstKeyAdjust = firstKey + 12 - (firstKey % 12);
  int lastKeyAdjust  = lastKey - (lastKey % 12) - 12;

  for (note = first_MIDINote; note <= last_MIDINote; note++) {
    int key = note + offset;
    if (key < firstKey) {
      key = (excursionStrategy == 1) ? firstKeyAdjust + (key % 12) : 255;
    }
    else if (lastKey < key) {
      key = (excursionStrategy == 1) ? lastKeyAdjust + (key % 12) : 255;
    }
    /* This may happen if the key range is smaller than an octave. */
    if ((key < firstKey) || (lastKey < key)) {
      key = 255;
    }
    translationTable[note] = key;
  }
}

/*
 * Clears a note-to-key translation table by setting all entries to 255.
 */
static void clearKeyTable (unsigned char * table) {
  int i;
  for (i = 0; i < 128; i++) {
    table[i] = 255;
  }
}

/*
 * This function loads the channel A note-to-key translation table.
 */
static void loadKeyTableA (struct b_midicfg * m) {
  int left = 0;
  int first_MIDI_Note;

  clearKeyTable (m->keyTableA);

  if (0 < m->splitA_PL) {
    loadKeyTableRegion (m->keyTableA,
			24, m->splitA_PL - 1,
			128, 159,
			m->transpose + m->nshA_PL,
			0);
    left = m->splitA_PL;
  }

  if (left < m->splitA_UL) {
    first_MIDI_Note = (36 < left) ? left : 36;
    loadKeyTableRegion (m->keyTableA,
			first_MIDI_Note, m->splitA_UL - 1,
			64 + (first_MIDI_Note % 12), 124,
			m->transpose + m->nshA_UL,
			0);
    left = m->splitA_UL;
  }

  first_MIDI_Note = (36 < left) ? left : 36;

  /*
   * Here we allow the upper MIDI
   * parameter to extend up to 127 (maximum). That way a liberal use of
   * nshA_U (noteshift for upper manual in split mode) can exploit a
   * wide controller (e.g. 88 keys).
   */

  loadKeyTableRegion (m->keyTableA,
		      first_MIDI_Note, 127,
		      0 + (first_MIDI_Note - 36), 60,
		      m->transpose + ((0 < left) ? m->nshA_U : m->nshA),
		      0);

} /* loadKeyTableA */

/*
 * Loads the B channel (lower manual) MIDI to key mapping table.
 */
static void loadKeyTableB (struct b_midicfg * m) {

  clearKeyTable (m->keyTableB);

  loadKeyTableRegion (m->keyTableB,
		      36, 96,
		      64, 124,
		      m->transpose + m->nshB,
		      m->userExcursionStrategy);

}

/*
 * Loads the C channel (pedals) MIDI to key mapping table.
 */
static void loadKeyTableC (struct b_midicfg * m) {

  clearKeyTable (m->keyTableC);

  loadKeyTableRegion (m->keyTableC,
		      24, 55,
		      128, 159,
		      m->transpose + m->nshC,
		      m->userExcursionStrategy);

}

/*
 * External interface to set and unset the A keyboard split points.
 */
void setKeyboardSplitMulti (void *mcfg,
			    int flags,
			    int p_splitA_PL,
			    int p_splitA_UL,
			    int p_nshA_PL,
			    int p_nshA_UL,
			    int p_nshA_U)
{
  struct b_midicfg * m = (struct b_midicfg *) mcfg;
  if (flags &  1) m->splitA_PL = p_splitA_PL;
  if (flags &  2) m->splitA_UL = p_splitA_UL;
  if (flags &  4) m->nshA_PL   = p_nshA_PL;
  if (flags &  8) m->nshA_UL   = p_nshA_UL;
  if (flags & 16) m->nshA_U    = p_nshA_U;

  loadKeyTableA (m);
}

void setKeyboardTransposeA (void *mcfg, int transpose) {
  struct b_midicfg * m = (struct b_midicfg *) mcfg;
  m->nshA = transpose;
  loadKeyTableA (m);
}

void setKeyboardTransposeB (void *mcfg, int transpose) {
  struct b_midicfg * m = (struct b_midicfg *) mcfg;
  m->nshB = transpose;
  loadKeyTableB (m);
}

void setKeyboardTransposeC (void *mcfg, int transpose) {
  struct b_midicfg * m = (struct b_midicfg *) mcfg;
  m->nshC = transpose;
  loadKeyTableC (m);
}

void setKeyboardTranspose (void *mcfg, int trsp) {
  struct b_midicfg * m = (struct b_midicfg *) mcfg;
  m->transpose = trsp;
  loadKeyTableA (m);
  loadKeyTableB (m);
  loadKeyTableC (m);
}

/*
 * Loads the status table. The table is used by the MIDI parser to look
 * up status bytes. The table gives the parser's initial state and a pointer
 * to items used in the processing of the data in the message. For example,
 * note on/off messages uses a pointer to a note-to-key translation table.
 */
static void loadStatusTable (struct b_midicfg * m) {
  int i;
  for (i = 0; i < 16; i++) {
    m->keyTable[i] = NULL;
    m->ctrlvec[i]  = NULL;
  }
  m->keyTable[m->rcvChA] = m->keyTableA;
  m->keyTable[m->rcvChB] = m->keyTableB;
  m->keyTable[m->rcvChC] = m->keyTableC;

  m->ctrlvec[m->rcvChA] = (void *) m->ctrlvecA;
  m->ctrlvec[m->rcvChB] = (void *) m->ctrlvecB;
  m->ctrlvec[m->rcvChC] = (void *) m->ctrlvecC;
}

/*
 * Auxillary function to midiPrimeControllerMapping below.
 */
static void loadCCMap (char * cfname,
		       int ccn,
		       unsigned char * A,
		       unsigned char * B,
		       unsigned char * C) {
  int x = getCCFunctionId (cfname);
  if (!(-1 < x)) {
    fprintf (stderr, "Unrecognized controller function name:'%s'\n", cfname);
    assert (-1 < x);
  }
  if (A != NULL) A[x] = (unsigned char) ccn;
  if (B != NULL) B[x] = (unsigned char) ccn;
  if (C != NULL) C[x] = (unsigned char) ccn;
}

/*
 * Sets the initial state of the tables that map from a controllable function
 * id to a MIDI controller number. This function must be
 * run before the configuration sequence because it is part of the static
 * default. Maintaining a static default in source becomes much to odious
 * when things move around.
 * What we do is that we load the tables ctrlUseA, ctrlUseB and ctrlUseC
 */
void midiPrimeControllerMapping (void *mcfg) {
  struct b_midicfg * m = (struct b_midicfg *) mcfg;

  loadCCMap ("swellpedal1",  1, m->ctrlUseA, m->ctrlUseB, m->ctrlUseC);
  loadCCMap ("swellpedal2", 11, m->ctrlUseA, m->ctrlUseB, m->ctrlUseC);

  loadCCMap ("xov.ctl_biased",      3, m->ctrlUseA, NULL, NULL);
  loadCCMap ("xov.ctl_biased_fb",   9, m->ctrlUseA, NULL, NULL);
  loadCCMap ("xov.ctl_biased_fb2", 14, m->ctrlUseA, NULL, NULL);
  loadCCMap ("xov.ctl_biased_gfb", 15, m->ctrlUseA, NULL, NULL);
  loadCCMap ("xov.ctl_sagtobias",  20, m->ctrlUseA, NULL, NULL);

  loadCCMap ("overdrive.inputgain",      21, m->ctrlUseA, NULL, NULL);
  loadCCMap ("overdrive.outputgain",     22, m->ctrlUseA, NULL, NULL);

  loadCCMap ("whirl.drum.filter.type", 23, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.drum.filter.hz",   24, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.drum.filter.q",    25, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.drum.filter.gain", 26, m->ctrlUseA, NULL, NULL);

  loadCCMap ("whirl.horn.filter.a.type", 27, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.horn.filter.a.hz",   28, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.horn.filter.a.q",    29, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.horn.filter.a.gain", 30, m->ctrlUseA, NULL, NULL);

  /* 32-63 are least significant bits of controller 0-31 */

  loadCCMap ("rotary.speed-toggle", 64, m->ctrlUseA, m->ctrlUseB, m->ctrlUseC);

  loadCCMap ("upper.drawbar16",  70, m->ctrlUseA, NULL, NULL);
  loadCCMap ("upper.drawbar513", 71, m->ctrlUseA, NULL, NULL);
  loadCCMap ("upper.drawbar8",   72, m->ctrlUseA, NULL, NULL);
  loadCCMap ("upper.drawbar4",   73, m->ctrlUseA, NULL, NULL);
  loadCCMap ("upper.drawbar223", 74, m->ctrlUseA, NULL, NULL);
  loadCCMap ("upper.drawbar2",   75, m->ctrlUseA, NULL, NULL);
  loadCCMap ("upper.drawbar135", 76, m->ctrlUseA, NULL, NULL);
  loadCCMap ("upper.drawbar113", 77, m->ctrlUseA, NULL, NULL);
  loadCCMap ("upper.drawbar1",   78, m->ctrlUseA, NULL, NULL);

  loadCCMap ("lower.drawbar16",  70, NULL, m->ctrlUseB, NULL);
  loadCCMap ("lower.drawbar513", 71, NULL, m->ctrlUseB, NULL);
  loadCCMap ("lower.drawbar8",   72, NULL, m->ctrlUseB, NULL);
  loadCCMap ("lower.drawbar4",   73, NULL, m->ctrlUseB, NULL);
  loadCCMap ("lower.drawbar223", 74, NULL, m->ctrlUseB, NULL);
  loadCCMap ("lower.drawbar2",   75, NULL, m->ctrlUseB, NULL);
  loadCCMap ("lower.drawbar135", 76, NULL, m->ctrlUseB, NULL);
  loadCCMap ("lower.drawbar113", 77, NULL, m->ctrlUseB, NULL);
  loadCCMap ("lower.drawbar1",   78, NULL, m->ctrlUseB, NULL);

  loadCCMap ("pedal.drawbar16",  70, NULL, NULL, m->ctrlUseC);
  loadCCMap ("pedal.drawbar513", 71, NULL, NULL, m->ctrlUseC);
  loadCCMap ("pedal.drawbar8",   72, NULL, NULL, m->ctrlUseC);
  loadCCMap ("pedal.drawbar4",   73, NULL, NULL, m->ctrlUseC);
  loadCCMap ("pedal.drawbar223", 74, NULL, NULL, m->ctrlUseC);
  loadCCMap ("pedal.drawbar2",   75, NULL, NULL, m->ctrlUseC);
  loadCCMap ("pedal.drawbar135", 76, NULL, NULL, m->ctrlUseC);
  loadCCMap ("pedal.drawbar113", 77, NULL, NULL, m->ctrlUseC);
  loadCCMap ("pedal.drawbar1",   78, NULL, NULL, m->ctrlUseC);

  loadCCMap ("percussion.enable",   80, m->ctrlUseA, NULL, NULL);
  loadCCMap ("percussion.decay",    81, m->ctrlUseA, NULL, NULL);
  loadCCMap ("percussion.harmonic", 82, m->ctrlUseA, NULL, NULL);

  loadCCMap ("vibrato.knob",    83, m->ctrlUseA, NULL, NULL);
  loadCCMap ("vibrato.routing", 92, m->ctrlUseA, NULL, NULL);

  loadCCMap ("whirl.horn.filter.b.type", 85, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.horn.filter.b.hz",   86, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.horn.filter.b.q",    87, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.horn.filter.b.gain", 88, m->ctrlUseA, NULL, NULL);

#ifdef HORN_COMB_FILTER // disabled in b_whirl/whirl.c
  loadCCMap ("whirl.horn.comb.a.feedback", 89, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.horn.comb.a.delay",    90, m->ctrlUseA, NULL, NULL);

  loadCCMap ("whirl.horn.comb.b.feedback", 102, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.horn.comb.b.delay",    103, m->ctrlUseA, NULL, NULL);
#endif

  loadCCMap ("rotary.speed-preset",   91, m->ctrlUseA, NULL, NULL);

  loadCCMap ("overdrive.character",   93, m->ctrlUseA, NULL, NULL);

  loadCCMap ("convolution.mix", 94, m->ctrlUseA, NULL, NULL);

#if 0 // leslie testing
  loadCCMap ("whirl.horn.breakpos", 34, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.drum.breakpos", 35, m->ctrlUseA, NULL, NULL);

  loadCCMap ("whirl.horn.acceleration", 36, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.horn.deceleration", 37, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.drum.acceleration", 38, m->ctrlUseA, NULL, NULL);
  loadCCMap ("whirl.drum.deceleration", 39, m->ctrlUseA, NULL, NULL);
#endif
}

/*
 * Sets global transpose (for playing in alternate scales).
 */
void setMIDINoteShift (void *mcfg, char offset) {
  struct b_midicfg * m = (struct b_midicfg *) mcfg;
  m->transpose = offset;
  loadKeyTableA (m);
  loadKeyTableB (m);
  loadKeyTableC (m);
}

/*
 * This call configures this module.
 */
int midiConfig (void *mcfg, ConfigContext * cfg) {
  int v;
  int ack = 0;
  struct b_midicfg * m = (struct b_midicfg *) mcfg;
  if ((ack = getConfigParameter_ir ("midi.upper.channel",
				    cfg,
				    &v,
				    1, 16)) == 1) {
    m->rcvChA = v - 1;
  }
  else if ((ack = getConfigParameter_ir ("midi.lower.channel",
					 cfg,
					 &v,
					 1, 16)) == 1) {
    m->rcvChB = v - 1;
  }
  else if ((ack = getConfigParameter_ir ("midi.pedals.channel",
					 cfg,
					 &v,
					 1, 16)) == 1) {
    m->rcvChC = v - 1;
  }
  else if ((ack = getConfigParameter_ir ("midi.transpose",
					 cfg,
					 &v,
					 -127, 127)) == 1) {
    m->transpose = v;
  }
  else if ((ack = getConfigParameter_ir ("midi.upper.transpose",
					 cfg,
					 &v,
					 -127, 127)) == 1) {
    m->nshA = v;
  }
  else if ((ack = getConfigParameter_ir ("midi.lower.transpose",
					 cfg,
					 &v,
					 -127, 127)) == 1) {
    m->nshB = v;
  }
  else if ((ack = getConfigParameter_ir ("midi.pedals.transpose",
					 cfg,
					 &v,
					 -127, 127)) == 1) {
    m->nshC = v;
  }
  else if ((ack = getConfigParameter_ir ("midi.pedals.transpose.split",
					 cfg,
					 &v,
					 -127, 127)) == 1) {
    m->nshA_PL = v;
  }
  else if ((ack = getConfigParameter_ir ("midi.lower.transpose.split",
					 cfg,
					 &v,
					 -127, 127)) == 1) {
    m->nshA_UL = v;
  }
  else if ((ack = getConfigParameter_ir ("midi.upper.transpose.split",
					 cfg,
					 &v,
					 -127, 127)) == 1) {
    m->nshA_U = v;
  }
  /*
   * The syntax for this config option is:
   * midi.controller.{upper,lower,pedals}.<cc>=<fname>
   * where <cc> is a MIDI controller number, and
   * <fname> is the symbolic name of a controllable function.
   */
  else if (strncasecmp (cfg->name, "midi.controller.", 16) == 0) {
    unsigned char * ctrlUse = m->ctrlUseA;
    midiccflags_t * flagUse = m->ctrlflg[m->rcvChA];

    int ccIdx = 0;
    if (strncasecmp ((cfg->name) + 16, "upper", 5) == 0) {
      ctrlUse = m->ctrlUseA;
      flagUse = m->ctrlflg[m->rcvChA];
      ccIdx = 22;
    }
    else if (strncasecmp ((cfg->name) + 16, "lower", 5) == 0) {
      ctrlUse = m->ctrlUseB;
      flagUse = m->ctrlflg[m->rcvChB];
      ccIdx = 22;
    }
    else if (strncasecmp ((cfg->name) + 16, "pedals", 6) == 0) {
      ctrlUse = m->ctrlUseC;
      flagUse = m->ctrlflg[m->rcvChC];
      ccIdx = 23;
    }
    else {
      showConfigfileContext (cfg, "directive 'upper', 'lower' or 'pedals' expected");
    }

    /* If the code above managed to parse a channel name... */
    if (0 < ccIdx) {
      int ccn;
      /* ... and we manage to parse a controller number... */
      if (sscanf ((cfg->name) + ccIdx, "%d", &ccn) == 1) {
	/* ...and the controller number is in the allowed range... */
	if ((0 <= ccn) && (ccn < 128)) {
	  int i = getCCFunctionId (cfg->value);
	  if (-1 < i) {
	    /* Store the controller number indexed by abstract function id */
	    ctrlUse[i] = ccn;
	    parseCCFlags(&(flagUse[ccn]), cfg->value);
	    ack++;
	  }
	  else {
	    /* No match found for symbolic function name. */
	    showConfigfileContext (cfg,
				   "name of controllable function not found");
	  }
	}
	else {
	  showConfigfileContext (cfg, "controller number out of range");
	}
      }
    }
  }

  return ack;
}

static const ConfigDoc doc[] = {
  {"midi.upper.channel", CFG_INT, "1", "The MIDI channel to use for the upper-manual. range: [1..16]"},
  {"midi.lower.channel", CFG_INT, "2", "The MIDI channel to use for the lower manual. range: [1..16]"},
  {"midi.pedals.channel", CFG_INT,"3", "The MIDI channel to use for the pedals. range: [1..16]"},
  {"midi.controller.upper.<cc>", CFG_TEXT, "\"-\"", "Speficy a function-name to bind to the given MIDI control-command. <cc> is an integer 0..127. Defaults are in midiPrimeControllerMapping() and can be listed using the '-d' commandline option. See general information."},
  {"midi.controller.lower.<cc>", CFG_TEXT, "\"-\"", "see midi.controller.upper"},
  {"midi.controller.pedals.<cc>", CFG_TEXT, "\"-\"", "see midi.controller.upper"},
  {"midi.transpose", CFG_INT, "0", "global transpose (noteshift) in semitones."},
  {"midi.upper.transpose", CFG_INT, "0", "shift/transpose MIDI-notes on upper-manual in semitones"},
  {"midi.lower.transpose", CFG_INT, "0", "shift/transpose MIDI-notes on lower-manual in semitones"},
  {"midi.pedals.transpose", CFG_INT, "0", "shift/transpose MIDI-notes on pedals in semitones"},
  {"midi.upper.transpose.split", CFG_INT, "0", "noteshift for upper manual in split mode"},
  {"midi.lower.transpose.split", CFG_INT, "0", "noteshift for lower manual in split mode"},
  {"midi.pedals.transpose.split", CFG_INT, "0", "noteshift for lower manual in split mode"},
  {NULL}
};

const ConfigDoc *midiDoc () {
  return doc;
}

void initMidiTables(void *mcfg) {
  struct b_midicfg * m = (struct b_midicfg *) mcfg;
  loadKeyTableA (m);
  loadKeyTableB (m);
  loadKeyTableC (m);
  loadStatusTable (m);
}


void process_midi_event(void *instp, const struct bmidi_event_t *ev) {
  struct b_instance * inst = (struct b_instance *) instp;
  struct b_midicfg * m = (struct b_midicfg *) inst->midicfg;
  switch(ev->type) {
    case NOTE_ON:
      if(m->keyTable[ev->channel] && m->keyTable[ev->channel][ev->tone.note] != 255) {
	if (ev->tone.velocity > 0){
	  oscKeyOn (inst->synth, m->keyTable[ev->channel][ev->tone.note]);
	} else {
	  oscKeyOff (inst->synth, m->keyTable[ev->channel][ev->tone.note]);
	}
      }
      break;
    case NOTE_OFF:
      if(m->keyTable[ev->channel] && m->keyTable[ev->channel][ev->tone.note] != 255)
	oscKeyOff (inst->synth, m->keyTable[ev->channel][ev->tone.note]);
      break;
    case PROGRAM_CHANGE:
      installProgram(inst, ev->control.value);
      break;
    case CONTROL_CHANGE:
#ifdef DEBUG_MIDI_CC
      {
	unsigned char * ctrlUse = NULL;
	const char *fn="";
	if      (ev->channel == m->rcvChA) ctrlUse = m->ctrlUseA;
	else if (ev->channel == m->rcvChB) ctrlUse = m->ctrlUseB;
	else if (ev->channel == m->rcvChC) ctrlUse = m->ctrlUseC;
	if (ctrlUse) {
	  int j;
	  for (j=0; j < CTRL_USE_MAX; ++j) {
	    if (ctrlUse[j] == ev->control.param) {
	      fn = ccFuncNames[j];
	      break;
	    }
	  }
	}
	printf("CC: %2d %03d -> %3d (%s) %s\n", ev->channel, ev->control.param, ev->control.value, fn,
	    (m->ctrlvec[ev->channel] && m->ctrlvec[ev->channel][ev->control.param].fn != emptyControlFunction) ? "*":"-"
	  );
      }
#endif
      /* see http://www.midi.org/techspecs/midimessages.php#3
       * for reserved and dedicated CCs
       */

      /*  0x00 and 0x20 are used for BANK select */
      if (ev->control.param == 0x00 || ev->control.param == 0x20) {
	break;
      } else

      if (ev->control.param == 121) {
	/* TODO - reset all controller */
	break;
      } else

      if (ev->control.param == 120 || ev->control.param == 123) {
	/* Midi panic: 120: all sound off, 123: all notes off*/
	int i;
	for (i=0; i < MAX_KEYS; ++i) {
	  oscKeyOff (inst->synth, i);
	}
	break;
      } else

      if (ev->control.param >= 120) {
	/* params 122-127 are reserved - skip them. */
	break;
      } else

      if (m->ctrlvec[ev->channel] && m->ctrlvec[ev->channel][ev->control.param].fn) {
	uint8_t val = ev->control.value & 0x7f;
	if (m->ctrlflg[ev->channel][ev->control.param] & MFLAG_INV) {
	  val = 127 - val;
	}
	(m->ctrlvec[ev->channel][ev->control.param].fn)(m->ctrlvec[ev->channel][ev->control.param].d, val);
      }
      break;
    default:
      break;
  }
}

/** convert jack_midi_event (raw MIDI data) into
 *  internal MIDI message format  and process the event
 */
void parse_raw_midi_data(void *inst, uint8_t *buffer, size_t size) {
  struct bmidi_event_t bev;
  memset(&bev, 0, sizeof(struct bmidi_event_t));

  if (size < 2 || size > 3) return;
  // All messages need to be 3 bytes; except program-changes: 2bytes.
  if (size == 2 && (buffer[0] & 0xf0)  != 0xC0) return;

  bev.channel=buffer[0]&0x0f;

  switch (buffer[0] & 0xf0) {
    case 0x80:
      bev.type=NOTE_OFF;
      bev.tone.note=buffer[1]&0x7f;
      bev.tone.velocity=buffer[2]&0x7f;
      break;
    case 0x90:
      bev.type=NOTE_ON;
      bev.tone.note=buffer[1]&0x7f;
      bev.tone.velocity=buffer[2]&0x7f;
      break;
    case 0xB0:
      bev.type=CONTROL_CHANGE;
      bev.control.param=buffer[1]&0x7f;
      bev.control.value=buffer[2]&0x7f;
      break;
    case 0xC0:
      bev.type=PROGRAM_CHANGE;
      bev.control.value=buffer[1]&0x7f;
      break;
    default:
      return;
  }
  process_midi_event(inst, &bev);
}

static void dumpCCAssigment(FILE * fp, unsigned char *ctrl, midiccflags_t *flags) {
  int i;
  fprintf(fp,"  Controller | Function \n");
  for (i=0;i<127;++i) {
    if (ctrl[i] != 255) {
      fprintf(fp,"     %03d     | %s %s\n", ctrl[i] ,ccFuncNames[i], (flags[i]&1)?"-":"");
    }
  }
}

void listCCAssignments(void *mcfg, FILE * fp) {
  struct b_midicfg * m = (struct b_midicfg *) mcfg;
  fprintf(fp,"MIDI CC Assigments:\n");
  fprintf(fp,"--- Upper Manual   - Channel %2d ---\n", m->rcvChA);
  dumpCCAssigment(fp, m->ctrlUseA, m->ctrlflg[m->rcvChA]);
  fprintf(fp,"--- Lower Manual   - Channel %2d ---\n", m->rcvChB);
  dumpCCAssigment(fp, m->ctrlUseB, m->ctrlflg[m->rcvChB]);
  fprintf(fp,"--- Pedal          - Channel %2d ---\n", m->rcvChC);
  dumpCCAssigment(fp, m->ctrlUseC, m->ctrlflg[m->rcvChC]);
}

/* vi:set ts=8 sts=2 sw=2: */
