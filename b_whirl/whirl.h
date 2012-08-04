/* setBfree - DSP tonewheel organ
 *
 * Copyright (C) 2003-2004 Fredrik Kilander <fk@dsv.su.se>
 * Copyright (C) 2008-2012 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2010 Ken Restivo <ken@restivo.org>
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

/* whirl.h */
#ifndef WHIRL_H
#define WHIRL_H

#include "../src/cfgParser.h" // ConfigContext
#include "../src/midi.h" // useMIDIControlFunction

#define WHIRL_DISPLC_SIZE ((unsigned int) (1 << 11))
#define WHIRL_DISPLC_MASK ((WHIRL_DISPLC_SIZE) - 1)

#define WHIRL_BUF_SIZE_SAMPLES ((unsigned int) (1 << 11))
#define WHIRL_BUF_MASK_SAMPLES (WHIRL_BUF_SIZE_SAMPLES - 1)

typedef enum {a0, a1, a2, b0, b1, b2, z0, z1} filterCoeff;

typedef struct _revcontrol {
  double hornTarget;
  double drumTarget;
} RevControl;

struct b_whirl {

  double SampleRateD;
  int bypass;        ///< if set to 1 completely bypass this effect
  double hnBreakPos; ///< where to stop horn - 0: free, 1.0: front-center, ]0..1] clockwise circle */
  double drBreakPos; ///< where to stop drum

/*
 * Forward (clockwise) displacement table for writing positions.
 */
  float hnFwdDispl[WHIRL_DISPLC_SIZE]; /* Horn */
  float drFwdDispl[WHIRL_DISPLC_SIZE]; /* Drum */

/*
 * Backward (counter-clockwise) displacement table.
 */
  float hnBwdDispl[WHIRL_DISPLC_SIZE]; /* Horn */
  float drBwdDispl[WHIRL_DISPLC_SIZE]; /* Drum */

  float bfw[WHIRL_DISPLC_SIZE][5];
  float bbw[WHIRL_DISPLC_SIZE][5];

#define AGBUF 512
#define AGMASK (AGBUF-1)

  float adx0[AGBUF];
  float adx1[AGBUF];
  float adx2[AGBUF];
  int   adi0;
  int   adi1;
  int   adi2;


/*
 * Writing positions (actually, indexes into hnFwdDispl[]):
 *                Left  Right
 * Primary           0      1
 * First reflec.     2      3
 * Second refl.      4      5
 */

  int hornPhase[6];

  int drumPhase[6];

/* The current angle of rotating elements */

  double hornAngleGRD;  /* 0..1 */
  double drumAngleGRD;

/* target speed - rotational frequency */
  float hornRPMslow;
  float hornRPMfast;
  float drumRPMslow;
  float drumRPMfast;

/* time constants [s] -- first order differential */
  float hornAcc;
  float hornDec;
  float drumAcc;
  float drumDec;

#define revSelectEnd (4)
  RevControl revoptions[9];
  int revselects[revSelectEnd];
  int revSelect;

  int hornAcDc;
  int drumAcDc;

  double hornIncrUI; ///< current angular speed - unit: radians / sample / (2*M_PI)
  double drumIncrUI; ///< current angular speed - unit: radians / sample / (2*M_PI)

  double hornTarget; ///< target angular speed  - unit: radians / sample / (2*M_PI)
  double drumTarget; ///< target angular speed  - unit: radians / sample / (2*M_PI)

/*
 * Spacing between reflections in samples. The first can't be zero, since
 * we must allow for the swing of the extent to wander close to the reader.
 */

  float hornSpacing[6];
  float hornRadiusCm; /* 17.0; 25-nov-04 */
  float drumRadiusCm;

  float airSpeed;	/* Meters per second */
  float micDistCm;	/* From mic to origin */
  float drumSpacing[6];

/* Delay buffers */

  float HLbuf[WHIRL_BUF_SIZE_SAMPLES]; /* Horn left buffer */
  float HRbuf[WHIRL_BUF_SIZE_SAMPLES]; /* Horn right buffer */
  float DLbuf[WHIRL_BUF_SIZE_SAMPLES]; /* Drum left buffer */
  float DRbuf[WHIRL_BUF_SIZE_SAMPLES]; /* Drum right buffer */

/* Single read position, incremented by one, always. */

  unsigned int outpos;
  float z[4];

  float drfL[8];/* Drum filter */
  float drfR[8];/* Drum filter */
  int    lpT;	/* high shelf */
  double lpF;	/* Frequency */
  double lpQ;	/* Q, bandwidth */
  double lpG;	/* Gain */

  float hafw[8];		/* Horn filter a */
  float haT; /* low pass */
  float haF; /* 3900.0; 25-nov-04 */
  float haQ; /*   1.4; 25-nov-04 */
  float haG; /*   0.0; 25-nov-04 */

  float hbfw[8];
  float hbT;		/* low shelf */
  float hbF;
  float hbQ; /* 2.0; 25-nov-04 */
  float hbG; /* -60.0; 25-nov-04 */ /* negative gain */

#ifdef HORN_COMB_FILTER
#define COMB_SIZE ((unsigned int) (1 << 10))
#define COMB_MASK (COMB_SIZE - 1)

  float comb0[COMB_SIZE];
  float cb0fb;
  int   cb0dl;
  float * cb0wp;		/* Write pointer */
  float * cb0rp;		/* Read pointer */
  float * cb0bp;		/* Begin pointer */
  float * cb0es;		/* End sentinel */

  float comb1[COMB_SIZE];
  float cb1fb;
  int   cb1dl;
  float * cb1wp;
  float * cb1rp;
  float * cb1bp;
  float * cb1es;
#else /* allow to parse config files which include these values */
  float cb0fb;
  int   cb0dl;
  float cb1fb;
  int   cb1dl;
#endif

  float hornLevel;
  float leakLevel;
  float leakage;

};

extern struct b_whirl *allocWhirl();
extern void freeWhirl(struct b_whirl *w);
extern int whirlConfig (struct b_whirl *w, ConfigContext * cfg);
extern const ConfigDoc *whirlDoc ();

extern void initWhirl (struct b_whirl *w, double rate);

extern void whirlProc (struct b_whirl *w,
		       const float * inbuffer,
		       float * outbL,
		       float * outbR,
		       size_t bufferLengthSamples);

extern void whirlProc2 (struct b_whirl *w,
			const float * inbuffer,
		        float * outL, float * outR,
		        float * outHL, float * outHR,
		        float * outDL, float * outDR,
		        size_t bufferLengthSamples);

#define WHIRL_FAST 2
#define WHIRL_SLOW 1
#define WHIRL_STOP 0

extern void setRevSelect (struct b_whirl *w, int n);
extern void useRevOption (struct b_whirl *w, int n);
extern void isetHornFilterAType (struct b_whirl *w, int v);
extern void fsetHornFilterAFrequency (struct b_whirl *w, float v);
extern void fsetHornFilterAQ (struct b_whirl *w, float v);
extern void fsetHornFilterAGain (struct b_whirl *w, float v);
extern void isetHornFilterBType (struct b_whirl *w, int v);
extern void fsetHornFilterBFrequency (struct b_whirl *w, float v);
extern void fsetHornFilterBQ (struct b_whirl *w, float v);
extern void fsetHornFilterBGain (struct b_whirl *w, float v);
#endif /* WHIRL_H */
/* vi:set ts=8 sts=2 sw=2: */
