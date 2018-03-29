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

#ifndef CONFIGDOCONLY

#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <assert.h>

#include "main.h"
#include "global_inst.h"

/* These are assertion support macros. */
/* In range? : A <= V < B  */
#define inRng(A,V,B) (((A) <= (V)) && ((V) < (B)))
/* Is a B a valid bus number? */
#define isBus(B) ((0 <= (B)) && ((B) < 9))
/* Is O a valid oscillator number? */
#define isOsc(O) ((0 <= (O)) && ((O) < 128))

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
 * Buses are numbered like this:
 *  0-- 8, upper manual, ( 0=16',  8=1')
 *  9--17, lower manual, ( 9=16', 17=1')
 * 18--26, pedal         (18=32')
 */
#ifndef NOF_BUSES
#define NOF_BUSES 27		/* Should be in tonegen.h */
#endif

#define UPPER_BUS_LO 0
#define UPPER_BUS_END 9
#define LOWER_BUS_LO 9
#define LOWER_BUS_END 18
#define PEDAL_BUS_LO 18
#define PEDAL_BUS_END 27

/* ****************************************************************/
/* clang-format off */

/* The message layout is:
 *
 * 15 14 13 12  11 10  9  8  7  6  5  4  3  2  1  0
 * [ Message  ] [             Parameter           ]
 * [0  0  0  0] [            Key number           ]    Key off
 * [0  0  0  1] [            Key number           ]    Key on
 */

/* Message field access macros */
#define MSG_MMASK 0xf000
#define MSG_PMASK 0x0fff
/* Retrive message part from a message */
#define MSG_GET_MSG(M) ((M)&MSG_MMASK)
/* Retrieve parameter part from a message */
#define MSG_GET_PRM(M) ((M)&MSG_PMASK)
/* Messages */
#define MSG_MKEYOFF 0x0000
#define MSG_MKEYON  0x1000
/* Key released message, arg is keynumber */
#define MSG_KEY_OFF(K) (MSG_MKEYOFF | ((K)&MSG_PMASK))
/* Key depressed message, arg is keynumber */
#define MSG_KEY_ON(K) (MSG_MKEYON | ((K)&MSG_PMASK))

/* Core instruction codes (opr field in struct _coreins). */
#define CR_CPY    0 /* Copy instruction */
#define CR_ADD    1 /* Add instruction */
#define CR_CPYENV 2 /* Copy via envelope instruction */
#define CR_ADDENV 3 /* Add via envelope instruction */

/* Rendering flag bits */
#define ORF_MODIFIED 0x0004
#define ORF_ADDED    0x0002
#define ORF_REMOVED  0x0001
/* Composite flag bits */
#define OR_ADD       0x0006
#define OR_REM       0x0005

#define RT_PERC2ND 0x08
#define RT_PERC3RD 0x04
#define RT_PERC    0x0C
#define RT_UPPRVIB 0x02
#define RT_LOWRVIB 0x01
#define RT_VIB     0x03

/* Equalisation macro selection. */
#define EQ_SPLINE 0
#define EQ_PEAK24 1 /* Legacy */
#define EQ_PEAK46 2 /* Legacy */

/* These units are in dB */
#define taperMinusThree -10.0
#define taperMinusTwo    -7.0
#define taperMinusOne    -3.5
#define taperReference    0.0
#define taperPlusOne      3.5
#define taperPlusTwo      7.0

/* ****************************************************************/

/**
 * Gear ratios for a 60 Hertz organ.
 */
static double const gears60ratios[12][2] = {
	{85, 104}, /* c  */
	{71,  82}, /* c# */
	{67,  73}, /* d  */
	{35,  36}, /* d# */
	{69,  67}, /* e  */
	{12,  11}, /* f  */
	{37,  32}, /* f# */
	{49,  40}, /* g  */
	{48,  37}, /* g# */
	{11,   8}, /* a  */
	{67,  46}, /* a# */
	{54,  35}  /* h  */
};

/**
 * Gear ratios for a 50 Hertz organ (estimated).
 */
static double const gears50ratios[12][2] = {
	{17, 26}, /* c  */
	{57, 82}, /* c# */
	{11, 15}, /* d  */
	{49, 63}, /* d# */
	{33, 40}, /* e  */
	{55, 63}, /* f  */
	{49, 53}, /* f# */
	{49, 50}, /* g  */
	{55, 53}, /* g# */
	{11, 10}, /* a  */
	{ 7,  6}, /* a# */
	{90, 73}  /* h  */
};

/**
 * This table is indexed by frequency number, i.e. the tone generator number
 * on the 91 oscillator generator. The first frequency/generator is numbered 1.
 */
static short const wheelPairs[92] = {
	0,                                                /* 0: not used */
	49, 50, 51, 52,  53, 54, 55, 56,  57, 58, 59, 60, /* 1-12 */
	61, 62, 63, 64,  65, 66, 67, 68,  69, 70, 71, 72, /* 13-24 */
	73, 74, 75, 76,  77, 78, 79, 80,  81, 82, 83, 84, /* 25-36 */
	0,  0,  0,  0,   0,  85, 86, 87,  88, 89, 90, 91, /* 37-48 */
	1,  2,  3,  4,   5,  6,  7,  8,   9,  10, 11, 12, /* 49-60 */
	13, 14, 15, 16,  17, 18, 19, 20,  21, 22, 23, 24, /* 61-72 */
	25, 26, 27, 28,  29, 30, 31, 32,  33, 34, 35, 36, /* 73-84 */
	42, 43, 44, 45,  46, 47, 48                       /* 85-91 */
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
static short const northTransformers[] = {
	85, 66, 90, 71, 47, 64, 86, 69, 45, 62, 86, 67, 91, 72, 48, 65, 89, 70,
	46, 63, 87, 68, 44, 61,
	0
};

/**
 * description of rows of transformers mounted on top of
 * the tonegenerator for the lower (south) manual
 */
static short const southTransformers[] = {
	78, 54, 83, 59, 76, 52, 81, 57, 74, 50, 79, 55, 84, 60, 77, 53, 82, 58,
	75, 51, 80, 56, 73, 49,
	0
};

/**
 * This array describes how oscillators are arranged on the terminal
 * soldering strip.
 */
static short const terminalStrip[] = {
	85, 42, 30, 76, 66, 18,  6, 54, 90, 35, 83, 71, 23, 11, 59, 47, 40,
	28, 76, 64, 16,  4, 52, 88, 33, 81, 69, 21,  9, 57, 45, 34, 26, 74,
	62, 14,  2, 50, 86, 43, 31, 79, 67, 19,  7, 55, 91, 36, 84, 72, 24,
	12, 60, 48, 41, 29, 77, 65, 17,  5, 53, 89, 34, 82, 70, 22, 10, 58,
	46, 39, 27, 75, 63, 15,  3, 51, 87, 32, 80, 68, 20,  8, 56, 44, 37,
	25, 73, 61, 13,  1, 49,
	0
};

/* clang-format on */
/* ****************************************************************/


static void initValues (struct b_tonegen *t) {
  t->leConfig = NULL;
  t->leRuntime = NULL;
  t->activeOscLEnd = 0;

  t->msgQueueWriter = t->msgQueue;
  t->msgQueueReader = t->msgQueue;
  t->msgQueueEnd = &(t->msgQueue[MSGQSZ]);
  t->envAttackModel  = ENV_CLICK;
  t->envReleaseModel = ENV_LINEAR;

  t->envAttackClickLevel = 0.50;
  t->envReleaseClickLevel = 0.25;

  /* these are set later in initToneGenerator() */
  t->envAtkClkMinLength = -1; /*  8 @ 22050 */
  t->envAtkClkMaxLength = -1; /* 40 @ 22050 */

  t->newRouting = 0;
  t->oldRouting = 0;

  t->percSendBus = 4; /* 3 or 4 */
  t->percSendBusA = 3;
  t->percSendBusB = 4;

  t->upperKeyCount = 0;

#ifdef KEYCOMPRESSION
  t->keyDownCount = 0;
#endif

  t->swellPedalGain = 0.07; /* initial level */
  t->outputLevelTrim = 0.07; /* 127/127 * midi-signal */
  t->tuning = 440.0;

  t->gearTuning = 1;

  t->drawBarChange = 0;
  t->percEnabled = FALSE;

  t->percTriggerBus = 8;
  t->percTrigRestore = 0;

  t->percFastDecaySeconds = 1.0;
  t->percSlowDecaySeconds = 4.0;

#ifdef HIPASS_PERCUSSION
  t->percEnvScaling = 11.0;
#else
  t->percEnvScaling =  3.0;
#endif /* HIPASS_PERCUSSION */


  t->percEnvGainResetNorm     = 1.0;
  t->percEnvGainResetSoft     = 0.5012;
  t->percEnvGainDecayFastNorm = 0.9995; /* Runtime select */
  t->percEnvGainDecayFastSoft = 0.9995; /* Runtime select */
  t->percEnvGainDecaySlowNorm = 0.9999; /* Runtime select */
  t->percEnvGainDecaySlowSoft = 0.9999; /* Runtime select */
  t->percDrawbarNormalGain = 0.60512;
  t->percDrawbarSoftGain = 1.0;
  t->percDrawbarGain = 1.0;

  t->tgVariant = TG_91FB12;
  t->tgPrecision = 0.001;
  t->eqMacro = EQ_SPLINE;
  t->eqvCeiling = 1.0;	/**< Normalizing manual osc eq. */

  t->eqP1y =  1.0;	/* Default is flat */
  t->eqR1y =  0.0;
  t->eqP4y =  1.0;
  t->eqR4y =  0.0;

  t->defaultCompartmentCrosstalk = 0.01; /* -40 dB */
  t->defaultTransformerCrosstalk = 0.0;
  t->defaultTerminalStripCrosstalk = 0.01; /* -40 db */
  t->defaultWiringCrosstalk = 0.01; /* -40 dB */
  t->contributionFloorLevel = 0.0000158;
  t->contributionMinLevel = 0.0;
  int i;
  for (i=0; i< MAX_PARTIALS; i++)
    t->wheel_Harmonics[i]  = 0;
  t->wheel_Harmonics[0]  = 1.0;
  //t->wheel_Harmonics  = { 1.0 }; /** < amplitudes of tonewheel harmonics */

  t->outputGain = 1.0;

#ifdef HIPASS_PERCUSSION
  t->pz = 0;
#endif
#ifdef KEYCOMPRESSION
  t->keyCompLevel = KEYCOMP_ZERO_LEVEL;
#endif
}

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
static ListElement * newConfigListElement (struct b_tonegen *t) {
  return newListElement (&t->leConfig);
}

/**
 * Allocates and returns a new runtime list element.
 */
static ListElement * newRuntimeListElement (struct b_tonegen *t) {
  return newListElement (&t->leRuntime);
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
void setToneGeneratorModel (struct b_tonegen *t, int variant) {
  switch (variant) {
  case TG_91FB00:
  case TG_82FB09:
  case TG_91FB12:
    t->tgVariant = variant;
    break;
  }
}

/**
 * This routine sets the tonegenerator's wave precision. The call must
 * be made before calling initToneGenerator() to have effect. It is the
 * target of startup configuration values.
 */
void setWavePrecision (struct b_tonegen *t, double precision) {
  if (0.0 < precision) {
    t->tgPrecision = precision;
  }
}

/**
 * Sets the tuning.
 */
void setTuning (struct b_tonegen *t, double refA_Hz) {
  if ((220.0 <= refA_Hz) && (refA_Hz <= 880.0)) {
    t->tuning = refA_Hz;
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
static void applyManualDefaults (struct b_tonegen *t, int keyOffset, int busOffset) {
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

  switch (t->tgVariant) {
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
    if (t->keyTaper[keyNumber] == NULL) { /* If taper is unset */
      int b;
      for (b = 0; b < 9; b++) {	/* For each bus contact */
	int terminalNumber;
	terminalNumber = (k + 13) + ULoffset[b]; /* Ideal terminal */
	/* Apply foldback rules */
	while (terminalNumber  < leastTerminal)   { terminalNumber += 12;}
	while (terminalNumber  < ULlowerFoldback) { terminalNumber += 12;}
	while (ULupperFoldback < terminalNumber)  { terminalNumber -= 12;}

	lep = newConfigListElement (t);
	LE_TERMINAL_OF(lep) = (short) terminalNumber;
	LE_BUSNUMBER_OF(lep) = (short) (b + busOffset);
	LE_LEVEL_OF(lep) = (float) taperingModel (k, b);

	appendListElement (&(t->keyTaper[keyNumber]), lep);
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
static void applyPedalDefaults (struct b_tonegen *t, int nofPedals) {
  int k;
  int PDoffset[9] = {-12, 7, 0, 12, 19, 24, 28, 31, 36};
  ListElement * lep;

  assert (nofPedals <= 128);

  for (k = 0; k < nofPedals; k++) {
    int keyNumber = k + 128;
    if (t->keyTaper[keyNumber] == NULL) {
      int b;
      for (b = 0; b < 9; b++) {
	int terminalNumber;
	terminalNumber = (k + 13) + PDoffset[b];
	if (terminalNumber < 1) continue;
	if (91 < terminalNumber) continue;
	lep = newConfigListElement (t);
	LE_TERMINAL_OF(lep)  = (short) terminalNumber;
	LE_BUSNUMBER_OF(lep) = (short) (b + PEDAL_BUS_LO);
	LE_LEVEL_OF(lep)     = (float) dBToGain (taperReference);
	appendListElement (&(t->keyTaper[keyNumber]), lep);
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
static void applyDefaultCrosstalk (struct b_tonegen *t, int keyOffset, int busOffset) {
  int k;
  int b;

  for (k = 0; k <= 60; k++) {
    int keyNumber = k + keyOffset;
    if (t->keyCrosstalk[keyNumber] == NULL) {
      for (b = 0; b < 9; b++) {
	int busNumber = busOffset + b;
	ListElement * lep;
	for (lep = t->keyTaper[keyNumber]; lep != NULL; lep = lep->next) {
	  if (LE_BUSNUMBER_OF(lep) == busNumber) {
	    continue;
	  }
	  ListElement * nlep = newConfigListElement (t);
	  LE_TERMINAL_OF(nlep) = LE_TERMINAL_OF(lep);
	  LE_BUSNUMBER_OF(nlep) = busNumber;
	  LE_LEVEL_OF(nlep) =
	    (t->defaultWiringCrosstalk * LE_LEVEL_OF(lep))
	    /
	    abs(busNumber - LE_BUSNUMBER_OF(lep));
	  appendListElement (&(t->keyCrosstalk[keyNumber]), nlep);
	}
      }
    }
  }
}

/**
 * Find east-west neighbours.
 */
static int findEastWestNeighbours (short const * const v, int w, int * ep, int * wp) {
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
static void applyDefaultConfiguration (struct b_tonegen *t) {
  int i;
  ListElement * lep;

  /* Crosstalk at the terminals. Terminal mix. */

  for (i = 1; i <= NOF_WHEELS; i++) {
    if (t->terminalMix[i] == NULL) {
      lep = newConfigListElement (t);
      LE_WHEEL_NUMBER_OF(lep) = (short) i;
      LE_WHEEL_LEVEL_OF(lep) = 1.0 - t->defaultCompartmentCrosstalk;
      appendListElement (&(t->terminalMix[i]), lep);
      if (0.0 < t->defaultCompartmentCrosstalk) {
	if (0 < wheelPairs[i]) {
	  lep = newConfigListElement (t);
	  LE_WHEEL_NUMBER_OF(lep) = wheelPairs[i];
	  LE_WHEEL_LEVEL_OF(lep) = t->defaultCompartmentCrosstalk;
	  appendListElement (&(t->terminalMix[i]), lep);
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

  if (0.0 < t->defaultTransformerCrosstalk) {

    for (i = 44; i <= NOF_WHEELS; i++) {
      int east = 0;
      int west = 0;

      findTransformerNeighbours (i, &east, &west);

      if (0 < east) {
	lep = newConfigListElement (t);
	LE_WHEEL_NUMBER_OF(lep) = (short) east;
	LE_WHEEL_LEVEL_OF(lep) = t->defaultTransformerCrosstalk;
	appendListElement (&(t->terminalMix[i]), lep);
      }

      if (0 < west) {
	lep = newConfigListElement (t);
	LE_WHEEL_NUMBER_OF(lep) = (short) west;
	LE_WHEEL_LEVEL_OF(lep) = t->defaultTransformerCrosstalk;
	appendListElement (&(t->terminalMix[i]), lep);
      }
    }

  } /* if defaultTransformerCrosstalk */

  if (0.0 < t->defaultTerminalStripCrosstalk) {
    for (i = 1; i <= NOF_WHEELS; i++) {
      int east = 0;
      int west = 0;
      findEastWestNeighbours (terminalStrip, i, &east, &west);

      if (0 < east) {
	lep = newConfigListElement (t);
	LE_WHEEL_NUMBER_OF(lep) = (short) east;
	LE_WHEEL_LEVEL_OF(lep) = t->defaultTerminalStripCrosstalk;
	appendListElement (&(t->terminalMix[i]), lep);
      }

      if (0 < west) {
	lep = newConfigListElement (t);
	LE_WHEEL_NUMBER_OF(lep) = (short) west;
	LE_WHEEL_LEVEL_OF(lep) = t->defaultTerminalStripCrosstalk;
	appendListElement (&(t->terminalMix[i]), lep);
      }

    }
  } /* if defaultTerminalStripCrosstalk */

  /* Key connections and taper */

  applyManualDefaults (t,  0, 0);
  applyManualDefaults (t, 64, 9);
  applyPedalDefaults (t, 32);

  /* Key crosstalk */
  applyDefaultCrosstalk (t,  0, 0);
  applyDefaultCrosstalk (t, 64, 9);

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
static void cpmInsert ( struct b_tonegen *t /* XXX terminalMix */,
		       const ListElement * lep,
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

  for (tlep = t->terminalMix[terminal]; tlep != NULL; tlep = tlep->next) {
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
static void compilePlayMatrix (struct b_tonegen *t) {
  unsigned char cpmBus [NOF_WHEELS + 1][NOF_BUSES];
  float cpmGain [NOF_WHEELS][NOF_BUSES];

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
    for (lep = t->keyTaper[k]; lep != NULL; lep = lep->next) {
      cpmInsert (t, lep, cpmBus, cpmGain, wheelNumber, rowLength, &endRow);
    }
#if 1				/* Disabled while debugging */
    /* Put in crosstalk information (info from the crosstalk model) */
    for (lep = t->keyCrosstalk[k]; lep != NULL; lep = lep->next) {
      cpmInsert (t, lep, cpmBus, cpmGain, wheelNumber, rowLength, &endRow);
    }
#endif
    /* Read the matrix and generate a list entry in the keyContrib table. */
    for (w = 0; w < endRow; w++) {
      int c;
      for (c = 0; c < rowLength[w]; c++) {
#if 0
	/* Original code 22-sep-2004/FK */
	ListElement * rep = newRuntimeListElement (t);
	LE_WHEEL_NUMBER_OF(rep) = wheelNumber[w];
	LE_BUSNUMBER_OF(rep) = cpmBus[w][c];
	LE_LEVEL_OF(rep) = cpmGain[w][c];
	ListElement ** P;
#else
	/* Test code 22-sep-2004/FK */
	ListElement ** P;
	ListElement * rep;
	if (cpmGain[w][c] < t->contributionFloorLevel) continue;
	rep = newRuntimeListElement (t);
	LE_WHEEL_NUMBER_OF(rep) = wheelNumber[w];
	LE_BUSNUMBER_OF(rep)    = cpmBus[w][c];
	LE_LEVEL_OF(rep)        = cpmGain[w][c];

	if (LE_LEVEL_OF(rep) < t->contributionMinLevel) {
	  LE_LEVEL_OF(rep) = t->contributionMinLevel;
	}

#endif

	/* Insertion sort, first on wheel then on bus. */

	for (P = &(t->keyContrib[k]); (*P) != NULL; P = &((*P)->next)) {
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
    if (t->keyContrib[k] != NULL) {
      t->keyContrib[k]->next = NULL;
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
static int apply_CH_Spline (struct b_tonegen *tg,
			    int nofOscillators,
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
    tg->oscillators[i].attenuation = a;
  }

  return 0;
}

/**
 * Implements a built-in oscillator equalization curve.
 */
static int applyOscEQ_peak24 (struct b_tonegen *t, int nofOscillators) {
  int i;

  for (i = 1; i <= 43; i++) {
    t->oscillators[i].attenuation = damperCurve (i,  1, 43, 0.2, -0.8,  1.0);
  }

  for (i = 44; i <= 48; i++) {
    t->oscillators[i].attenuation = damperCurve (i, 44, 48, 1.6, -0.4, -0.3);
  }

  for (i = 49; i <= nofOscillators; i++) {
    t->oscillators[i].attenuation =
      damperCurve (i, 49, nofOscillators, 0.9, -1.0, -0.7);
  }

  return 0;
}

/**
 * Implements a built-in oscillator equalization curve.
 */
static int applyOscEQ_peak46 (struct b_tonegen *t, int nofOscillators) {
  int i;

  for (i = 1; i <= 43; i++) {
    t->oscillators[i].attenuation = damperCurve (i,  1, 43,  0.3, 0.4,  1.0);
  }

  for (i = 44; i <= 48; i++) {
    t->oscillators[i].attenuation = damperCurve (i, 44, 48,  0.1, -0.4, 0.4);
  }

  for (i = 49; i <= nofOscillators; i++) {
    t->oscillators[i].attenuation =
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

  assert(minSamples < maxSamples);

  minWaves = ceil  ((Hz * (double) minSamples) / SampleRateD);
  maxWaves = floor ((Hz * (double) maxSamples) / SampleRateD);

  assert (minWaves <= maxWaves);
  assert (minWaves > 0);

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
  assert (minSpn <= maxSamples);

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
  const double fullCircle = 2.0 * M_PI;
  double apl[MAX_PARTIALS];
  double plHz[MAX_PARTIALS];
  double aplSum;
  double U;
  float * yp = buf;
  unsigned int i;

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
	apl[j] * sin (remainder ((plHz[j] * fullCircle * (double) i) / SampleRateD,
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
static void initOscillators (struct b_tonegen *t, int variant, double precision) {
  int i;
  double baseTuning;
  int nofOscillators;
  int tuningOsc = 10;
  struct _oscillator * osp;
  double harmonicsList[MAX_PARTIALS];

  switch (variant) {

  case 0:
    nofOscillators = 91;
    baseTuning     = t->tuning / 8.0;
    tuningOsc      = 10;
    break;

  case 1:
    nofOscillators = 82;
    baseTuning     = t->tuning / 8.0;
    tuningOsc      = 1;
    break;

  case 2:
    nofOscillators = 91;
    baseTuning     = t->tuning / 8.0;
    tuningOsc      = 10;
    break;

  default:
    assert (0);
  } /* switch variant */

  /*
   * Apply equalisation curve. This sets the attenuation field in the
   * oscillator struct.
   */

  switch (t->eqMacro) {
  case EQ_SPLINE:
    apply_CH_Spline (t, nofOscillators, t->eqP1y, t->eqR1y, t->eqP4y, t->eqR4y);
    break;
  case EQ_PEAK24:
    applyOscEQ_peak24 (t, nofOscillators);
    break;
  case EQ_PEAK46:
    applyOscEQ_peak46 (t, nofOscillators);
    break;
  }

  for (i = 1; i <= nofOscillators; i++) {
    int j;
    size_t wszs;		/* Wave size samples */
    size_t wszb;		/* Wave size bytes */
    double tun;
    ListElement * lep;

    osp = & (t->oscillators[i]);

    if (t->eqvSet[i] != 0) {
      osp->attenuation = t->eqvAtt[i];
    }

    osp->aclPos = -1;
    osp->rflags =  0;
    osp->pos    =  0;

    tun = (double) (i - tuningOsc);

    if (t->gearTuning) {
			/* Frequency number minus one. The frequency number is the number of
			 * the oscillator on the tone generator with 91 oscillators. This
			 * means that the frequency number here always is 0 for c@37 Hz.
			 * The first tonegenerator is always numbered one, but depending
			 * on the organ model generator number 1 may be c@37 Hz (91 osc) or
			 * a@55 Hz (86 osc).
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

      if (t->gearTuning == 1) {
	gearA = gears60ratios[select][0];
	gearB = gears60ratios[select][1];
	osp->frequency = (20.0 * teeth * gearA) / gearB;
      }
      else {
	gearA = gears50ratios[select][0];
	gearB = gears50ratios[select][1];
	osp->frequency = (25.0 * teeth * gearA) / gearB;
      }
      osp->frequency *= (t->tuning / 440.0);
    }
    else {
      osp->frequency = baseTuning * pow (2.0, tun / 12.0);
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
		    ceil(SampleRateD / 48000.0) * 4096);

    /* Compute the number of bytes needed for exactly one wave buffer. */

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
      harmonicsList[j] = t->wheel_Harmonics[j];
    }

    /* Add optional global default from configuration */

    for (lep = t->wheelHarmonics[0]; lep != NULL; lep = lep->next) {
      int h = LE_HARMONIC_NUMBER_OF(lep) - 1;
      assert (0 <= h);
      if (h < MAX_PARTIALS) {
	harmonicsList[h] += LE_HARMONIC_LEVEL_OF(lep);
      }
    }

    /* Then add any harmonics specific to this wheel. */

    for (lep = t->wheelHarmonics[i]; lep != NULL; lep = lep->next) {
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
void setVibratoUpper (struct b_tonegen *t, int isEnabled) {
  if (isEnabled) {
    t->newRouting |= RT_UPPRVIB;
  } else {
    t->newRouting &= ~RT_UPPRVIB;
  }
}

/**
 * Controls the routing of the lower manual through the vibrato scanner.
 */
void setVibratoLower (struct b_tonegen *t, int isEnabled) {
  if (isEnabled) {
    t->newRouting |= RT_LOWRVIB;
  } else {
    t->newRouting &= ~RT_LOWRVIB;
  }
}

int getVibratoRouting (struct b_tonegen *t) {
  int rv = 0;
  if ((t->newRouting & RT_LOWRVIB))
    rv |=1;
  if ((t->newRouting & RT_UPPRVIB))
    rv |=2;
  return rv;
}

/**
 * This routine sets percussion on or off.
 *
 * @param isEnabled  If true, percussion is enabled. If false, percussion
 *                   is disabled.
 */
void setPercussionEnabled (struct b_tonegen *t, int isEnabled) {

  if (isEnabled) {
    t->newRouting |=  RT_PERC;
    if (-1 < t->percTriggerBus) {
      t->drawBarGain[t->percTriggerBus] = 0.0;
      t->drawBarChange = 1;
    }
  } else {
    t->newRouting &= ~RT_PERC;
    if (-1 < t->percTriggerBus) {
      t->drawBarGain[t->percTriggerBus] =
	t->drawBarLevel[t->percTriggerBus][t->percTrigRestore];
      t->drawBarChange = 1;
    }
  }
  t->percEnabled = isEnabled;
}

/**
 * This routine sets the percCounterReset variable from the current
 * combination of the control flags percIsFast and percIsSoft.
 */
static void setPercussionResets (struct b_tonegen *t) {
  if (t->percIsFast) {

    /* Alternate 25-May-2003 */
    t->percEnvGainDecay =
      t->percIsSoft ? t->percEnvGainDecayFastSoft : t->percEnvGainDecayFastNorm;

  }
  else {			/* Slow */

    /* Alternate 25-May-2003 */
    t->percEnvGainDecay =
      t->percIsSoft ? t->percEnvGainDecaySlowSoft : t->percEnvGainDecaySlowNorm;

  }
}

/**
 * Selects fast or slow percussion.
 * @param isFast  If true, selects fast percussion decay. If false,
 *                selects slow percussion decay.
 */
void setPercussionFast (struct b_tonegen *t, int isFast) {
  t->percIsFast = isFast;
  setPercussionResets (t);
}

/**
 * Selects soft or normal percussion volume.
 * @param isSoft  If true, selects soft percussion. If false, selects
 *                normal percussion.
 */
void setPercussionVolume (struct b_tonegen *t, int isSoft) {
  t->percIsSoft = isSoft;

  /* Alternate 25-May-2003 */
  t->percEnvGainReset =
    t->percEnvScaling * (isSoft ? t->percEnvGainResetSoft : t->percEnvGainResetNorm);

  t->percDrawbarGain = isSoft ? t->percDrawbarSoftGain : t->percDrawbarNormalGain;

  setPercussionResets (t);
}

/**
 * Selects first or second choice of percussion tone tap.
 */
void setPercussionFirst (struct b_tonegen *t, int isFirst) {
  if (isFirst) {
    t->percSendBus = t->percSendBusA;
  } else {
    t->percSendBus = t->percSendBusB;
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
static void computePercResets (struct b_tonegen *t) {

  /* Compute the percussion reset values. */

  /* Alternate 25-May-2003 */
  t->percEnvGainDecayFastNorm = getPercDecayConst_sec (t->percEnvGainResetNorm,
						    dBToGain (-60.0),
						    t->percFastDecaySeconds);

  t->percEnvGainDecayFastSoft = getPercDecayConst_sec (t->percEnvGainResetSoft,
						    dBToGain (-60.0),
						    t->percFastDecaySeconds);

  t->percEnvGainDecaySlowNorm = getPercDecayConst_sec (t->percEnvGainResetNorm,
						    dBToGain (-60.0),
						    t->percSlowDecaySeconds);

  t->percEnvGainDecaySlowSoft = getPercDecayConst_sec (t->percEnvGainResetSoft,
						    dBToGain (-60.0),
						    t->percSlowDecaySeconds);

  /* Deploy the computed reset values. */

  setPercussionResets (t);
}


/**
 * This routine sets the fast percussion decay time.
 * @param seconds  The percussion decay time.
 */
void setFastPercussionDecay (struct b_tonegen *t, double seconds) {
  t->percFastDecaySeconds = seconds;
  if (t->percFastDecaySeconds <= 0.0) {
    t->percFastDecaySeconds = 0.1;
  }
  computePercResets (t);
}

/**
 * This routine sets the slow percussion decay time.
 * @param seconds  The percussion decay time.
 */
void setSlowPercussionDecay (struct b_tonegen *t, double seconds) {
  t->percSlowDecaySeconds = seconds;
  if (t->percSlowDecaySeconds <= 0.0) {
    t->percSlowDecaySeconds = 0.1;
  }
  computePercResets (t);
}

/**
 * Sets the percussion starting gain of the envelope for normal volume.
 * The expected level is 1.0 with soft volume less than that.
 * @param g  The starting gain.
 */
void setNormalPercussionGain (struct b_tonegen *t, double g) {
  t->percEnvGainResetNorm = (float) g;
}

/**
 * Sets the percussion starting gain of the envelope for soft volume.
 * The expected level is less than 1.0.
 * @param g  The starting gain.
 */
void setSoftPercussionGain (struct b_tonegen *t, double g) {
  t->percEnvGainResetSoft = (float) g;
}

/**
 * Sets the percussion gain scaling factor.
 * @param s  The scaling factor.
 */
void setPercussionGainScaling (struct b_tonegen *t, double s) {
  t->percEnvScaling = (float) s;
}

void setEnvAttackModel (struct b_tonegen *t, int model) {
  if ((0 <= model) && (model < ENV_CLICKMODELS)) {
    t->envAttackModel = model;
  }
}

void setEnvReleaseModel (struct b_tonegen *t, int model) {
  if ((0 <= model) && (model < ENV_CLICKMODELS)) {
    t->envReleaseModel = model;
  }
}

/**
 * This parameter actually simulates the amount of noise created when the
 * closing/opening contact surfaces move against each other. Setting it
 * to zero will still click, due to the timing differences in the separate
 * envelopes (they close at different times). However, a higher value will
 * simulate more oxidization of the contacts, as in a worn instrument.
 */
void setEnvAttackClickLevel (struct b_tonegen *t, double u) {
  t->envAttackClickLevel = u;
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
void setEnvAtkClkMinLength (struct b_tonegen *t, double u) {
  setEnvAtkClkLength (&t->envAtkClkMinLength, u);
}

/**
 * Sets the maximum duration of a keyclick noise burst.
 */
void setEnvAtkClkMaxLength (struct b_tonegen *t, double u) {
  setEnvAtkClkLength (&t->envAtkClkMaxLength, u);
}

void setEnvReleaseClickLevel (struct b_tonegen *t, double u) {
 t->envReleaseClickLevel = u;
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
static void initKeyCompTable (struct b_tonegen *t) {
  int i;
  float u = -5.0;
  float v = -9.0;
  float m = 1.0 / (MAXKEYS - 12);

 /* The first two entries, 0 and 1, should be equal! */

  t->keyCompTable[ 0] = t->keyCompTable[1] = KEYCOMP_ZERO_LEVEL;
  t->keyCompTable[ 2] = dBToGain ( -1.1598);
  t->keyCompTable[ 3] = dBToGain ( -2.0291);
  t->keyCompTable[ 4] = dBToGain ( -2.4987);
  t->keyCompTable[ 5] = dBToGain ( -2.9952);
  t->keyCompTable[ 6] = dBToGain ( -3.5218);
  t->keyCompTable[ 7] = dBToGain ( -4.0823);
  t->keyCompTable[ 8] = dBToGain ( -4.6815);
  t->keyCompTable[ 9] = dBToGain ( -4.9975);
  t->keyCompTable[10] = dBToGain ( -4.9998);

  /* Linear interpolation from u to v. */

  for (i = 11; i < MAXKEYS; i++) {
    float a = (float) (i - 11);
    t->keyCompTable[i] = dBToGain (u + ((v - u) * a * m));
  }

}

#endif /* KEYCOMPRESSION */

#if DEBUG_TONEGEN_OSC
/**
 * Dumps the configuration lists to a text file.
 */
static void dumpConfigLists (struct b_tonegen *t, char * fname) {
  FILE * fp;
  int i;
  int j;

  if ((fp = fopen (fname, "w")) != NULL) {

    fprintf (fp,
	     "%s\n\n",
	     "Array wheelHarmonics (index is wheel number)");

    for (i = 0; i <= NOF_WHEELS; i++) {
      fprintf (fp, "wheelHarmonics[%2d]=", i);
      if (t->wheelHarmonics[i] == NULL) {
	fprintf (fp, "NULL");
      }
      else {
	ListElement * lep;
	j = 0;
	for (lep = t->wheelHarmonics[i]; lep != NULL; lep = lep->next) {
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
      if (t->terminalMix[i] == NULL) {
	fprintf (fp, "NULL");
      }
      else {
	ListElement * lep;
	j = 0;
	for (lep = t->terminalMix[i]; lep != NULL; lep = lep->next) {
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
      if (t->keyTaper[i] == NULL) {
	fprintf (fp, "NULL");
      }
      else {
	ListElement * lep;
	j = 0;
	for (lep = t->keyTaper[i]; lep != NULL; lep = lep->next) {
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
      if (t->keyCrosstalk[i] == NULL) {
	fprintf (fp, "NULL");
      }
      else {
	ListElement * lep;
	j = 0;
	for (lep = t->keyCrosstalk[i]; lep != NULL; lep = lep->next) {
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
static void dumpRuntimeData (struct b_tonegen *t, char * fname) {
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
      for (rep = t->keyContrib[k]; rep != NULL; rep = rep->next) {
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

/**
 * Dump the oscillator array for diagnostics.
 */
static int dumpOscToText (struct b_tonegen *t, char * fname) {
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
	     t->oscillators[i].frequency,
	     t->oscillators[i].lengthSamples,
	     t->oscillators[i].lengthSamples * sizeof (float),
	     t->oscillators[i].attenuation);
    bufferSamples += t->oscillators[i].lengthSamples;
  }

  fprintf (fp, "TOTAL MEMORY: %zu samples, %zu bytes\n",
	   bufferSamples,
	   bufferSamples * sizeof (float));


  fclose (fp);
  return 0;
}
#endif

/**
 * This routine configures this module.
 */
int oscConfig (struct b_tonegen *t, ConfigContext * cfg) {
  int ack = 0;
  double d;
  int ival;
  if ((ack = getConfigParameter_d ("osc.tuning", cfg, &d)) == 1) {
    setTuning (t, d);
  }
  else if (!strcasecmp (cfg->name, "osc.temperament")) {
    ack++;
    if (!strcasecmp (cfg->value, "equal")) {
      t->gearTuning = 0;
    }
    else if (!strcasecmp (cfg->value, "gear60")) {
      t->gearTuning = 1;
    }
    else if (!strcasecmp (cfg->value, "gear50")) {
      t->gearTuning = 2;
    }
    else {
      showConfigfileContext (cfg, "'equal', 'gear60', or 'gear50' expected");
    }
  }
  else if ((ack = getConfigParameter_d ("osc.x-precision", cfg, &d)) == 1) {
    setWavePrecision (t, d);
  }
  else if ((ack = getConfigParameter_d ("osc.perc.fast",
				       cfg,
				       &t->percFastDecaySeconds))) {
    ;
  }
  else if ((ack = getConfigParameter_d ("osc.perc.slow",
				       cfg,
				       &t->percSlowDecaySeconds))) {
    ;
  }
  else if ((ack = getConfigParameter_d ("osc.perc.normal", cfg, &d)) == 1) {
    setNormalPercussionGain (t, d);
  }
  else if ((ack = getConfigParameter_d ("osc.perc.soft", cfg, &d)) == 1) {
    setSoftPercussionGain (t, d);
  }
  else if ((ack = getConfigParameter_d ("osc.perc.gain", cfg, &d)) == 1) {
    setPercussionGainScaling (t, d);
  }
  else if ((ack = getConfigParameter_ir ("osc.perc.bus.a",
					 cfg,
					 &ival,
					 0, 8)) == 1) {
    t->percSendBusA = ival;
  }
  else if ((ack = getConfigParameter_ir ("osc.perc.bus.b",
					 cfg,
					 &ival,
					 0, 8)) == 1) {
    t->percSendBusB = ival;
  }
  else if ((ack = getConfigParameter_ir ("osc.perc.bus.trig",
					 cfg,
					 &ival,
					 -1, 8)) == 1) {
    t->percTriggerBus = ival;
  }
  else if (!strcasecmp (cfg->name, "osc.eq.macro")) {
    ack++;
    if (!strcasecmp (cfg->value, "chspline")) {
      t->eqMacro = EQ_SPLINE;
    } else if (!strcasecmp (cfg->value, "peak24")) {
      t->eqMacro = EQ_PEAK24;
    } else if (!strcasecmp (cfg->value, "peak46")) {
      t->eqMacro = EQ_PEAK46;
    } else {
      showConfigfileContext (cfg, "expected chspline, peak24 or peak46");
    }
  }
  else if ((ack = getConfigParameter_d ("osc.eq.p1y", cfg, &t->eqP1y)))
    ;
  else if ((ack = getConfigParameter_d ("osc.eq.r1y", cfg, &t->eqR1y)))
    ;
  else if ((ack = getConfigParameter_d ("osc.eq.p4y", cfg, &t->eqP4y)))
    ;
  else if ((ack = getConfigParameter_d ("osc.eq.r4y", cfg, &t->eqR4y)))
    ;
  else if ((ack = getConfigParameter_d ("osc.eqv.ceiling", cfg, &t->eqvCeiling)))
    ;
  else if (!strncasecmp (cfg->name, "osc.eqv.", 8)) {
    int n;
    double v;
    ack++;
    if (sscanf (cfg->name, "osc.eqv.%d", &n) == 1) {
      if ((0 <= n) && (n <= 127)) {
	if (sscanf (cfg->value, "%lf", &v) == 1) {
	  if ((0.0 <= v) && (v <= t->eqvCeiling)) {
	    t->eqvAtt[n] = v / t->eqvCeiling;
	    t->eqvSet[n] = 1;
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
	ListElement * lep = newConfigListElement (t);
	LE_HARMONIC_NUMBER_OF(lep) = (short) n;
	LE_HARMONIC_LEVEL_OF(lep) = (float) v;
	appendListElement (&(t->wheelHarmonics[0]), lep);
      }
      else {
	configDoubleUnparsable (cfg);
      }

    }
    else if (sscanf (cfg->name, "osc.harmonic.w%d.f%d", &w, &n) == 2) {
      if ((0 < w) && (w <= NOF_WHEELS)) {
	if (sscanf (cfg->value, "%lf", &v) == 1) {
	  ListElement * lep = newConfigListElement (t);
	  LE_HARMONIC_NUMBER_OF(lep) = (short) n;
	  LE_HARMONIC_LEVEL_OF(lep) = (float) v;
	  appendListElement (&(t->wheelHarmonics[w]), lep);
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
	  ListElement * lep = newConfigListElement (t);
	  LE_WHEEL_NUMBER_OF(lep) = (short) w;
	  LE_WHEEL_LEVEL_OF(lep) = (float) v;
	  appendListElement (&(t->terminalMix[n]), lep);
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
    int w;
    double v;
    char buf[128];
    ack++;
    if (sscanf (cfg->name, "osc.taper.k%d.b%d.t%d", &k, &b, &w) == 3) {
      if ((0 < k) && (k < MAX_KEYS)) {
	if ((0 < b) && (b < NOF_BUSES)) {
	  if ((0 < w) && (w <= NOF_WHEELS)) {
	    if (sscanf (cfg->value, "%lf", &v) == 1) {
	      ListElement * lep = newConfigListElement (t);
	      LE_TERMINAL_OF(lep) = w;
	      LE_BUSNUMBER_OF(lep) = b;
	      LE_TAPER_OF(lep) = (float) v;
	      appendListElement (&t->keyTaper[k], lep);
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
	int w;
	double v;
	const char * s = cfg->value;
	do {
	  if (sscanf (s, "%d:%d:%lf", &b, &w, &v) == 3) {
	    if ((0 < b) && (b < NOF_BUSES)) {
	      if ((0 < w) && (w <= NOF_WHEELS)) {
		ListElement * lep = newConfigListElement (t);
		LE_TERMINAL_OF(lep) = w;
		LE_BUSNUMBER_OF(lep) = b;
		LE_LEVEL_OF(lep) = v;
		appendListElement (&(t->keyCrosstalk[k]), lep);
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
    t->defaultCompartmentCrosstalk = d;
  }
  else if ((ack = getConfigParameter_dr ("osc.transformer-crosstalk",
					 cfg, &d, 0.0, 1.0)) == 1) {
    t->defaultTransformerCrosstalk = d;
  }
  else if ((ack = getConfigParameter_dr ("osc.terminalstrip-crosstalk",
					 cfg, &d, 0.0, 1.0)) == 1) {
    t->defaultTerminalStripCrosstalk = d;
  }
  else if ((ack = getConfigParameter_dr ("osc.wiring-crosstalk",
					 cfg, &d, 0.0, 1.0)) == 1) {
    t->defaultWiringCrosstalk = d;
  }
  else if ((ack = getConfigParameter_dr ("osc.contribution-floor",
					 cfg, &d, 0.0, 1.0)) == 1) {
    t->contributionFloorLevel = d;
  }
  else if ((ack = getConfigParameter_dr ("osc.contribution-min",
					 cfg, &d, 0.0, 1.0)) == 1) {
    t->contributionMinLevel = d;
  }
  else if ((ack = getConfigParameter_dr ("osc.attack.click.level",
					 cfg, &d, 0.0, 1.0)) == 1) {
    setEnvAttackClickLevel (t, d);
  }
  else if ((ack = getConfigParameter_dr ("osc.attack.click.maxlength",
					 cfg, &d, 0.0, 1.0)) == 1) {
    setEnvAtkClkMaxLength (t, d);
  }
  else if ((ack = getConfigParameter_dr ("osc.attack.click.minlength",
					 cfg, &d, 0.0, 1.0)) == 1) {
    setEnvAtkClkMinLength (t, d);
  }
  else if ((ack = getConfigParameter_dr ("osc.release.click.level",
					 cfg, &d, 0.0, 1.0)) == 1) {
    setEnvReleaseClickLevel (t, d);
  }
  else if (!strcasecmp (cfg->name, "osc.release.model")) {
    ack++;
    if (!strcasecmp (getConfigValue (cfg), "click")) {
      setEnvReleaseModel (t, ENV_CLICK);
    }
    else if (!strcasecmp (getConfigValue (cfg), "cosine")) {
      setEnvReleaseModel (t, ENV_COSINE);
    }
    else if (!strcasecmp (getConfigValue (cfg), "linear")) {
      setEnvReleaseModel (t, ENV_LINEAR);
    }
    else if (!strcasecmp (getConfigValue (cfg), "shelf")) {
      setEnvReleaseModel (t, ENV_SHELF);
    }
  }
  else if (!strcasecmp (cfg->name, "osc.attack.model")) {
    ack++;
    if (!strcasecmp (getConfigValue (cfg), "click")) {
      setEnvAttackModel (t, ENV_CLICK);
    }
    else if (!strcasecmp (getConfigValue (cfg), "cosine")) {
      setEnvAttackModel (t, ENV_COSINE);
    }
    else if (!strcasecmp (getConfigValue (cfg), "linear")) {
      setEnvAttackModel (t, ENV_LINEAR);
    }
    else if (!strcasecmp (getConfigValue (cfg), "shelf")) {
      setEnvAttackModel (t, ENV_SHELF);
    }
  }
  return ack;
}

/**
 * This routine initialises the envelope shape tables.
 */
static void initEnvelopes (struct b_tonegen *t) {
  int bss = BUFFER_SIZE_SAMPLES;
  int b;
  int i;			/* 0 -- 127 */
  int burst;			/* Samples in noist burst */
  int bound;
  int start;			/* Sample where burst starts */
  double T = (double) (BUFFER_SIZE_SAMPLES - 1); /* 127.0 */

  for (b = 0; b < 9; b++) {

    if (t->envAttackModel == ENV_CLICK) {
      /* Select a random length of this burst. */
      bound = t->envAtkClkMaxLength - t->envAtkClkMinLength;
      if (bound < 1) {
	bound = 1;
      }
      burst = t->envAtkClkMinLength + (rand () % bound);
      if (bss <= burst) {
	burst = bss - 1;
      }
      /* Select a random start position of the burst. */
      start = (rand () % (bss - burst));
      /* From sample 0 to start the amplification is zero. */
      for (i = 0; i < start; i++) t->attackEnv[b][i] = 0.0;
      /* In the burst area the amplification is random. */
      for (; i < (start + burst); i++) {
	t->attackEnv[b][i] = 1.0 - (t->envAttackClickLevel * drnd ());
      }
      /* From the end of the burst to the end of the envelope the amplification is unity. */
      for (; i < bss; i++) t->attackEnv[b][i] = 1.0;

#if 1
      /* 2002-08-31/FK EXPERIMENTAL */
      /* Two-point average low-pass filter. */
      {
	t->attackEnv[b][0] /= 2.0;
	for (i = 1; i < bss; i++) {
	  t->attackEnv[b][i] = (t->attackEnv[b][i-1] + t->attackEnv[b][i]) / 2.0;
	}
      }
#endif

    }

    if (t->envAttackModel == ENV_SHELF) {
      bound = t->envAtkClkMaxLength - t->envAtkClkMinLength;
      if (bound < 1) bound = 1;
      start = rand () % bound;
      if ((bss - 2) <= start) start = bss - 2;
      for (i = 0; i < start; i++) t->attackEnv[b][i] = 0.0;
      t->attackEnv[b][i + 0] = 0.33333333;
      t->attackEnv[b][i + 1] = 0.66666666;
      for (i = i + 2; i < bss; i++) {
	t->attackEnv[b][i] = 1.0;
      }
    }

    if (t->envReleaseModel == ENV_SHELF) {
      bound = t->envAtkClkMaxLength - t->envAtkClkMinLength;
      if (bound < 1) bound = 1;
      start = rand () % bound;
      if ((bss - 2) <= start) start = bss - 2;
      for (i = 0; i < start; i++) t->releaseEnv[b][i] = 0.0;
      t->releaseEnv[b][i + 0] = 0.33333333;
      t->releaseEnv[b][i + 1] = 0.66666666;
      for (i = i + 2; i < bss; i++) {
	t->releaseEnv[b][i] = 1.0;
      }
    }

    if (t->envReleaseModel == ENV_CLICK) {
      burst = 8 + (rand () % 32);
      start = (rand () % (bss - burst));

      for (i = 0; i < start; i++) t->releaseEnv[b][i] = 0.0;
      for (; i < (start + burst); i++) {
	t->releaseEnv[b][i] = 1.0 - (t->envReleaseClickLevel * drnd ());
      }
      for (; i < bss; i++) t->releaseEnv[b][i] = 1.0;
      /* Filter the envelope */
      t->releaseEnv[b][0] /= 2.0;
      for (i = 1; i < bss; i++) {
	t->releaseEnv[b][i] = (t->releaseEnv[b][i-1] + t->releaseEnv[b][i]) / 2.0;
      }
    }

    /* cos(0)=1.0, cos(PI/2)=0, cos(PI)=-1.0 */

    if (t->envAttackModel == ENV_COSINE) {	/* Sigmoid decay */
      for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
	int d = BUFFER_SIZE_SAMPLES - (i + 1);
	double a = (M_PI * (double) d) / T;	/* PI < a <= 0 */
	t->attackEnv [b][i] = 0.5 + (0.5 * cos (a));
      }
    }

    if (t->envReleaseModel == ENV_COSINE) {
      for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
	double a = (M_PI * (double) i) / T;	/* 0 < b <= PI */
	t->releaseEnv[b][i] = 0.5 - (0.5 * cos (a));
      }
    }

    if (t->envAttackModel == ENV_LINEAR) {	/* Linear decay */
      int k = BUFFER_SIZE_SAMPLES;			/* TEST SPECIAL */

      for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
	if (i < k) {
	  t->attackEnv[b][i]  = ((float) i) / (float) k;
	} else {
	  t->attackEnv[b][i] = 1.0;
	}
      }
    }

    if (t->envReleaseModel == ENV_LINEAR) {
      int k = BUFFER_SIZE_SAMPLES;			/* TEST SPECIAL */

      for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
	if (i < k) {
	  t->releaseEnv[b][i] = ((float) i) / (float) k;
	} else {
	  t->releaseEnv[b][i] = 1.0;
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
static void setDrawBar (struct b_tonegen *t, int bus, unsigned int setting) {
  assert ((0 <= bus) && (bus < NOF_BUSES));
  assert ((0 <= setting) && (setting < 9));
  t->drawBarChange = 1;
  if (bus == t->percTriggerBus) {
    t->percTrigRestore = setting;
    if (t->percEnabled) return;
  }
  t->drawBarGain[bus] = t->drawBarLevel[bus][setting];
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
void setDrawBars (void *inst, unsigned int manual, unsigned int setting []) {
  struct b_tonegen *t = ((b_instance*)inst)->synth;
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
    setDrawBar (t, offset + i, setting[i]);
    notifyControlChangeById(((b_instance*)inst)->midicfg, offset + i, 127 - (setting[i] * 127 / 8));
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

static void setMIDIDrawBar (struct b_tonegen *t, int bus, unsigned char v) {
  int val = 127 - v;
  setDrawBar (t, bus, rint(val * 8.0 / 127.0));
}

static void setDrawbar0 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 0, v);}
static void setDrawbar1 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 1, v);}
static void setDrawbar2 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 2, v);}
static void setDrawbar3 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 3, v);}
static void setDrawbar4 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 4, v);}
static void setDrawbar5 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 5, v);}
static void setDrawbar6 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 6, v);}
static void setDrawbar7 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 7, v);}
static void setDrawbar8 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 8, v);}

static void setDrawbar9  (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d,  9, v);}
static void setDrawbar10 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 10, v);}
static void setDrawbar11 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 11, v);}
static void setDrawbar12 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 12, v);}
static void setDrawbar13 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 13, v);}
static void setDrawbar14 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 14, v);}
static void setDrawbar15 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 15, v);}
static void setDrawbar16 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 16, v);}
static void setDrawbar17 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 17, v);}

static void setDrawbar18 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 18, v);}
static void setDrawbar19 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 19, v);}
static void setDrawbar20 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 20, v);}
static void setDrawbar21 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 21, v);}
static void setDrawbar22 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 22, v);}
static void setDrawbar23 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 23, v);}
static void setDrawbar24 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 24, v);}
static void setDrawbar25 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 25, v);}
static void setDrawbar26 (void *d, unsigned char v) { setMIDIDrawBar ((struct b_tonegen*)d, 26, v);}

/**
 * This routine controls percussion from a MIDI controller.
 * It turns percussion on and off and switches between normal and soft.
 * On a slider-type controller you get:
 *
 *  off   on normal       on soft        off
 * 0---16-------------64-------------112------127
 *
 */
static void setPercEnableFromMIDI (void *d, unsigned char u) {
  struct b_tonegen *t = (struct b_tonegen *) d;
  setPercussionEnabled (t, u < 64 ? FALSE : TRUE);
}

/**
 * This routine controls percussion from a MIDI controller.
 * It sets fast or slow decay.
 */
static void setPercDecayFromMIDI (void *d, unsigned char u) {
  struct b_tonegen *t = (struct b_tonegen *) d;
  setPercussionFast (t, u < 64 ? FALSE : TRUE);
}

/**
 * This routine controls percussion from a MIDI controller.
 * It sets the third or second harmonic.
 */
static void setPercHarmonicFromMIDI (void *d, unsigned char u) {
  struct b_tonegen *t = (struct b_tonegen *) d;
  setPercussionFirst (t, u < 64 ? FALSE : TRUE);
}

static void setPercVolumeFromMIDI (void *d, unsigned char u) {
  struct b_tonegen *t = (struct b_tonegen *) d;
  setPercussionVolume (t, u < 64 ? FALSE : TRUE);
}

/**
 * This routine controls the swell pedal from a MIDI controller.
 */
static void setSwellPedal1FromMIDI (void *d, unsigned char u) {
  struct b_tonegen *t = (struct b_tonegen *) d;
  t->swellPedalGain = (t->outputLevelTrim * ((double) u)) / 127.0;
  notifyControlChangeByName (t->midi_cfg_ptr, "swellpedal2", u);
}

static void setSwellPedal2FromMIDI (void *d, unsigned char u) {
  struct b_tonegen *t = (struct b_tonegen *) d;
  t->swellPedalGain = (t->outputLevelTrim * ((double) u)) / 127.0;
  notifyControlChangeByName (t->midi_cfg_ptr, "swellpedal1", u);
}


/**
 * This routine initialises this module. When we come here during startup,
 * configuration files have already been read, so parameters should already
 * be set.
 */
void initToneGenerator (struct b_tonegen *t, void *m) {
  int i;

  t->midi_cfg_ptr = m;

  /* init global variables */
  t->percIsSoft=t->percIsFast=0;
  t->percEnvGain=0;
  for (i=0; i< NOF_BUSES; ++i) {
    int j;
    t->drawBarGain[i]=0;
    for (j=0; j< 9; ++j) {
      t->drawBarLevel[i][j] = 0;
    }
  }
  for (i=0; i< MAX_KEYS; ++i)
    t->activeKeys[i] = 0;
  for (i=0; i< MAX_KEYS/32; ++i)
    t->_activeKeys[i] = 0;
  for (i=0; i< CR_PGMMAX; ++i)
    memset((void*)&t->corePgm[i], 0, sizeof(CoreIns));
  for (i=0; i<= NOF_WHEELS; ++i)
    memset((void*)&t->oscillators[i], 0, sizeof(struct _oscillator));
  for (i=0; i< 128; ++i) {
    t->eqvAtt[i]=0.0; t->eqvSet[i]='\0';
  }

  if (t->envAtkClkMinLength<0) {
    t->envAtkClkMinLength = floor(SampleRateD * 8.0 / 22050.0);
  }
  if (t->envAtkClkMaxLength<0) {
    t->envAtkClkMaxLength = ceil(SampleRateD * 40.0 / 22050.0);
  }

  if (t->envAtkClkMinLength > BUFFER_SIZE_SAMPLES) {
    t->envAtkClkMinLength = BUFFER_SIZE_SAMPLES;
  }
  if (t->envAtkClkMaxLength > BUFFER_SIZE_SAMPLES) {
    t->envAtkClkMaxLength = BUFFER_SIZE_SAMPLES;
  }

  applyDefaultConfiguration (t);

#if DEBUG_TONEGEN_OSC
  dumpConfigLists (t, "osc_cfglists.txt");
#endif

  compilePlayMatrix (t);

#if DEBUG_TONEGEN_OSC
  dumpRuntimeData (t, "osc_runtime.txt");
#endif

  /* Allocate taper buffers, initialize oscillator structs, build keyOsc. */
  initOscillators (t, t->tgVariant, t->tgPrecision);

#ifdef KEYCOMPRESSION

  initKeyCompTable (t);

#endif /* KEYCOMPRESSION */

  initEnvelopes (t);

  /* Initialise drawbar gain values */

  for (i = 0; i < NOF_BUSES; i++) {
    int setting;
    for (setting = 0; setting < 9; setting++) {
      float u = (float) setting;
      t->drawBarLevel[i][setting] = u / 8.0;
    }
  }

#if 1
  /* Gives the drawbars a temporary initial value */
  setMIDIDrawBar (t,  0, 8);
  setMIDIDrawBar (t,  1, 8);
  setMIDIDrawBar (t,  2, 6);

  setMIDIDrawBar (t,  9, 8);
  setMIDIDrawBar (t, 10, 3);
  setMIDIDrawBar (t, 11, 8);

  setMIDIDrawBar (t, 18, 8);
  setMIDIDrawBar (t, 20, 6);
#endif

  setPercussionFirst (t, FALSE);
  setPercussionVolume (t, FALSE);
  setPercussionFast (t, TRUE);
  setPercussionEnabled (t, FALSE);

  useMIDIControlFunction (m, "swellpedal1", setSwellPedal1FromMIDI, t);
  useMIDIControlFunction (m, "swellpedal2", setSwellPedal2FromMIDI, t);

  useMIDIControlFunction (m, "upper.drawbar16",  setDrawbar0, t);
  useMIDIControlFunction (m, "upper.drawbar513", setDrawbar1, t);
  useMIDIControlFunction (m, "upper.drawbar8",   setDrawbar2, t);
  useMIDIControlFunction (m, "upper.drawbar4",   setDrawbar3, t);
  useMIDIControlFunction (m, "upper.drawbar223", setDrawbar4, t);
  useMIDIControlFunction (m, "upper.drawbar2",   setDrawbar5, t);
  useMIDIControlFunction (m, "upper.drawbar135", setDrawbar6, t);
  useMIDIControlFunction (m, "upper.drawbar113", setDrawbar7, t);
  useMIDIControlFunction (m, "upper.drawbar1",   setDrawbar8, t);

  useMIDIControlFunction (m, "lower.drawbar16",  setDrawbar9, t);
  useMIDIControlFunction (m, "lower.drawbar513", setDrawbar10, t);
  useMIDIControlFunction (m, "lower.drawbar8",   setDrawbar11, t);
  useMIDIControlFunction (m, "lower.drawbar4",   setDrawbar12, t);
  useMIDIControlFunction (m, "lower.drawbar223", setDrawbar13, t);
  useMIDIControlFunction (m, "lower.drawbar2",   setDrawbar14, t);
  useMIDIControlFunction (m, "lower.drawbar135", setDrawbar15, t);
  useMIDIControlFunction (m, "lower.drawbar113", setDrawbar16, t);
  useMIDIControlFunction (m, "lower.drawbar1",   setDrawbar17, t);

  useMIDIControlFunction (m, "pedal.drawbar16",  setDrawbar18, t);
  useMIDIControlFunction (m, "pedal.drawbar513", setDrawbar19, t);
  useMIDIControlFunction (m, "pedal.drawbar8",   setDrawbar20, t);
  useMIDIControlFunction (m, "pedal.drawbar4",   setDrawbar21, t);
  useMIDIControlFunction (m, "pedal.drawbar223", setDrawbar22, t);
  useMIDIControlFunction (m, "pedal.drawbar2",   setDrawbar23, t);
  useMIDIControlFunction (m, "pedal.drawbar135", setDrawbar24, t);
  useMIDIControlFunction (m, "pedal.drawbar113", setDrawbar25, t);
  useMIDIControlFunction (m, "pedal.drawbar1",   setDrawbar26, t);

  useMIDIControlFunction (m, "percussion.enable",   setPercEnableFromMIDI, t);
  useMIDIControlFunction (m, "percussion.decay",    setPercDecayFromMIDI, t);
  useMIDIControlFunction (m, "percussion.harmonic", setPercHarmonicFromMIDI, t);
  useMIDIControlFunction (m, "percussion.volume",   setPercVolumeFromMIDI, t);

#if DEBUG_TONEGEN_OSC
  dumpOscToText (t, "osc.txt");
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

void freeToneGenerator (struct b_tonegen *t) {
  freeListElements(t->leConfig);
  freeListElements(t->leRuntime);
  int i;
  for (i=1; i <= NOF_WHEELS; i++) {
    if (t->oscillators[i].wave) free(t->oscillators[i].wave);
  }
  free(t);
}


/**
 * This function is the entry point for the MIDI parser when it has received
 * a NOTE OFF message on a channel and note number mapped to a playing key.
 */
void oscKeyOff (struct b_tonegen *t, unsigned char keyNumber, unsigned char realKey) {
  if (MAX_KEYS <= keyNumber) return;
  /* The key must be marked as on */
  if (t->activeKeys[keyNumber] != 0) {
    /* Flag the key as inactive */
    t->activeKeys[keyNumber] = 0;
    if (realKey != 255) {
      t->_activeKeys[realKey/32] &= ~(1<<(realKey%32));
    }
    /* Track upper manual keys for percussion trigger */
    if (keyNumber < 64) {
      t->upperKeyCount--;
    }
#ifdef KEYCOMPRESSION
    t->keyDownCount--;
    assert (0 <= t->keyDownCount);
#endif /* KEYCOMPRESSION */
    /* Write message saying that the key is released */
    *t->msgQueueWriter++ = MSG_KEY_OFF(keyNumber);
    /* Check for wrap on message queue */
    if (t->msgQueueWriter == t->msgQueueEnd) {
      t->msgQueueWriter = t->msgQueue;
    }
  } /* if key was active */

  /*  printf ("\rOFF:%3d", keyNumber); fflush (stdout); */

}

/**
 * This function is the entry point for the MIDI parser when it has received
 * a NOTE ON message on a channel and note number mapped to a playing key.
 */
void oscKeyOn (struct b_tonegen *t, unsigned char keyNumber, unsigned char realKey) {
  if (MAX_KEYS <= keyNumber) return;
  /* If the key is already depressed, release it first. */
  if (t->activeKeys[keyNumber] != 0) {
    oscKeyOff (t, keyNumber, realKey);
  }
  /* Mark the key as active */
  t->activeKeys[keyNumber] = 1;
  if (realKey != 255) {
    t->_activeKeys[realKey/32] |= (1<<(realKey%32));
  }
  /* Track upper manual for percussion trigger */
  if (keyNumber < 64) {
    t->upperKeyCount++;
  }
#ifdef KEYCOMPRESSION
  t->keyDownCount++;
#endif /* KEYCOMPRESSION */
  /* Write message */
  *t->msgQueueWriter++ = MSG_KEY_ON(keyNumber);
  /* Check for wrap on message queue */
  if (t->msgQueueWriter == t->msgQueueEnd) {
    t->msgQueueWriter = t->msgQueue;
  }

  // printf ("\rON :%3d", keyNumber); fflush (stdout);

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
void oscGenerateFragment (struct b_tonegen *t, float * buf, size_t lengthSamples) {

  int i;
  float * yptr = buf;
  struct _oscillator * osp;
  unsigned int copyDone = 0;
  unsigned int recomputeRouting;
  int removedEnd = 0;
  unsigned short * const removedList = t->removedList;
  float * const swlBuffer = t->swlBuffer;
  float * const vibBuffer = t->vibBuffer;
  float * const vibYBuffr = t->vibYBuffr;
  float * const prcBuffer = t->prcBuffer;

#ifdef KEYCOMPRESSION
  const float keyComp = t->keyCompTable[t->keyDownCount];
  const float keyCompDelta = (keyComp - t->keyCompLevel) / (float) BUFFER_SIZE_SAMPLES;
#define KEYCOMPCHASE() {t->keyCompLevel += keyCompDelta;}
#define KEYCOMPLEVEL (t->keyCompLevel)

#else

#define KEYCOMPLEVEL (1.0)
#define KEYCOMPCHASE()

#endif /* KEYCOMPRESSION */

  /* End of declarations */

  /* Reset the core program */
  t->coreWriter = t->coreReader = t->corePgm;

	/* ****************************************************************
	 *     M E S S S A G E   Q U E U E
	 * ****************************************************************/

  while (t->msgQueueReader != t->msgQueueWriter) {

    unsigned short msg = *t->msgQueueReader++; /* Read next message */
    int keyNumber;
    ListElement * lep;

    /* Check wrap on message queue */
    if (t->msgQueueReader == t->msgQueueEnd) {
      t->msgQueueReader = t->msgQueue;
    }

    if (MSG_GET_MSG(msg) == MSG_MKEYON) {
      keyNumber = MSG_GET_PRM(msg);
      for (lep = t->keyContrib[keyNumber]; lep != NULL; lep = lep->next) {
	int wheelNumber = LE_WHEEL_NUMBER_OF(lep);
	osp = &(t->oscillators[wheelNumber]);

	if (t->aot[wheelNumber].refCount == 0) {
	  /* Flag the oscillator as added and modified */
	  osp->rflags = OR_ADD;
	  /* If not already on the active list, add it */
	  if (osp->aclPos == -1) {
	    osp->aclPos = t->activeOscLEnd;
	    t->activeOscList[t->activeOscLEnd++] = wheelNumber;
	  }
	}
	else {
	  osp->rflags |= ORF_MODIFIED;
	}

	t->aot[wheelNumber].busLevel[LE_BUSNUMBER_OF(lep)] += LE_LEVEL_OF(lep);
	t->aot[wheelNumber].keyCount[LE_BUSNUMBER_OF(lep)] += 1;
	t->aot[wheelNumber].refCount += 1;
      }

    }
    else if (MSG_GET_MSG(msg) == MSG_MKEYOFF) {
      keyNumber = MSG_GET_PRM(msg);
      for (lep = t->keyContrib[keyNumber]; lep != NULL; lep = lep->next) {
	int wheelNumber = LE_WHEEL_NUMBER_OF(lep);
	osp = &(t->oscillators[wheelNumber]);

	t->aot[wheelNumber].busLevel[LE_BUSNUMBER_OF(lep)] -= LE_LEVEL_OF(lep);
	t->aot[wheelNumber].keyCount[LE_BUSNUMBER_OF(lep)] -= 1;
	t->aot[wheelNumber].refCount -= 1;

	assert (0 <= t->aot[wheelNumber].refCount);
	assert (-1 < osp->aclPos); /* Must be on the active osc list */

	if (t->aot[wheelNumber].refCount == 0) {
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

	/* ****************************************************************
	 *     A C T I V A T E D   L I S T
	 * ****************************************************************/

  if ((recomputeRouting = (t->oldRouting != t->newRouting))) {
    t->oldRouting = t->newRouting;
  }


  /*
   * At this point, new oscillators has been added to the active list
   * and removed oscillators are still on the list.
   */

  for (i = 0; i < t->activeOscLEnd; i++) {

    int oscNumber = t->activeOscList[i]; /* Get the oscillator number */
    AOTElement * aop = &(t->aot[oscNumber]); /* Get a pointer to active struct */
    osp = &(t->oscillators[oscNumber]); /* Point to the oscillator */

    if (osp->rflags & ORF_REMOVED) { /* Decay instruction for removed osc. */
      /* Put it on the removal list */
      removedList[removedEnd++] = oscNumber;

      /* All envelopes, both attack and release must traverse 0-1. */

      t->coreWriter->env = t->releaseEnv[i & 7];

      if (copyDone) {
	t->coreWriter->opr = CR_ADDENV;
      } else {
	t->coreWriter->opr = CR_CPYENV;
	copyDone = 1;
      }

      t->coreWriter->src = osp->wave + osp->pos;
      t->coreWriter->off = 0;	/* Start at the beginning of target buffers */
      t->coreWriter->sgain = aop->sumSwell;
      t->coreWriter->pgain = aop->sumPercn;
      t->coreWriter->vgain = aop->sumScanr;
      /* Target gain is zero */
      t->coreWriter->nsgain = t->coreWriter->npgain = t->coreWriter->nvgain = 0.0;

      if (osp->lengthSamples < (osp->pos + BUFFER_SIZE_SAMPLES)) {
	/* Need another instruction because of wrap */
	CoreIns * prev = t->coreWriter;
	t->coreWriter->cnt = osp->lengthSamples - osp->pos;
	osp->pos = BUFFER_SIZE_SAMPLES - t->coreWriter->cnt;
	t->coreWriter += 1;
	t->coreWriter->opr = prev->opr;
	t->coreWriter->src = osp->wave;
	t->coreWriter->off = prev->cnt;
	t->coreWriter->env = prev->env + prev->cnt;

	t->coreWriter->sgain = prev->sgain;
	t->coreWriter->pgain = prev->pgain;
	t->coreWriter->vgain = prev->vgain;

	t->coreWriter->nsgain = prev->nsgain;
	t->coreWriter->npgain = prev->npgain;
	t->coreWriter->nvgain = prev->nvgain;

	t->coreWriter->cnt = osp->pos;
      }
      else {
	t->coreWriter->cnt = BUFFER_SIZE_SAMPLES;
	osp->pos += BUFFER_SIZE_SAMPLES;
      }

      t->coreWriter += 1;
    }
    else {			/* ADD or MODIFIED */
      int reroute = 0;

      /*
       * Copy the current gains. For unmodified oscillators these will be
       * used. For modified oscillators we provide the update below.
       */
      if (osp->rflags & ORF_ADDED) {
	t->coreWriter->sgain = t->coreWriter->pgain = t->coreWriter->vgain = 0.0;
      }
      else {
	t->coreWriter->sgain = aop->sumSwell;
	t->coreWriter->pgain = aop->sumPercn;
	t->coreWriter->vgain = aop->sumScanr;
      }

      /* Update the oscillator's contribution to each busgroup mix */

      if ((osp->rflags & ORF_MODIFIED) || t->drawBarChange) {
	int d;
	float sum = 0.0;

	for (d = UPPER_BUS_LO; d < UPPER_BUS_END; d++) {
	  sum += aop->busLevel[d] * t->drawBarGain[d];
	}
	aop->sumUpper = sum;
	sum = 0.0;
	for (d = LOWER_BUS_LO; d < LOWER_BUS_END; d++) {
	  sum += aop->busLevel[d] * t->drawBarGain[d];
	}
	aop->sumLower = sum;
	sum = 0.0;
	for (d = PEDAL_BUS_LO; d < PEDAL_BUS_END; d++) {
	  sum += aop->busLevel[d] * t->drawBarGain[d];
	}
	aop->sumPedal = sum;
	reroute = 1;
      }

      /* If the group mix or routing has changed */

      if (reroute || recomputeRouting) {

	if (t->oldRouting & RT_PERC) { /* Percussion */
	  aop->sumPercn = aop->busLevel[t->percSendBus];
	}
	else {
	  aop->sumPercn = 0.0;
	}

	aop->sumScanr = 0.0;	/* Initialize scanner level */
	aop->sumSwell = aop->sumPedal; /* Initialize swell level */

	if (t->oldRouting & RT_UPPRVIB) { /* Upper manual ... */
	  aop->sumScanr += aop->sumUpper; /* ... to vibrato */
	}
	else {
	  aop->sumSwell += aop->sumUpper; /* ... to swell pedal */
	}

	if (t->oldRouting & RT_LOWRVIB) { /* Lower manual ... */
	  aop->sumScanr += aop->sumLower; /* ... to vibrato */
	}
	else {
	  aop->sumSwell += aop->sumLower; /* ... to swell pedal */
	}
      }	/* if rerouting */

      /* Emit instructions for oscillator */
      if (osp->rflags & OR_ADD) {
	/* Envelope attack instruction */
	t->coreWriter->env = t->attackEnv[i & 7];
	/* Next gain values */
	t->coreWriter->nsgain = aop->sumSwell;
	t->coreWriter->npgain = aop->sumPercn;
	t->coreWriter->nvgain = aop->sumScanr;

	if (copyDone) {
	  t->coreWriter->opr = CR_ADDENV;
	}
	else {
	  t->coreWriter->opr = CR_CPYENV;
	  copyDone = 1;
	}
      }
      else {
	if (copyDone) {
	  t->coreWriter->opr = CR_ADD;
	}
	else {
	  t->coreWriter->opr = CR_CPY;
	  copyDone = 1;
	}
      }

      /* The source is the wave of the oscillator at its current position */
      t->coreWriter->src = osp->wave + osp->pos;
      t->coreWriter->off = 0;


      if (osp->lengthSamples < (osp->pos + BUFFER_SIZE_SAMPLES)) {
	/* Instruction wraps source buffer */
	CoreIns * prev = t->coreWriter; /* Refer to the first instruction */
	t->coreWriter->cnt = osp->lengthSamples - osp->pos; /* Set len count */
	osp->pos = BUFFER_SIZE_SAMPLES - t->coreWriter->cnt; /* Updat src pos */

	t->coreWriter += 1;	/* Advance to next instruction */

	t->coreWriter->opr = prev->opr; /* Same operation */
	t->coreWriter->src = osp->wave; /* Start of wave because of wrap */
	t->coreWriter->off = prev->cnt;
	if (t->coreWriter->opr & 2) {
	  t->coreWriter->env = prev->env + prev->cnt; /* Continue envelope */
	}
	/* The gains are identical to the previous instruction */
	t->coreWriter->sgain = prev->sgain;
	t->coreWriter->pgain = prev->pgain;
	t->coreWriter->vgain = prev->vgain;

	t->coreWriter->nsgain = prev->nsgain;
	t->coreWriter->npgain = prev->npgain;
	t->coreWriter->nvgain = prev->nvgain;

	t->coreWriter->cnt = osp->pos; /* Up to next read position */
      }
      else {
	t->coreWriter->cnt = BUFFER_SIZE_SAMPLES;
	osp->pos += BUFFER_SIZE_SAMPLES;
      }

      t->coreWriter += 1;	/* Advance to next instruction */


    } /* else aot element not removed, ie modified or added */

    /* Clear rendering flags */
    osp->rflags = 0;

  } /* for the active list */

  t->drawBarChange = 0;

	/* ****************************************************************
	 *       R E M O V A L   L I S T
	 * ****************************************************************/

  /*
   * Core instructions are now written.
   * Process the removed entries list. [Could action be merged above?]
   */
  for (i = 0; i < removedEnd; i++) {
    int vicosc = removedList[i]; /* Victim oscillator number */
    int actidx = t->oscillators[vicosc].aclPos; /* Victim's active index */
    t->oscillators[vicosc].aclPos = -1; /* Reset victim's backindex */
    t->activeOscLEnd--;

    assert (0 <= t->activeOscLEnd);

    if (0 < t->activeOscLEnd) {	/* If list is not yet empty ... */
      int movosc = t->activeOscList[t->activeOscLEnd]; /* Fill hole w. last entry */
      if (movosc != vicosc) {
	t->activeOscList[actidx] = movosc;
	t->oscillators[movosc].aclPos = actidx;
      }
    }
  }

	/* ****************************************************************
	 *   C O R E   I N T E R P R E T E R
	 * ****************************************************************/

  /*
   * Special case: silence. If the vibrato scanner is used we must run zeros
   * through it because it is stateful (has a delay line).
   * We could possibly be more efficient here but for the moment we just zero
   * the input buffers to the mixing stage and reset the percussion.
   */

  if (t->coreReader == t->coreWriter) {
    float * ys = swlBuffer;
    float * yv = vibBuffer;
    float * yp = prcBuffer;

    for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
      *ys++ = 0.0;
      *yv++ = 0.0;
      *yp++ = 0.0;
    }

  }

  for (; t->coreReader < t->coreWriter; t->coreReader++) {
    short opr  = t->coreReader->opr;
    int     n  = t->coreReader->cnt;
    float * ys = swlBuffer + t->coreReader->off;
    float * yv = vibBuffer + t->coreReader->off;
    float * yp = prcBuffer + t->coreReader->off;
    const float   gs = t->coreReader->sgain;
    const float   gv = t->coreReader->vgain;
    const float   gp = t->coreReader->pgain;
    const float   ds = t->coreReader->nsgain - gs; /* Move these three down */
    const float   dv = t->coreReader->nvgain - gv;
    const float   dp = t->coreReader->npgain - gp;
    const float * ep  = t->coreReader->env;
    const float * xp  = t->coreReader->src;
    //printf("CR: %f %f +:%f  (ns:%f)\n", gs, ds, gs+ds, t->coreReader->nsgain );

    if (opr & 1) {		/* ADD and ADDENV */
      if (opr & 2) {		/* ADDENV */
	for (; 0 < n; n--) {
	  float x = (float) (*xp++);
	  const float e = *ep++;
	  *ys++ += x * (gs + (e * ds));
	  *yv++ += x * (gv + (e * dv));
	  *yp++ += x * (gp + (e * dp));
	}
      } else {			/* ADD */
	for (; 0 < n; n--) {
	  const float x = (float) (*xp++);
	  *ys++ += x * gs;
	  *yv++ += x * gv;
	  *yp++ += x * gp;
	}
      }

    } else {

      if (opr & 2) {		/* CPY and CPYENV */
	for (; 0 < n; n--) {	/* CPYENV */
	  const float x = (float) (*xp++);
	  const float e =  *ep++;
	  *ys++ = x * (gs + (e * ds));
	  *yv++ = x * (gv + (e * dv));
	  *yp++ = x * (gp + (e * dp));
	}

      } else {

	for (; 0 < n; n--) {	/* CPY */
	  const float x = (float) (*xp++);
	  *ys++ = x * gs;
	  *yv++ = x * gv;
	  *yp++ = x * gp;
	}

      }
    }
  } /* while there are core instructions */

	/* ****************************************************************
	 *      M I X D O W N
	 * ****************************************************************/

  /*
   * The percussion, sweel and scanner buffers are now written.
   */

  /* If anything is routed through the scanner, apply FX and get outbuffer */

  if (t->oldRouting & RT_VIB) {
#if 1
    vibratoProc (&t->inst_vibrato, vibBuffer, vibYBuffr, BUFFER_SIZE_SAMPLES);
#else
    size_t ii;
    for (ii=0;ii< BUFFER_SIZE_SAMPLES;++ii) vibYBuffr[ii]=0.0;
#endif

  }

  /* Mix buffers, applying percussion and swell pedal. */

  {
    const float * xp = swlBuffer;
    const float * vp = vibYBuffr;
    const float * pp = prcBuffer;


    if (t->oldRouting & RT_PERC) {	/* If percussion is on */
#ifdef HIPASS_PERCUSSION
      float * tp = &(prcBuffer[BUFFER_SIZE_SAMPLES - 1]);
      float temp = *tp;
      pp = tp - 1;
      for (i = 1; i < BUFFER_SIZE_SAMPLES; i++) {
	*tp = *pp - *tp;
	tp--;
	pp--;
      }
      *tp = t->pz - *tp;
      t->pz = temp;
      pp = prcBuffer;
#endif /* HIPASS_PERCUSSION */
      t->outputGain = t->swellPedalGain * t->percDrawbarGain;
      if (t->oldRouting & RT_VIB) { /* If vibrato is on */
	for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) { /* Perc and vibrato */
	  *yptr++ =
	    (t->outputGain * KEYCOMPLEVEL *
	     ((*xp++) + (*vp++) + ((*pp++) * t->percEnvGain)));
	  t->percEnvGain *= t->percEnvGainDecay;
	  KEYCOMPCHASE();
	}
      } else {			/* Percussion only */
	for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
	  *yptr++ =
	    (t->outputGain * KEYCOMPLEVEL * ((*xp++) + ((*pp++) * t->percEnvGain)));
	  t->percEnvGain *= t->percEnvGainDecay;
	  KEYCOMPCHASE();
	}
      }

    } else if (t->oldRouting & RT_VIB) { /* No percussion and vibrato */

      for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
	*yptr++ =
	  (t->swellPedalGain * KEYCOMPLEVEL * ((*xp++) + (*vp++)));
	KEYCOMPCHASE();
      }
    } else {			/* No percussion and no vibrato */
      for (i = 0; i < BUFFER_SIZE_SAMPLES; i++) {
	*yptr++ =
	  (t->swellPedalGain * KEYCOMPLEVEL * (*xp++));
	KEYCOMPCHASE();
      }
    }
  }

  if (t->upperKeyCount == 0) {
    t->percEnvGain = t->percEnvGainReset;
  }
} /* oscGenerateFragment */


struct b_tonegen *allocTonegen() {
  struct b_tonegen *t = (struct b_tonegen*) calloc(1, sizeof(struct b_tonegen));
  if (!t) return NULL;
  initValues(t);
  resetVibrato(t);
  return (t);
}

#else
# include "cfgParser.h"
# include "tonegen.h"
#endif /* END CONFIGDOCONLY */

#define STRINGEXPAND(x) #x
#define STRINGIFY(x) STRINGEXPAND(x)

static const ConfigDoc doc[] = {
  {"osc.tuning",                       CFG_DOUBLE,  "440.0", "Base tuning of the organ.", "Hz", 220.0, 880.0, .5},
  {"osc.temperament",                  CFG_TEXT,    "\"gear60\"", "Tuning temperament, gear-ratios/motor-speed. One of: \"equal\", \"gear60\", \"gear50\"", "", 0, 2, 1},
  {"osc.x-precision",                  CFG_DOUBLE,  "0.001", "Wave precision. Maximum allowed error when calculating wave buffer-length for a given frequency (ideal #of samples - discrete #of samples)", INCOMPLETE_DOC},
  {"osc.perc.fast",                    CFG_DOUBLE,  "1.0", "Fast percussion decay time", "s", 0, 10.0, 0.1},
  {"osc.perc.slow",                    CFG_DOUBLE,  "4.0", "Slow percussion decay time", "s", 0, 10.0, 0.1},
  {"osc.perc.normal",                  CFG_DECIBEL, "1.0", "Percussion starting gain of the envelope for normal volume.", "dB", 0, 1, 2.0},
  {"osc.perc.soft",                    CFG_DECIBEL, "0.5012", "Percussion starting gain of the envelope for soft volume.", "dB", 0, .89125, 2.0}, /* range [0..1[ (less than 1.0) */
#ifdef HIPASS_PERCUSSION
  {"osc.perc.gain",                    CFG_DOUBLE,  "11.0", "Basic volume of the percussion signal, applies to both normal and soft", "", 0, 22.0, .5},
#else
  {"osc.perc.gain",                    CFG_DOUBLE,  "3.0", "Basic volume of the percussion signal, applies to both normal and soft", "", 0, 22.0, .5},
#endif
  {"osc.perc.bus.a",                   CFG_INT,     "3", "range [0..8]", INCOMPLETE_DOC},
  {"osc.perc.bus.b",                   CFG_INT,     "4", "range [0..8]", INCOMPLETE_DOC},
  {"osc.perc.bus.trig",                CFG_INT,     "8", "range [-1..8]", INCOMPLETE_DOC},
  {"osc.eq.macro",                     CFG_TEXT,    "\"chspline\"", "one of \"chspline\", \"peak24\", \"peak46\"", INCOMPLETE_DOC},
  {"osc.eq.p1y",                       CFG_DOUBLE,  "1.0", "EQ spline parameter", INCOMPLETE_DOC},
  {"osc.eq.r1y",                       CFG_DOUBLE,  "0.0", "EQ spline parameter", INCOMPLETE_DOC},
  {"osc.eq.p4y",                       CFG_DOUBLE,  "1.0", "EQ spline parameter", INCOMPLETE_DOC},
  {"osc.eq.r4y",                       CFG_DOUBLE,  "0.0", "EQ spline parameter", INCOMPLETE_DOC},
  {"osc.eqv.ceiling",                  CFG_DOUBLE,  "1.0", "Normalize EQ parameters.", INCOMPLETE_DOC},
  {"osc.eqv.<oscnum>",                 CFG_DOUBLE,  "-", "oscnum=[0..127], value: [0..osc.eqv.ceiling]; default values are calculated depending on selected osc.eq.macro and tone-generator-model.", INCOMPLETE_DOC},
  {"osc.harmonic.<h>",                 CFG_DOUBLE,  "-", "speficy level of given harmonic number.", INCOMPLETE_DOC},
  {"osc.harmonic.w<w>.f<h>",           CFG_DOUBLE,  "-", "w: number of wheel [0..91], h: harmonic number", INCOMPLETE_DOC},
  {"osc.terminal.t<t>.w<w>",           CFG_DOUBLE,  "-", "t,w: wheel-number [0..91]", INCOMPLETE_DOC},
  {"osc.taper.k<key>.b<bus>.t<wheel>", CFG_DOUBLE,  "-", "customize tapering model. Specify level of [key, drawbar, tonewheel].", INCOMPLETE_DOC},
  {"osc.crosstalk.k<key>",             CFG_TEXT,    "-", "value colon-separated: \"<int:bus>:<int:wheel>:<double:level>\"", INCOMPLETE_DOC},
  {"osc.compartment-crosstalk",        CFG_DECIBEL, "0.01", "Crosstalk between tonewheels in the same compartment. The value refers to the amount of rogue signal picked up.", "dB", 0, 0.5, 2.0}, /* actual range 0..1 */
  {"osc.transformer-crosstalk",        CFG_DECIBEL, "0",    "Crosstalk between transformers on the top of the tg.", "dB", 0, 0.5, 2.0}, /* actual range 0..1 */
  {"osc.terminalstrip-crosstalk",      CFG_DECIBEL, "0.01", "Crosstalk between connection on the terminal strip.", "dB", 0, 0.5, 2.0}, /* actual range 0..1 */
  {"osc.wiring-crosstalk",             CFG_DECIBEL, "0.01", "Throttle on the crosstalk distribution model for wiring", "dB", 0, 0.5, 2.0}, /* actual range 0..1 */
  {"osc.contribution-floor",           CFG_DECIBEL, "0.0000158", "Signals weaker than this are not put on the contribution list", "dB", 0, .001, 2.00}, /* actual range 0..1 */
  {"osc.contribution-min",             CFG_DECIBEL, "0", "If non-zero, contributing signals have at least this level", "dB", 0, .001, 2.0}, /* actual range 0..1 */
  {"osc.attack.click.level",           CFG_DOUBLE,  "0.5", "Amount of random attenuation applied to a closing bus-oscillator connection.", "%", 0, 1, .02},
  {"osc.attack.click.maxlength",       CFG_DOUBLE,  "0.6250", "The maximum length of a key-click noise burst, 100% corresponds to " STRINGIFY(BUFFER_SIZE_SAMPLES) " audio-samples", "%", 0, 1, 0.025},
  {"osc.attack.click.minlength",       CFG_DOUBLE,  "0.1250", "The minimum length of a key-click noise burst, 100% corresponds to " STRINGIFY(BUFFER_SIZE_SAMPLES) " audio-samples", "%", 0, 1, 0.025},
  {"osc.release.click.level",          CFG_DOUBLE,  "0.25",   "Amount of random attenuation applied to an opening bus-oscillator", "%", 0, 1, 0.02},
  {"osc.release.model",                CFG_TEXT,    "\"linear\"", "Model applied during key-release, one of \"click\", \"cosine\", \"linear\", \"shelf\" ", "", 0, 3, 1},
  {"osc.attack.model",                 CFG_TEXT,    "\"click\"",  "Model applied during key-attack; one of \"click\", \"cosine\", \"linear\", \"shelf\" ",  "", 0, 3, 1},
  DOC_SENTINEL
};

const ConfigDoc *oscDoc () {
  return doc;
}
