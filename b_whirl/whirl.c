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

/* whirl.c
 * Based on the scanner code, another Leslie experiment based on multiple
 * writing pointers.
 *
 * To do, in no particular order:
 * +Drum
 * +Filters
 * +Resonance (for horn)
 * Resonance for drum compartment
 * Fine-tuning
 * +Speed switching
 * +External controls
 * +Configuration interface
 * +De-interpolation noise (sample width)
 *
 * 12-sep-2004/FK Inserted two all-pass filters at the composite output
 *                hoping to de-correlate the channels; inconclusive results.
 *                See ifdef EXIT_FILTER.
 *
 * 23-aug-2004/FK (Re)introduced MIDI control of horn filter A's frequency.
 *                I needed to hear the difference in realtime. There is a
 *                distinct resonance at 3900 Hz (current default) but some
 *                extra brightness at 6000 Hz. Since it can also be set from
 *                config I will have to continue testing in rehearsal.
 * 21-apr-2004/FK Interpolating with a sample width of 1 sounds crappy,
 *                mostly because the lower frequencies introduce noise.
 *                However, by placing the low-pass filter at the drum
 *                output, much of the noise is reduced to an acceptable
 *                level.
 * 17-apr-2004/FK Sounds like a chorus.
 * 15-apr-2004/FK First version
 * 14-apr-2004/FK Opened file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "eqcomp.h"
#include "whirl.h"

extern double SampleRateD;

#define DISPLC_SIZE ((unsigned int) (1 << 11))
/* The DISPLC_MASK is a fixed point mask */
#define DISPLC_MASK ((DISPLC_SIZE << 16) - 1)

#define BUF_SIZE_SAMPLES ((unsigned int) (1 << 11))
#define BUF_MASK_SAMPLES (BUF_SIZE_SAMPLES - 1)

static int bypass = 0;
static double hnBreakPos = 0; /* where to stop horn - 0: free, 1.0: front-center, ]0..1] clockwise circle */
static double drBreakPos = 0; /* where to stop drum */

/*
 * Forward (clockwise) displacement table for writing positions.
 */

static float hnFwdDispl[DISPLC_SIZE]; /* Horn */
static float drFwdDispl[DISPLC_SIZE]; /* Drum */

/*
 * Backward (counter-clockwise) displacement table.
 */

static float hnBwdDispl[DISPLC_SIZE]; /* Horn */
static float drBwdDispl[DISPLC_SIZE]; /* Drum */

/* static float afw0[DISPLC_SIZE];	/\* Angle-dependent filtering *\/ */
/* static float afw1[DISPLC_SIZE];	/\* Forward *\/ */

/* static float abw0[DISPLC_SIZE];	/\* Angle-dependent filtering *\/ */
/* static float abw1[DISPLC_SIZE]; */

static float bfw[DISPLC_SIZE][5];
static float bbw[DISPLC_SIZE][5];

#define AGBUF 512
#define AGMASK (AGBUF-1)

static float adx0[AGBUF];
static float adx1[AGBUF];
static float adx2[AGBUF];
static int   adi0;
static int   adi1;
static int   adi2;

static double ipx;
static double ipy;


/*
 * Writing positions (actually, indexes into hnFwdDispl[]):
 *                Left  Right
 * Primary           0      1
 * First reflec.     2      3
 * Second refl.      4      5
 */

static int hornPhase[6];

static int drumPhase[6];

/* The current angle of rotating elements */

static int hornAngle = 0;
static int drumAngle = 0;


/* Added to angle. */
#define HI_OFFSET 0.08
#define LO_OFFSET 0.06
static int hornFixedOff = 0;
static int drumFixedOff = 0;

/* http://www.dairiki.org/HammondWiki/LeslieRotationSpeed */
static float hornRPMslow = 48.0;
static float hornRPMfast = 400.0;
static float drumRPMslow = 40.0;
static float drumRPMfast = 342.0;

static float hornAcc = 350.0; /* RPM per second */
static float hornDec = 448.0;
static float drumAcc = 120.0;
static float drumDec = 102.0;

typedef struct _revcontrol {
  int hornTarget;
  int drumTarget;
} RevControl;

static RevControl revoptions [9];
static int revselects[9];
static size_t revSelectEnd;
static int revSelect = 0;

static int hornAcDc = 0;
static int drumAcDc = 0;

static int hornIncrUI = 0; ///< current speed - unit: DISPLC_SIZE read increments
static int drumIncrUI = 0; ///< current speed - unit: DISPLC_SIZE read increments

static double hornAII = 0; ///< current horn acceleration / deceleration
static double drumAII = 0; ///< current drum acceleration / deceleration

static int hornTarget = 0; ///< target speed - unit: DISPLC_SIZE read increments
static int drumTarget = 0; ///< target speed - unit: DISPLC_SIZE read increments

/*
 * Spacing between reflections in samples. The first can't be zero, since
 * we must allow for the swing of the extent to wander close to the reader.
 */

static float hornSpacing[6] = {
  12.0,				/* Primary */
  18.0,
  53.0,				/* First reflection */
  50.0,
  106.0,			/* Secondary reflection */
  116.0
};

static float hornRadiusCm = 19.2; /* 17.0; 25-nov-04 */
static float drumRadiusCm = 22.0;

static const float airSpeed = 340.0;	/* Meters per second */

static float micDistCm = 42.0;	/* From mic to origin */

static float drumSpacing[6] = {
  36.0, 39.0,
  79.0, 86.0,
  123.0, 116.0
};

/* Delay buffers */

static float HLbuf[BUF_SIZE_SAMPLES]; /* Horn left buffer */
static float HRbuf[BUF_SIZE_SAMPLES]; /* Horn right buffer */
static float DLbuf[BUF_SIZE_SAMPLES]; /* Drum left buffer */
static float DRbuf[BUF_SIZE_SAMPLES]; /* Drum right buffer */

/* Single read position, incremented by one, always. */

static unsigned int outpos = 0;

typedef enum {a0, a1, a2, b0, b1, b2, z0, z1} filterCoeff;

static float drfw[8];		/* Drum filter */
static int    lpT = 8;          /* high shelf */
static double lpF = 811.9695;	/* Frequency */
static double lpQ =   1.6016;	/* Q, bandwidth */
static double lpG = -38.9291;	/* Gain */

static float hafw[8];		/* Horn filter a */
static float haT = 0;		/* low pass */
static float haF = 4500;  /* 3900.0; 25-nov-04 */
static float haQ = 2.7456; /*   1.4; 25-nov-04 */
static float haG = -30.0;  /*   0.0; 25-nov-04 */

static float hbfw[8];
static float hbT = 7;		/* low shelf */
static float hbF = 300.0;
static float hbQ =   1.0; /* 2.0; 25-nov-04 */
static float hbG = -30.0; /* -60.0; 25-nov-04 */ /* negative gain */

#define COMB_SIZE ((unsigned int) (1 << 10))
#define COMB_MASK (COMB_SIZE - 1)

static float comb0[COMB_SIZE];
static float cb0fb = -0.55;
static int   cb0dl = 38;
static float * cb0wp;		/* Write pointer */
static float * cb0rp;		/* Read pointer */
static float * cb0bp;		/* Begin pointer */
static float * cb0es;		/* End sentinel */

static float comb1[COMB_SIZE];
static float cb1fb = -0.3508;
static int   cb1dl = 120;
static float * cb1wp;
static float * cb1rp;
static float * cb1bp;
static float * cb1es;

static float hornLevel = 0.7;
static float leakLevel = 0.15;
static float leakage = 0;

static int fixptd (double u) {
  //assert (u < (1<<16));
  int rv = (int) (u*(1<<16));
  return rv<1 ? 1 : rv;
}

/*
 * Returns the increment needed to traverse a certain table the
 * requested number of revolutions per minute.
 * @param tblSz  The number of slots in the table.
 * @param rpm    Revolutions per minute.
 */
static double getTableInc (size_t tblSz, double rpm) {
  return (rpm * (double) tblSz) / (SampleRateD * 60.0);
}

static int ui_increment (double rpm) {
  return fixptd (getTableInc ((size_t) DISPLC_SIZE, rpm));
}


/*
 *
 */
static void setIIRFilter (float W[],
			  int T,
			  const double F,
			  const double Q,
			  const double G) {
  double C[6];
  eqCompute (T, F, Q, G, C);
  W[a1] = C[EQC_A1];
  W[a2] = C[EQC_A2];
  W[b0] = C[EQC_B0];
  W[b1] = C[EQC_B1];
  W[b2] = C[EQC_B2];
}

/*
 * Sets the revolution selection.
 */
void setRevSelect (int n) {
  int i;

  revSelect = n % revSelectEnd;
  i = revselects[revSelect];
  useRevOption(i);
}

void useRevOption (int n) {
  int i = n % 9;

#if 0 // not in RT callback
  printf ("\rREV:%d %d ", n, i); fflush (stdout);
#endif

  hornTarget = revoptions[i].hornTarget;
  drumTarget = revoptions[i].drumTarget;

  if (hornIncrUI < hornTarget) {
    hornAII = getTableInc ((size_t) DISPLC_SIZE, hornAcc / SampleRateD);
    hornAcDc = 1;
  }
  else if (hornTarget < hornIncrUI) {
    hornAII = getTableInc ((size_t) DISPLC_SIZE, hornDec / SampleRateD);
    hornAcDc = -1;
  }
  if (drumIncrUI < drumTarget) {
    drumAII = getTableInc ((size_t) DISPLC_SIZE, drumAcc / SampleRateD);
    drumAcDc = 1;
  }
  else if (drumTarget < drumIncrUI) {
    drumAII = getTableInc ((size_t) DISPLC_SIZE, drumDec / SampleRateD);
    drumAcDc = -1;
  }

}

void advanceRevSelect () {
  setRevSelect (revSelect + 1);
}

static void revControlAll (unsigned char u) {
  useRevOption ((int) (u / 15)); // 0..8
}

static void revControl (unsigned char u) {
  setRevSelect ((int) (u / 32)); // 3 modes only - stop, slow, fast, stop
}

/* 11-may-2004/FK Hack to make the pedal work. See midiIn.c */
void setWhirlSustainPedal (unsigned char u) {
  if (u) {
    setRevSelect ((revSelect == 1) ? 2 : 1);
  }
}

static void setRevOption (int i,
			  unsigned int hnTgt,
			  unsigned int drTgt) {
  revoptions[i].hornTarget = hnTgt;
  revoptions[i].drumTarget = drTgt;
}

static void computeRotationSpeeds () {
  const int hfast = ui_increment (hornRPMfast);
  const int hslow = ui_increment (hornRPMslow);
  const int hstop = 0;
  const int dfast = ui_increment (drumRPMfast);
  const int dslow = ui_increment (drumRPMslow);
  const int dstop = 0;

  setRevOption (8, hfast, dfast);
  setRevOption (7, hfast, dslow);
  setRevOption (6, hfast, dstop);
  setRevOption (5, hslow, dfast);
  setRevOption (4, hslow, dslow);
  setRevOption (3, hslow, dstop);
  setRevOption (2, hstop, dfast);
  setRevOption (1, hstop, dslow);
  setRevOption (0, hstop, dstop);

  revselects[0] = 0;		/* stop */
  revselects[1] = 4;		/* both slow */
  revselects[2] = 8;		/* both fast */
  revselects[3] = 4;		/* both slow */
  revSelectEnd = 4;		/* How may entries in revselects[] */
  setRevSelect(revSelect);
}

/*
 *
 */
static void ipolmark (double degrees, double level) {
  ipx = degrees;
  ipy = level;
}

/*
 *
 */
static void ipoldraw (double degrees, double level, int partial) {
  double d;
  double e;
  double range;
  int fromIndex;
  int toIndex;
  int i;

  d = ipx;
  while (d < 0.0) d += 360.0;
  fromIndex = (int) ((d * (double) DISPLC_SIZE) / 360.0);

  ipx = degrees;

  e = ipx;
  while (e < d) e += 360.0;
  toIndex = (int) ((e * (double) DISPLC_SIZE) / 360.0);

  range = (double) (toIndex - fromIndex);
  for (i = fromIndex; i <= toIndex; i++) {
    double x = (double) (i - fromIndex);
    double w = ipy + ((x / range) * (level - ipy));
    bfw[i & (DISPLC_SIZE - 1)][partial] = (float) w;
  }

  ipy = level;
} /* ipoldraw */

/*
 *
 */
static void initTables () {
  int i;
  int j;
  double sum;
  double hornRadiusSamples = (hornRadiusCm * SampleRateD/100.0) / airSpeed;
  double drumRadiusSamples = (drumRadiusCm * SampleRateD/100.0) / airSpeed;
  double micDistSamples    = (micDistCm    * SampleRateD/100.0) / airSpeed;

  for (i = 0; i < DISPLC_SIZE; i++) {
    /* Compute angle around the circle */
    double v = (2.0 * M_PI * (double) i) / (double) DISPLC_SIZE;
    /* Distance between the mic and the rotor korda */
    double a = micDistSamples - (hornRadiusSamples * cos (v));
    /* Distance between rotor and mic-origin line */
    double b = hornRadiusSamples * sin (v);

    hnFwdDispl[i] = sqrt ((a * a) + (b * b));
    hnBwdDispl[DISPLC_SIZE - (i + 1)] = hnFwdDispl[i];

    a = micDistSamples - (drumRadiusSamples * cos (v));
    b = drumRadiusSamples * sin (v);
    drFwdDispl[i] = sqrt ((a * a) + (b * b));
    drBwdDispl[DISPLC_SIZE - (i + 1)] = drFwdDispl[i];

    a = (1.0 + cos (v)) / 2.0;	/* Goes from 1, 0.5, 0, 0.5, 1 */
#ifdef COMMENT
    afw0[i] = 0.5 * (1.0 + a);
    afw1[i] = 1.0 - afw0[i];

#if 1
    afw0[i] *= 0.33321 + (0.33321 * a);
    afw1[i] *= 0.33321 * (1.0 - a);
#endif

    abw0[DISPLC_SIZE - (i + 1)] = afw0[i];
    abw1[DISPLC_SIZE - (i + 1)] = afw1[i];
#endif /* COMMENT */
  }

  hornPhase[0] = 0;
  hornPhase[1] = DISPLC_MASK >> 1;

  hornPhase[2] = ((DISPLC_SIZE * 2) / 6) << 16;
  hornPhase[3] = ((DISPLC_SIZE * 5) / 6) << 16;

  hornPhase[4] = ((DISPLC_SIZE * 1) / 6) << 16;
  hornPhase[5] = ((DISPLC_SIZE * 4) / 6) << 16;

  for (i = 0; i < 6; i++) {
    hornSpacing[i] += hornRadiusSamples + 1.0;
  }

  drumPhase[0] = 0;
  drumPhase[1] = DISPLC_MASK >> 1;

  drumPhase[2] = ((DISPLC_SIZE * 2) / 6) << 16;
  drumPhase[3] = ((DISPLC_SIZE * 5) / 6) << 16;

  drumPhase[4] = ((DISPLC_SIZE * 1) / 6) << 16;
  drumPhase[5] = ((DISPLC_SIZE * 4) / 6) << 16;

  for (i = 0; i < 6; i++) {
    drumSpacing[i] += drumRadiusSamples + 1.0;
  }

  setIIRFilter (drfw, lpT, lpF, lpQ, lpG);
  setIIRFilter (hafw, haT, haF, haQ, haG);
  setIIRFilter (hbfw, hbT, hbF, hbQ, hbG);

  cb0rp = &(comb0[COMB_SIZE - cb0dl]);
  cb0wp = &(comb0[0]);
  cb0bp = &(comb0[0]);
  cb0es = &(comb0[COMB_SIZE]);

  cb1rp = &(comb1[COMB_SIZE - cb1dl]);
  cb1wp = &(comb1[0]);
  cb1bp = &(comb1[0]);
  cb1es = &(comb1[COMB_SIZE]);

  /* Horn angle-dependent impulse response coefficents. */
  /* These were derived from 'Doppler simulation and the leslie',
   * Julius Smith, Stefania Serafin, Jonathan Abel, David Berners,
   * Proc. of the 5th Conference on Digital Audio Effects (DAFx-02),
   * Hamburg, Germany, September 26-28, 2002.
   * In this article figure 8 depicts the 'First 5 principal components
   * weighted by their corresponding singular values'. I have been unable
   * to clearly understand what this means, but anyway ploughed ahead
   * interpreted the components as impulse response coefficients. The
   * figure plots the five components as horizontal wavy lines, where
   * the x axis is the angle and the y axis is relative to each component.
   * So, the following code consists of 'drawing' instructions that simply
   * 'paints' a copy of each line as interpreted from the figure, from
   * left (-180 degrees) to right (180 degrees). The points between the
   * given coordinates are linearly interpolated and inserted into the
   * bfw matrix. Then (below) we normalise the lot to unit gain.
   */
#if 0 // Fredrik
  ipolmark (-180.0, 1.10);
  ipoldraw (-166.3, 0.85, 0);
  ipoldraw (-150.0, 0.85, 0);
  ipoldraw (-134.8, 0.85, 0);
  ipoldraw (-123.6, 0.78, 0);
  ipoldraw (-105.6, 0.78, 0);
  ipoldraw (-100.0, 0.78, 0);
  ipoldraw ( -78.6, 0.85, 0);
  ipoldraw ( -60.7, 0.81, 0);
  ipoldraw ( -50.0, 0.89, 0);
  ipoldraw ( -44.7, 0.90, 0);
  ipoldraw ( -29.2, 1.30, 0);
  ipoldraw (   0.0, 2.79, 0);
  ipoldraw (  15.7, 2.30, 0);
  ipoldraw (  31.5, 1.50, 0);
  ipoldraw (  44.9, 0.90, 0);
  ipoldraw (  50.0, 0.88, 0);
  ipoldraw (  60.7, 0.80, 0);
  ipoldraw (  74.2, 0.84, 0);
  ipoldraw ( 100.0, 0.75, 0);
  ipoldraw ( 121.3, 0.80, 0);
  ipoldraw ( 150.0, 0.87, 0);
  ipoldraw ( 166.3, 0.83, 0);
  ipoldraw ( 180.0, 1.10, 0);
#else // Robin
  ipolmark (-180.0, 1.036);
  ipoldraw (-166.4,  .881, 0);
  ipoldraw (-150.5,  .881, 0);
  ipoldraw (-135.3,  .881, 0);
  ipoldraw (-122.4,  .792, 0);
  ipoldraw (-106.5,  .792, 0);
  ipoldraw ( -91.2,  .836, 0);
  ipoldraw ( -75.8,  .881, 0);
  ipoldraw ( -59.4,  .851, 0);
  ipoldraw ( -44.7,  .941, 0);
  ipoldraw ( -30.0, 1.298, 0);
  ipoldraw ( -14.7, 2.119, 0);
  ipoldraw (   0.0, 2.820, 0);
  ipoldraw (  15.6, 2.313, 0);
  ipoldraw (  30.0, 1.492, 0);
  ipoldraw (  44.7,  .926, 0);
  ipoldraw (  60.0,  .836, 0);
  ipoldraw (  74.7,  .866, 0);
  ipoldraw (  90.6,  .792, 0);
  ipoldraw ( 100.0,  .777, 0);
  ipoldraw ( 105.0,  .777, 0);
  ipoldraw ( 120.0,  .836, 0);
  ipoldraw ( 135.3,  .836, 0);
  ipoldraw ( 150.0,  .881, 0);
  ipoldraw ( 166.5,  .866, 0);
  ipoldraw ( 180.0, 1.036, 0);
#endif

  ipolmark (-180.0, -0.10);
  ipoldraw (-150.0,  0.10, 1);
  ipoldraw (-137.0, -0.10, 1);
  ipoldraw (-118.0,  0.18, 1);
  ipoldraw (-100.0,  0.20, 1);
  ipoldraw ( -87.0,  0.40, 1);
  ipoldraw ( -75.0,  0.30, 1);
  ipoldraw ( -44.5,  0.70, 1);
  ipoldraw ( -30.0,  0.45, 1);
  ipoldraw (   3.1, -0.75, 1);
  ipoldraw (  15.6, -0.51, 1);
  ipoldraw (  35.7,  0.45, 1);
  ipoldraw (  44.6,  0.67, 1);
  ipoldraw (  74.7,  0.20, 1);
  ipoldraw (  90.2,  0.31, 1);
  ipoldraw ( 107.0,  0.02, 1);
  ipoldraw ( 122.3,  0.15, 1);
  ipoldraw ( 135.3, -0.12, 1);
  ipoldraw ( 153.3,  0.08, 1);
  ipoldraw ( 180.0, -0.10, 1);

  ipolmark (-180.0,  0.40);
  ipoldraw (-165.0,  0.20, 2);
  ipoldraw (-150.0,  0.48, 2);
  ipoldraw (-121.2,  0.22, 2);
  ipoldraw ( -89.2,  0.30, 2);
  ipoldraw ( -69.2,  0.22, 2);
  ipoldraw ( -58.0,  0.11, 2);
  ipoldraw ( -40.2, -0.43, 2);
  ipoldraw ( -29.0, -0.58, 2);
  ipoldraw ( -15.6, -0.49, 2);
  ipoldraw (   0.0,  0.00, 2);
  ipoldraw (  17.8, -0.60, 2);
  ipoldraw (  75.9,  0.35, 2);
  ipoldraw (  91.5,  0.28, 2);
  ipoldraw ( 104.9,  0.32, 2);
  ipoldraw ( 122.7,  0.22, 2);
  ipoldraw ( 150.0,  0.45, 2);
  ipoldraw ( 167.0,  0.20, 2);
  ipoldraw ( 180.0,  0.40, 2);

  ipolmark (-180.0, -0.15);
  ipoldraw (-165.2, -0.20, 3);
  ipoldraw (-150.0,  0.00, 3);
  ipoldraw (-133.9, -0.20, 3);
  ipoldraw (-106.0,  0.09, 3);
  ipoldraw ( -89.3, -0.20, 3);
  ipoldraw ( -76.3,  0.00, 3);
  ipoldraw ( -60.3,  0.34, 3);
  ipoldraw ( -44.6,  0.00, 3);
  ipoldraw ( -15.6, -0.28, 3);
  ipoldraw (   0.0,  0.36, 3);
  ipoldraw (  14.5,  0.15, 3);
  ipoldraw (  20.1, -0.18, 3);
  ipoldraw (  44.6,  0.18, 3);
  ipoldraw (  60.4,  0.26, 3);
  ipoldraw (  75.9,  0.20, 3);
  ipoldraw (  90.4, -0.10, 3);
  ipoldraw ( 104.9,  0.10, 3);
  ipoldraw ( 122.8, -0.10, 3);
  ipoldraw ( 136.2, -0.10, 3);
  ipoldraw ( 150.0,  0.12, 3);
  ipoldraw ( 165.0, -0.25, 3);
  ipoldraw ( 180.0, -0.15, 3);

  ipolmark (-180.0,  0.20);
  ipoldraw (-165.2,  0.00, 4);
  ipoldraw (-150.0,  0.22, 4);
  ipoldraw (-136.2, -0.22, 4);
  ipoldraw (-120.5,  0.00, 4);
  ipoldraw (-100.0,  0.00, 4);
  ipoldraw ( -90.0,  0.10, 4);
  ipoldraw ( -74.8, -0.15, 4);
  ipoldraw ( -60.3, -0.20, 4);
  ipoldraw ( -44.6,  0.21, 4);
  ipoldraw ( -15.6,  0.00, 4);
  ipoldraw (   0.0,  0.30, 4);
  ipoldraw (  15.6, -0.21, 4);
  ipoldraw (  20.1, -0.11, 4);
  ipoldraw (  45.7,  0.18, 4);
  ipoldraw (  60.3, -0.10, 4);
  ipoldraw (  74.8, -0.20, 4);
  ipoldraw (  90.4, -0.05, 4);
  ipoldraw ( 104.9, -0.18, 4);
  ipoldraw ( 120.5,  0.00, 4);
  ipoldraw ( 136.2, -0.30, 4);
  ipoldraw ( 150.0,  0.20, 4);
  ipoldraw ( 165.0,  0.00, 4);
  ipoldraw ( 180.0,  0.20, 4);

  sum = 0.0;
  /* Compute the normalisation factor */
  for (i = 0; i < DISPLC_SIZE; i++) {
    double colsum = 0.0;
    for (j = 0; j < 5; j++) {
      colsum += fabs (bfw[i][j]);
    }
    if (sum < colsum) {
      sum = colsum;
    }
  }
  /* Apply normalisation */
  for (i = 0; i < DISPLC_SIZE; i++) {
    for (j = 0; j < 5; j++) {
      bfw[i][j] *= 1.0 / sum;
      bbw[DISPLC_SIZE - i - 1][j] = bfw[i][j];
    }
  }

} /* initTables */

/*
 * Displays the settings of a filter.
 */
static void displayFilter (char * id, int T, float F, float Q, float G) {
#if 0 // not in RT callback
  const char * type = eqGetTypeString (T);
  printf ("\n%s:T=%3.3s:F=%10.4f:Q=%10.4f:G=%10.4f", id, type, F, Q, G);
  fflush (stdout);
#endif
}

#define UPDATE_A_FILTER { \
  setIIRFilter (hafw, haT, haF, haQ, haG); \
  displayFilter ("Horn A", haT, haF, haQ, haG); \
}

#define UPDATE_B_FILTER { \
  setIIRFilter (hbfw, hbT, hbF, hbQ, hbG); \
  displayFilter ("Horn B", hbT, hbF, hbQ, hbG); \
}


/*
 * Sets the type of the A horn IIR filter.
 */
static void setHornFilterAType (unsigned char uc) {
  haT = (int) (uc / 15);
  UPDATE_A_FILTER;
}

void isetHornFilterAType (int v) {
  haT = (int) (v % 9);
  UPDATE_A_FILTER;
}

/*
 * Sets the cutoff frequency of the A horn IIR filter.
 */
static void setHornFilterAFrequency (unsigned char uc) {
  double u = (double) uc;
  double minv = 250.0;
  double maxv = 8000.0;
  haF = minv + ((maxv - minv) * ((u * u) / 16129.0));
  UPDATE_A_FILTER;
}

void fsetHornFilterAFrequency (float v) {
  if (v<250.0 || v> 8000.0) return;
  haF = v;
  UPDATE_A_FILTER;
}

/*
 * Sets the Q value of the A horn IIR filter.
 */
static void setHornFilterAQ (unsigned char uc) {
  double u = (double) uc;
  double minv = 0.01;
  double maxv = 6.00;
  haQ = minv + ((maxv - minv) * (u / 127.0));
  UPDATE_A_FILTER;
}

void fsetHornFilterAQ (float v) {
  if (v<0.01 || v> 6.0) return;
  haQ = v;
  UPDATE_A_FILTER;
}

/*
 * Sets the Gain value of the A horn IIR filter.
 */
static void setHornFilterAGain (unsigned char uc) {
  double u = (double) uc;
  double minv = -48.0;
  double maxv =  48.0;
  haG = minv + ((maxv - minv) * (u / 127.0));
  UPDATE_A_FILTER;
}

void fsetHornFilterAGain (float v) {
  if (v<-48.0 || v> 48.0) return;
  haG = v;
  UPDATE_A_FILTER;
}


static void setHornFilterBType (unsigned char uc) {
  hbT = (int) (uc / 15);
  UPDATE_B_FILTER;
}

void isetHornFilterBType (int v) {
  hbT = (int) (v % 9);
  UPDATE_B_FILTER;
}

static void setHornFilterBFrequency (unsigned char uc) {
  double u = (double) uc;
  double minv = 250.0;
  double maxv = 8000.0;
  hbF = minv + ((maxv - minv) * ((u * u) / 16129.0));
  UPDATE_B_FILTER;
}

void fsetHornFilterBFrequency (float v) {
  if (v<250.0 || v> 8000.0) return;
  hbF = v;
  UPDATE_B_FILTER;
}

static void setHornFilterBQ (unsigned char uc) {
  double u = (double) uc;
  double minv = 0.01;
  double maxv = 6.00;
  hbQ = minv + ((maxv - minv) * (u / 127.0));
  UPDATE_B_FILTER;
}

void fsetHornFilterBQ (float v) {
  if (v<0.01 || v> 6.0) return;
  hbQ = v;
  UPDATE_B_FILTER;
}

static void setHornFilterBGain (unsigned char uc) {
  double u = (double) uc;
  double minv = -48.0;
  double maxv =  48.0;
  hbG = minv + ((maxv - minv) * (u / 127.0));
  UPDATE_B_FILTER;
}

void fsetHornFilterBGain (float v) {
  if (v<-48.0 || v> 48.0) return;
  hbG = v;
  UPDATE_B_FILTER;
}

void setHornBreakPosition (unsigned char uc) {
  hnBreakPos = (double)uc/127.0;
}

void setDrumBreakPosition (unsigned char uc) {
  drBreakPos = (double)uc/127.0;
}

void setHornAcceleration (unsigned char uc) {
  hornAcc = 10.0 + (double)uc*3.4;
}

void setHornDeceleration (unsigned char uc) {
  hornDec = 8.0 + (double)uc*4.4;
}

void setDrumAcceleration (unsigned char uc) {
  drumAcc = 10.0 + (double)uc*1.75;
}

void setDrumDeceleration (unsigned char uc) {
  drumDec = 2.0 + (double)uc*1.6;
}

/*
 * This function initialises this module. It is run after whirlConfig.
 */
void initWhirl () {

  initTables ();
  computeRotationSpeeds();

  memset(HLbuf, 0, BUF_SIZE_SAMPLES);
  memset(HRbuf, 0, BUF_SIZE_SAMPLES);
  memset(DLbuf, 0, BUF_SIZE_SAMPLES);
  memset(DRbuf, 0, BUF_SIZE_SAMPLES);

  /* TODO change w/speed ? (was not done upstream).
   * in upstream, those were always
   * zero after the first leslie-tempo switch!
   */
  hornFixedOff = fixptd(HI_OFFSET);
  drumFixedOff = fixptd(LO_OFFSET);

  leakage = leakLevel * hornLevel;

  useMIDIControlFunction ("rotary.speed-toggle",    setWhirlSustainPedal);
  useMIDIControlFunction ("rotary.speed-preset",    revControl);
  useMIDIControlFunction ("rotary.speed-select",    revControlAll);

  useMIDIControlFunction ("whirl.horn.filter.a.type", setHornFilterAType);
  useMIDIControlFunction ("whirl.horn.filter.a.hz",   setHornFilterAFrequency);
  useMIDIControlFunction ("whirl.horn.filter.a.q",    setHornFilterAQ);
  useMIDIControlFunction ("whirl.horn.filter.a.gain", setHornFilterAGain);
  useMIDIControlFunction ("whirl.horn.filter.b.type", setHornFilterBType);
  useMIDIControlFunction ("whirl.horn.filter.b.hz",   setHornFilterBFrequency);
  useMIDIControlFunction ("whirl.horn.filter.b.q",    setHornFilterBQ);
  useMIDIControlFunction ("whirl.horn.filter.b.gain", setHornFilterBGain);

  useMIDIControlFunction ("whirl.horn.breakpos", setHornBreakPosition);
  useMIDIControlFunction ("whirl.drum.breakpos", setDrumBreakPosition);

  useMIDIControlFunction ("whirl.horn.acceleration", setHornAcceleration);
  useMIDIControlFunction ("whirl.horn.deceleration", setHornDeceleration);
  useMIDIControlFunction ("whirl.drum.acceleration", setDrumAcceleration);
  useMIDIControlFunction ("whirl.drum.deceleration", setDrumDeceleration);
}

/*
 * Configuration interface.
 */
int whirlConfig (ConfigContext * cfg) {
  double d;
  int k;
  int rtn = 1;
  if (getConfigParameter_d ("whirl.horn.slowrpm", cfg, &d) == 1) {
    hornRPMslow = (float) d;
  }
  else if (getConfigParameter_d ("whirl.horn.fastrpm", cfg, &d) == 1) {
    hornRPMfast = (float) d;
  }
  else if (getConfigParameter_d ("whirl.horn.acceleration", cfg, &d) == 1) {
    hornAcc = (float) d;
  }
  else if (getConfigParameter_d ("whirl.horn.deceleration", cfg, &d) == 1) {
    hornDec = (float) d;
  }
  else if (getConfigParameter_d ("whirl.drum.slowrpm", cfg, &d) == 1) {
    drumRPMslow = (float) d;
  }
  else if (getConfigParameter_d ("whirl.drum.fastrpm", cfg, &d) == 1) {
    drumRPMfast = (float) d;
  }
  else if (getConfigParameter_d ("whirl.drum.acceleration", cfg, &d) == 1) {
    drumAcc = (float) d;
  }
  else if (getConfigParameter_d ("whirl.drum.deceleration", cfg, &d) == 1) {
    drumDec = (float) d;
  }
  else if (getConfigParameter_d ("whirl.horn.radius", cfg, &d) == 1) {
    hornRadiusCm = (float) d;
  }
  else if (getConfigParameter_d ("whirl.drum.radius", cfg, &d) == 1) {
    drumRadiusCm = (float) d;
  }
  else if (getConfigParameter_d ("whirl.horn.level", cfg, &d) == 1) {
    hornLevel = (float) d;
  }
  else if (getConfigParameter_d ("whirl.horn.leak", cfg, &d) == 1) {
    leakLevel = (float) d;
  }
  else if (getConfigParameter_d ("whirl.mic.distance", cfg, &d) == 1) {
    micDistCm = (float) d;
  }
  else if (getConfigParameter_ir
	   ("whirl.drum.filter.type", cfg, &k, 0, 8) == 1) {
    lpT = k;
  }
  else if (getConfigParameter_d ("whirl.drum.filter.q", cfg, &d) == 1) {
    lpQ =  d;
  }
  else if (getConfigParameter_d ("whirl.drum.filter.hz", cfg, &d) == 1) {
    lpF =  d;
  }
  else if (getConfigParameter_d ("whirl.drum.filter.gain", cfg, &d) == 1) {
    lpG =  d;
  }
  else if (getConfigParameter_ir
	   ("whirl.horn.filter.a.type", cfg, &k, 0, 8) == 1) {
    haT = k;
  }
  else if (getConfigParameter_d ("whirl.horn.filter.a.hz", cfg, &d) == 1) {
    haF = (double) d;
  }
  else if (getConfigParameter_d ("whirl.horn.filter.a.q", cfg, &d) == 1) {
    haQ = (double) d;
  }
  else if (getConfigParameter_d ("whirl.horn.filter.a.gain", cfg, &d) == 1) {
    haG = (double) d;
  }
  else if (getConfigParameter_ir
	   ("whirl.horn.filter.b.type", cfg, &k, 0, 8) == 1) {
    hbT = k;
  }
  else if (getConfigParameter_d ("whirl.horn.filter.b.hz", cfg, &d) == 1) {
    hbF = (double) d;
  }
  else if (getConfigParameter_d ("whirl.horn.filter.b.q", cfg, &d) == 1) {
    hbQ = (double) d;
  }
  else if (getConfigParameter_d ("whirl.horn.filter.b.gain", cfg, &d) == 1) {
    hbG = (double) d;
  }
  else if (getConfigParameter_d ("whirl.horn.comb.a.feedback", cfg, &d) == 1) {
    cb0fb = (double) d;
  }
  else if (getConfigParameter_i ("whirl.horn.comb.a.delay", cfg, &k) == 1) {
    cb0dl = k;
  }
  else if (getConfigParameter_d ("whirl.horn.comb.b.feedback", cfg, &d) == 1) {
    cb1fb = (double) d;
  }
  else if (getConfigParameter_i ("whirl.horn.comb.b.delay", cfg, &k) == 1) {
    cb1dl = k;
  }
  else if (getConfigParameter_i ("whirl.speed-preset", cfg, &k) == 1) {
    revSelect = k;
  }
  else if (getConfigParameter_ir ("whirl.bypass", cfg, &k, 0, 1) == 1) {
    bypass = k;
  }
  else if (getConfigParameter_dr ("whirl.horn.breakpos", cfg, &d, 0, 1.0) == 1) {
    hnBreakPos = (double) d;
  }
  else if (getConfigParameter_dr ("whirl.drum.breakpos", cfg, &d, 0, 1.0) == 1) {
    drBreakPos = (double) d;
  }

  else {
    rtn = 0;
  }
  return rtn;
}

static const ConfigDoc doc[] = {
  {"whirl.bypass", CFG_INT, "0", "if set to 1, completely bypass leslie emulation"},
  {"rotary.speed-preset", CFG_INT, "0", "horn and drum speed. 0:stopped, 1:slow, 2:fast"},
  {"whirl.horn.slowrpm", CFG_DOUBLE, "48.0", "RPM"},
  {"whirl.horn.fastrpm", CFG_DOUBLE, "400.0", "RPM"},
  {"whirl.horn.acceleration", CFG_DOUBLE, "350.0", "RPM/sec"},
  {"whirl.horn.deceleration", CFG_DOUBLE, "448.0", "RPM/sec"},
  {"whirl.horn.breakpos", CFG_DOUBLE, "0", "horn stop position 0: free, 0.0-1.0 clockwise position where to stop. 1.0:front-center"},
  {"whirl.drum.slowrpm", CFG_DOUBLE, "40.0", "RPM"},
  {"whirl.drum.fastrpm", CFG_DOUBLE, "342.0", "RPM"},
  {"whirl.drum.acceleration", CFG_DOUBLE, "120.0", "RPM/sec"},
  {"whirl.drum.deceleration", CFG_DOUBLE, "102.0", "RPM/sec"},
  {"whirl.drum.breakpos", CFG_DOUBLE, "0", "drum stop position 0: free, 0.0-1.0 clockwise position where to stop. 1.0:front-center"},
  {"whirl.horn.radius", CFG_DOUBLE, "19.2", "in centimeter."},
  {"whirl.drum.radius", CFG_DOUBLE, "22.0", "in centimeter."},
  {"whirl.mic.distance", CFG_DOUBLE, "42.0", "distance from mic to origin in centimeters."},
  {"whirl.horn.level", CFG_DOUBLE, "0.7", "horn wet-signal volume"},
  {"whirl.horn.leak", CFG_DOUBLE, "0.15", "horh dry-signal leak"},
  {"whirl.drum.filter.type", CFG_INT, "8", "Filter type: 0-8. see \"Filter types\" below "},
  {"whirl.drum.filter.q", CFG_DOUBLE, "1.6016", ""},
  {"whirl.drum.filter.hz", CFG_DOUBLE, "811.9695", ""},
  {"whirl.drum.filter.gain", CFG_DOUBLE, "-38.9291", ""},
  {"whirl.horn.filter.a.type", CFG_INT, "0", ""},
  {"whirl.horn.filter.a.hz", CFG_DOUBLE, "4500", "Filter frequency; range: [250..8000]"},
  {"whirl.horn.filter.a.q", CFG_DOUBLE, "2.7456", "Filter Quality; range: [0.01..6.0]"},
  {"whirl.horn.filter.a.gain", CFG_DOUBLE, "-30.0", "range: [-48.0..48.0]"},
  {"whirl.horn.filter.b.type", CFG_INT, "7", ""},
  {"whirl.horn.filter.b.hz", CFG_DOUBLE, "300.0", ""},
  {"whirl.horn.filter.b.q", CFG_DOUBLE, "1.0", ""},
  {"whirl.horn.filter.b.gain", CFG_DOUBLE, "-30.0", ""},
  {"whirl.horn.comb.a.feedback", CFG_DOUBLE, "-0.55", ""},
  {"whirl.horn.comb.a.delay", CFG_INT, "38", ""},
  {"whirl.horn.comb.b.feedback", CFG_DOUBLE, "-0.3508", ""},
  {"whirl.horn.comb.b.delay", CFG_DOUBLE, "120", ""},
  {NULL}
};

const ConfigDoc *whirlDoc () {
  return doc;
}


/*
 *
 */
void whirlProc2 (const float * inbuffer,
		 float * outL,  float * outR,
		 float * outHL, float * outHR,
		 float * outDL, float * outDR,
		 size_t bufferLengthSamples) {

  const float * xp = inbuffer;
  int i;
  static float z[12];

  if (bypass) {
    for (i = 0; i < bufferLengthSamples; i++) {
      if (outL) *outL++ = inbuffer[i];
      if (outR) *outR++ = inbuffer[i];
      if (outHL) *outHL++ = inbuffer[i];
      if (outHR) *outHR++ = inbuffer[i];
      if (outDL) *outDL++ = 0;
      if (outDR) *outDR++ = 0;
    }
    return;
  }

  /* compute rotattion speeds , increment/decrement dep on acceleration */

  if (hornAcDc) {
    if (0 < hornAcDc) {
      hornIncrUI += fixptd(hornAII * bufferLengthSamples);
      if (hornTarget <= hornIncrUI) {
	hornIncrUI = hornTarget;
	hornAcDc = 0;
      }
    }
    else {
      hornIncrUI -= fixptd(hornAII * bufferLengthSamples);
      if (hornIncrUI <= hornTarget) {
	hornIncrUI = hornTarget;
	hornAcDc = 0;
      }
    }
  }

  if (drumAcDc) {
    if (0 < drumAcDc) {
      drumIncrUI += fixptd(drumAII * bufferLengthSamples);
      if (drumTarget <= drumIncrUI) {
	drumIncrUI = drumTarget;
	drumAcDc = 0;
      }
    }
    else {
      drumIncrUI -= fixptd(drumAII * bufferLengthSamples);
      if (drumIncrUI <= drumTarget) {
	drumIncrUI = drumTarget;
	drumAcDc = 0;
      }
    }
  }

#if 1
  /* break position -- don't stop anywhere..
     the original Leslie can not do this, sometimes the horn is aimed at the back of
     the cabinet when it comes to a halt, which results in a less than desirable sound.

     continue to slowly move the horn and drum to the center position after it actually
     came to a stop.
   */
  if (hnBreakPos>0) {
    const int targetPos= (hnBreakPos>=1.0)? 0 : (hnBreakPos * DISPLC_MASK);
    if (!hornAcDc && hornIncrUI==0 && hornAngle!=targetPos) {
      hornAngle = (hornAngle + (DISPLC_MASK/200)) & DISPLC_MASK;
      if ((hornAngle-targetPos) < (DISPLC_MASK/180)) hornAngle=targetPos;
    }
  }
  if (drBreakPos>0) {
    const int targetPos= (drBreakPos>=1.0)? 0 : (drBreakPos * DISPLC_MASK);
    if (!drumAcDc && drumIncrUI==0 && drumAngle!=targetPos) {
      drumAngle = (drumAngle + (DISPLC_MASK/200)) & DISPLC_MASK;
      if ((drumAngle-targetPos) < (DISPLC_MASK/180)) drumAngle=targetPos;
    }
  }
#endif

#if 0 // DEBUG Position, Speed, Acceleration
  char const * const acdc[3]= {"<","#",">"};
  static int fgh=0;
  if ((fgh++ % (int)(SampleRateD/128/5) ) ==0) {
    printf ("H:%.3f D:%.3f | HS:%.2f DS:%.2f [Hz]| HT:%.2f DT:%.2f [Hz]| HA:%.5f%s DA:%.5f%s [Hz/s]\n",
	(double)hornAngle/DISPLC_MASK, (double)drumAngle/DISPLC_MASK,
	SampleRateD*(double)hornIncrUI/DISPLC_MASK, SampleRateD*(double)drumIncrUI/DISPLC_MASK,
	SampleRateD*(double)hornTarget/DISPLC_MASK, SampleRateD*(double)drumTarget/DISPLC_MASK,
	SampleRateD*(double)fixptd(hornAII * bufferLengthSamples)/DISPLC_MASK, acdc[hornAcDc+1],
	SampleRateD*(double)fixptd(drumAII * bufferLengthSamples)/DISPLC_MASK, acdc[drumAcDc+1]
	);
  }
#endif

  for (i = 0; i < bufferLengthSamples; i++) {
    unsigned int k;
    unsigned int n;
    float q;
    float r;
    float w;
    float x = (float) (*xp++) + DENORMAL_HACK;
    float xa;
    float xx = x;
    float leak = 0;

  //if (IS_DENORMAL(x)) fprintf(stderr,"DENORMAL x!\n");

#define ANGFILTER(BW,DX,DI) {           \
    xa  = BW[k][0] * x;                     \
    xa += BW[k][1] * DX[(DI)];              \
    xa += BW[k][2] * DX[((DI)+1) & AGMASK]; \
    xa += BW[k][3] * DX[((DI)+2) & AGMASK]; \
    xa += BW[k][4] * DX[((DI)+3) & AGMASK]; \
    }

#define ADDHIST(DX,DI,XS) {      \
    DI = (DI + AGMASK) & AGMASK; \
    DX[DI] = XS;}

#if 1
    /* Assumes that input samples have width=1.0. Results in artefacts. */
#define HN_MOTION(P,BUF,DSP,BW,DX,DI) {                 \
    k = ((hornAngle + hornPhase[(P)] + hornFixedOff) & DISPLC_MASK) >> 16; \
    w = hornSpacing[(P)] + DSP[k] + (float) outpos;         \
    r = floorf (w);                                         \
    ANGFILTER(BW,DX,DI);                                \
    q = xa * (w - r);                                       \
    n = ((unsigned int) r) & BUF_MASK_SAMPLES;              \
    BUF[n] += xa - q;                                       \
    n = (n + 1) & BUF_MASK_SAMPLES;                         \
    BUF[n] += q;}

#define DR_MOTION(P,BUF,DSP) {                              \
    k = ((drumAngle + drumPhase[(P)] + drumFixedOff) & DISPLC_MASK) >> 16; \
    w = drumSpacing[(P)] + DSP[k] + (float) outpos;         \
    r = floorf (w);                                         \
    q = x * (w - r);                                        \
    n = ((unsigned int) r) & BUF_MASK_SAMPLES;              \
    BUF[n] += x - q;                                        \
    n = (n + 1) & BUF_MASK_SAMPLES;                         \
    BUF[n] += q;}

#else
    /*
     * Strangely enough, all my attempts to alleviate the under- and
     * overlap noise sounded worse than the above.
     */
#endif

    /* This is just a bum filter to take some high-end off. */
#if 1
#define FILTER_C(W0,W1,I) {           \
    float temp = x;                   \
    x = ((W0) * x) + ((W1) * z[(I)]); \
    z[(I)] = temp; }
#else
#define FILTER_C(W0,W1,I) { \
    x = ((W0) * x) + ((W1) * z[(I)]); \
    z[(I)] = x;}
#endif

#define EQ_IIR(W,X,Y) { \
    float temp = (X) - (W[a1] * W[z0]) - (W[a2] * W[z1]); \
    Y = (temp * W[b0]) + (W[b1] * W[z0]) + (W[b2] * W[z1]); \
    W[z1] = W[z0]; \
    W[z0] = temp;}

#define COMB(WP,RP,BP,ES,FB,X) {\
    X += ((*(RP)++) * (FB)); \
    *(WP)++ = X; \
    if ((RP) == (ES)) RP = BP; \
    if ((WP) == (ES)) WP = BP;}

    /* 1) apply filters A,B -- horn-speaker characteristics
     * intput: x
     * output: x', leak
     */

    EQ_IIR(hafw, x, x);
    EQ_IIR(hbfw, x, x);

    leak = x * leakage;

#if 0 /* only causes hiss-noise - in particular on 'E-4,F-4' ~660Hz
       * no audible benefit to leslie effect
       */
      COMB(cb0wp, cb0rp, cb0bp, cb0es, cb0fb, x);
      COMB(cb1wp, cb1rp, cb1bp, cb1es, cb1fb, x);
#endif

    /* 2) now do doppler shift for the horn -- FM
     * intput: x' (filtered x)
     * output: HLbuf, HRbuf, leak
     */

    /* --- STATIC HORN FILTER --- */
#if 1
    /* HORN PRIMARY */
    HN_MOTION(0, HLbuf, hnFwdDispl, bfw, adx0, adi0);
    HN_MOTION(1, HRbuf, hnBwdDispl, bbw, adx0, adi0);
    ADDHIST(adx0, adi0, x);
#endif

#if 1
    /* --- HORN FIRST REFLECTION FILTER --- */
    FILTER_C(0.4, 0.4, 0);

    /* HORN FIRST REFLECTION */
    HN_MOTION(2, HLbuf, hnBwdDispl, bbw, adx1, adi1);
    HN_MOTION(3, HRbuf, hnFwdDispl, bfw, adx1, adi1);
    ADDHIST(adx1, adi1, x);
#endif
#if 1
    /* --- HORN SECOND REFLECTION FILTER --- */
    FILTER_C(0.4, 0.4, 1);

    /* HORN SECOND REFLECTION */
    HN_MOTION(4, HLbuf, hnFwdDispl, bfw, adx2, adi2);
    HN_MOTION(5, HRbuf, hnBwdDispl, bbw, adx2, adi2);
    ADDHIST(adx2, adi2, x);

#endif

    /* 1A) do doppler shift for drum (actually orig signal -- FM
     * intput: x
     * output: DLbuf, DRbuf
     * */

    x = xx;

#define DRUM
#ifdef DRUM
#if 1
    /* --- DRUM --- */
    DR_MOTION(0, DLbuf, drFwdDispl);
    DR_MOTION(1, DRbuf, drBwdDispl);
#endif
#if 1
    FILTER_C(0.4, 0.4, 2);
    DR_MOTION(2, DLbuf, drBwdDispl);
    DR_MOTION(3, DRbuf, drFwdDispl);
#endif
#if 1
    FILTER_C(0.4, 0.4, 3);
    DR_MOTION(4, DLbuf, drFwdDispl);
    DR_MOTION(5, DRbuf, drBwdDispl);
#endif
#endif /* DRUM */

    /* 1B) apply filter to drum-signal - and add horn */

    {
      float y;
      EQ_IIR(drfw, DLbuf[outpos], y);
      if (outL)
	*outL++ = (float) (y + (hornLevel * HLbuf[outpos]) + leak);
      if (outHL)
	*outHL++ = (hornLevel * HLbuf[outpos]) + leak;
      if (outDL)
	*outDL++ = y;

      EQ_IIR(drfw, DRbuf[outpos], y);
      if (outR)
	*outR++ = (float) (y + (hornLevel * HRbuf[outpos]) + leak);
      if (outHR)
	*outHR++ = (hornLevel * HRbuf[outpos]) + leak;
      if (outDR)
	*outDR++ = y;
    }

    HLbuf[outpos] = 0.0;
    HRbuf[outpos] = 0.0;
    DLbuf[outpos] = 0.0;
    DRbuf[outpos] = 0.0;

    outpos = (outpos + 1) & BUF_MASK_SAMPLES;
    hornAngle = (hornAngle + hornIncrUI) & DISPLC_MASK;
    drumAngle = (drumAngle + drumIncrUI) & DISPLC_MASK;
  }
}

extern void whirlProc (const float * inbuffer,
		       float * outbL,
		       float * outbR,
		       size_t bufferLengthSamples)
{
  whirlProc2(inbuffer, outbL, outbR,
      NULL, NULL,
      NULL, NULL,
      bufferLengthSamples);
}

/* vi:set ts=8 sts=2 sw=2: */
