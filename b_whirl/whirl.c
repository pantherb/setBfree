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

//#define DEBUG_SPEED  // debug acceleration,deceleration
//#define HORN_COMB_FILTER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "eqcomp.h"
#include "whirl.h"

extern double SampleRateD;

#define DISPLC_SIZE ((unsigned int) (1 << 11))
#define DISPLC_MASK ((DISPLC_SIZE) - 1)

#define BUF_SIZE_SAMPLES ((unsigned int) (1 << 11))
#define BUF_MASK_SAMPLES (BUF_SIZE_SAMPLES - 1)

static int bypass = 0;        ///< if set to 1 completely bypass this effect
static double hnBreakPos = 0; ///< where to stop horn - 0: free, 1.0: front-center, ]0..1] clockwise circle */
static double drBreakPos = 0; ///< where to stop drum

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

static double hornAngleGRD = 0;  /* 0..1 */
static double drumAngleGRD = 0;

static int hornAngle = 0;
static int drumAngle = 0;

/* rotational frequency and time-constats were taken from the paper
 * "Discrete Time Emulation of the Leslie Speaker"
 * by Jorge Herrera, Craig Hanson, and Jonathan S. Abel
 * Presented at the 127th Convention
 * 2009 October 9–12 New York NY, USA
 *
 *  horn: fast:7.056 Hz, slow: 0.672 Hz
 *  drum: fast:5.955 Hz, slow: 0.101 Hz (wrong?)
 *
 * alternate values:
 * http://www.dairiki.org/HammondWiki/LeslieRotationSpeed 
 *  horn: fast: 400 RPM, slow: 48 RPM
 *  drum: fast: 342 RPM, slow: 40 RPM
 */

/* target speed */
static float hornRPMslow = 60.0 * 0.672;
static float hornRPMfast = 60.0 * 7.056;
static float drumRPMslow = 60.0 * 0.600;
static float drumRPMfast = 60.0 * 5.955;

/* time constants [s] -- first order differential */
static float hornAcc = 0.161;
static float hornDec = 0.321;
static float drumAcc = 4.127;
static float drumDec = 1.371;

typedef struct _revcontrol {
  double hornTarget;
  double drumTarget;
} RevControl;

#define revSelectEnd (4)
static RevControl revoptions [9];
static int revselects[revSelectEnd];
static int revSelect = 0;

static int hornAcDc = 0;
static int drumAcDc = 0;

static double hornIncrUI = 0; ///< current angular speed - unit: radians / sample / (2*M_PI)
static double drumIncrUI = 0; ///< current angular speed - unit: radians / sample / (2*M_PI)

static double hornTarget = 0; ///< target angular speed  - unit: radians / sample / (2*M_PI)
static double drumTarget = 0; ///< target angular speed  - unit: radians / sample / (2*M_PI)

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

static float drfL[8];		/* Drum filter */
static float drfR[8];		/* Drum filter */
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

#ifdef HORN_COMB_FILTER
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
#else /* allow to parse config files which include these values */
static float cb0fb = 0;
static int   cb0dl = 0;
static float cb1fb = 0;
static int   cb1dl = 0;
#endif

static float hornLevel = 0.7;
static float leakLevel = 0.15;
static float leakage = 0;


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
    hornAcDc = 1;
  }
  else if (hornTarget < hornIncrUI) {
    hornAcDc = -1;
  }
  if (drumIncrUI < drumTarget) {
    drumAcDc = 1;
  }
  else if (drumTarget < drumIncrUI) {
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
			  double hnTgt,
			  double drTgt) {
  revoptions[i].hornTarget = hnTgt;
  revoptions[i].drumTarget = drTgt;
}

static void computeRotationSpeeds () {
  const double hfast = hornRPMfast / (SampleRateD * 60.0);
  const double hslow = hornRPMslow / (SampleRateD * 60.0);
  const double hstop = 0;
  const double dfast = drumRPMfast / (SampleRateD * 60.0);
  const double dslow = drumRPMslow / (SampleRateD * 60.0);
  const double dstop = 0;

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
    bfw[i & DISPLC_MASK][partial] = (float) w;
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
  }

  hornPhase[0] = 0;
  hornPhase[1] = DISPLC_SIZE >> 1;

  hornPhase[2] = ((DISPLC_SIZE * 2) / 6);
  hornPhase[3] = ((DISPLC_SIZE * 5) / 6);

  hornPhase[4] = ((DISPLC_SIZE * 1) / 6);
  hornPhase[5] = ((DISPLC_SIZE * 4) / 6);

  for (i = 0; i < 6; i++) {
    hornSpacing[i] += hornRadiusSamples + 1.0;
  }

  drumPhase[0] = 0;
  drumPhase[1] = DISPLC_SIZE >> 1;

  drumPhase[2] = ((DISPLC_SIZE * 2) / 6);
  drumPhase[3] = ((DISPLC_SIZE * 5) / 6);

  drumPhase[4] = ((DISPLC_SIZE * 1) / 6);
  drumPhase[5] = ((DISPLC_SIZE * 4) / 6);

  for (i = 0; i < 6; i++) {
    drumSpacing[i] += drumRadiusSamples + 1.0;
  }

  setIIRFilter (drfL, lpT, lpF, lpQ, lpG);
  setIIRFilter (drfR, lpT, lpF, lpQ, lpG);
  setIIRFilter (hafw, haT, haF, haQ, haG);
  setIIRFilter (hbfw, hbT, hbF, hbQ, hbG);

#ifdef HORN_COMB_FILTER
  cb0rp = &(comb0[COMB_SIZE - cb0dl]);
  cb0wp = &(comb0[0]);
  cb0bp = &(comb0[0]);
  cb0es = &(comb0[COMB_SIZE]);

  cb1rp = &(comb1[COMB_SIZE - cb1dl]);
  cb1wp = &(comb1[0]);
  cb1bp = &(comb1[0]);
  cb1es = &(comb1[COMB_SIZE]);
#endif

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
#if 1 // Fredrik
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
  hornAcc = .01 + (double)uc/80.0;
}

void setHornDeceleration (unsigned char uc) {
  hornDec = .01 + (double)uc/80.0;
}

void setDrumAcceleration (unsigned char uc) {
  drumAcc = .01 + (double)uc/14.0;
}

void setDrumDeceleration (unsigned char uc) {
  drumDec = .01 + (double)uc/14.0;
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
    revSelect = k % revSelectEnd;
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
  {"whirl.speed-preset", CFG_INT, "0", "initial horn and drum speed. 0:stopped, 1:slow, 2:fast"},
  {"whirl.horn.slowrpm", CFG_DOUBLE, "40.32", "target RPM for slow (aka choral) horn speed"},
  {"whirl.horn.fastrpm", CFG_DOUBLE, "423.36", "target RPM for fast (aka tremolo) horn speed"},
  {"whirl.horn.acceleration", CFG_DOUBLE, "0.161", "time constant; seconds. Time required to accelerate reduced by a factor exp(1) = 2.718.."},
  {"whirl.horn.deceleration", CFG_DOUBLE, "0.321", "time constant; seconds. Time required to decelerate reduced by a factor exp(1) = 2.718.."},
  {"whirl.horn.breakpos", CFG_DOUBLE, "0", "horn stop position 0: free, 0.0-1.0 clockwise position where to stop. 1.0:front-center"},
  {"whirl.drum.slowrpm", CFG_DOUBLE, "36.0", "target RPM for slow (aka choral) drum speed."},
  {"whirl.drum.fastrpm", CFG_DOUBLE, "357.3", "target RPM for fast (aka tremolo) drum speed."},
  {"whirl.drum.acceleration", CFG_DOUBLE, "4.127", "time constant in seconds. Time required to accelerate reduced by a factor exp(1) = 2.718.."},
  {"whirl.drum.deceleration", CFG_DOUBLE, "1.371", "time constant in seconds. Time required to decelerate reduced by a factor exp(1) = 2.718.."},
  {"whirl.drum.breakpos", CFG_DOUBLE, "0", "drum stop position 0: free, 0.0-1.0 clockwise position where to stop. 1.0:front-center"},
  {"whirl.horn.radius", CFG_DOUBLE, "19.2", "in centimeter."},
  {"whirl.drum.radius", CFG_DOUBLE, "22.0", "in centimeter."},
  {"whirl.mic.distance", CFG_DOUBLE, "42.0", "distance from mic to origin in centimeters."},
  {"whirl.horn.level", CFG_DOUBLE, "0.7", "horn wet-signal volume"},
  {"whirl.horn.leak", CFG_DOUBLE, "0.15", "horh dry-signal leak"},
  {"whirl.drum.filter.type", CFG_INT, "8", "Filter type: 0-8. see \"Filter types\" below. This filter separates the signal to be sent to the drum-speaker. It should be a high-shelf filter with negative gain."},
  {"whirl.drum.filter.q", CFG_DOUBLE, "1.6016", "Filter Quality, bandwidth; range: [0.2..3.0]"},
  {"whirl.drum.filter.hz", CFG_DOUBLE, "811.9695", "Filter frequency."},
  {"whirl.drum.filter.gain", CFG_DOUBLE, "-38.9291", "Filter gain [-48.0..48.0]"},
  {"whirl.horn.filter.a.type", CFG_INT, "0", "Filter type: 0-8. see \"Filter types\" below. This is the first of two filters to shape the signal to be sent to the horn-speaker; by default a low-pass filter with negative gain to cut off high freqencies."},
  {"whirl.horn.filter.a.hz", CFG_DOUBLE, "4500", "Filter frequency; range: [250..8000]"},
  {"whirl.horn.filter.a.q", CFG_DOUBLE, "2.7456", "Filter Quality; range: [0.01..6.0]"},
  {"whirl.horn.filter.a.gain", CFG_DOUBLE, "-30.0", "range: [-48.0..48.0]"},
  {"whirl.horn.filter.b.type", CFG_INT, "7", "Filter type: 0-8. see \"Filter types\" below. This is the second of two filters to shape the signal to be sent to the horn-speaker; by default a low-shelf filter with negative gain to remove frequencies which are sent to the drum."},
  {"whirl.horn.filter.b.hz", CFG_DOUBLE, "300.0", "Filter frequency; range: [250..8000]"},
  {"whirl.horn.filter.b.q", CFG_DOUBLE, "1.0", "Filter Quality, bandwidth; range: [0.2..3.0]"},
  {"whirl.horn.filter.b.gain", CFG_DOUBLE, "-30.0", "Filter gain [-48.0..48.0]"},
#if 0 // comb-filter is disabled
  {"whirl.horn.comb.a.feedback", CFG_DOUBLE, "-0.55", ""},
  {"whirl.horn.comb.a.delay", CFG_INT, "38", ""},
  {"whirl.horn.comb.b.feedback", CFG_DOUBLE, "-0.3508", ""},
  {"whirl.horn.comb.b.delay", CFG_DOUBLE, "120", ""},
#endif
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
  static float z[4] = {0,0,0,0};

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

  /* compute rotational speeds for this cycle */
  if (hornAcDc) {
    const double l = exp(-1.0/(SampleRateD / bufferLengthSamples * (hornAcDc>0? hornAcc : hornDec )));
    hornIncrUI += (1-l) * (hornTarget - hornIncrUI);

    if (fabs(hornTarget - hornIncrUI) < (1.0/360.0/SampleRateD) ) {
#ifdef DEBUG_SPEED
      printf("AcDc Horn off\n");
#endif
      hornAcDc = 0;
      hornIncrUI = hornTarget;
    }
  }

  if (drumAcDc) {
    const double l = exp(-1.0/(SampleRateD / bufferLengthSamples * (drumAcDc>0? drumAcc: drumDec )));
    drumIncrUI += (1-l) * (drumTarget - drumIncrUI);

    if (fabs(drumTarget - drumIncrUI) < (1.0/360.0/SampleRateD)) {
#ifdef DEBUG_SPEED
      printf("ACDC Drum off\n");
#endif
      drumAcDc = 0;
      drumIncrUI = drumTarget;
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
    const double targetPos= hnBreakPos - floor(hnBreakPos);
    if (!hornAcDc && hornIncrUI==0 && hornAngleGRD!=targetPos) {
      hornAngleGRD += 1.0/400.0;
      hornAngleGRD = hornAngleGRD - floor(hornAngleGRD);
      if ((hornAngleGRD-targetPos) < (1.0/360.0)) hornAngleGRD=targetPos;
    }
  }
  if (drBreakPos>0) {
    const int targetPos= drBreakPos - floor(drBreakPos);
    if (!drumAcDc && drumIncrUI==0 && drumAngleGRD!=targetPos) {
      drumAngleGRD += 1.0/400.0;
      drumAngleGRD = drumAngleGRD - floor(drumAngleGRD);
      if ((drumAngleGRD-targetPos) < (1.0/360.0)) drumAngleGRD=targetPos;
    }
  }
#endif

#ifdef DEBUG_SPEED
  char const * const acdc[3]= {"<","#",">"};
  static int fgh=0;
  if ((fgh++ % (int)(SampleRateD/128/5) ) ==0) {
    printf ("H:%.3f D:%.3f | HS:%.3f DS:%.3f [Hz]| HT:%.2f DT:%.2f [Hz]| %s %s\n",
	(double)hornAngle/DISPLC_SIZE, (double)drumAngle/DISPLC_SIZE,
	SampleRateD*(double)hornIncrUI, SampleRateD*(double)drumIncrUI,
	SampleRateD*(double)hornTarget, SampleRateD*(double)drumTarget,
	acdc[hornAcDc+1], acdc[drumAcDc+1]
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

#define HN_MOTION(P,BUF,DSP,BW,DX,DI) {                     \
    k = ((hornAngle + hornPhase[(P)]) & DISPLC_MASK);       \
    w = hornSpacing[(P)] + DSP[k] + (float) outpos;         \
    r = floorf (w);                                         \
    ANGFILTER(BW,DX,DI);                                    \
    q = xa * (w - r);                                       \
    n = ((unsigned int) r) & BUF_MASK_SAMPLES;              \
    BUF[n] += xa - q;                                       \
    n = (n + 1) & BUF_MASK_SAMPLES;                         \
    BUF[n] += q;}

#define DR_MOTION(P,BUF,DSP) {                              \
    k = ((drumAngle + drumPhase[(P)]) & DISPLC_MASK);       \
    w = drumSpacing[(P)] + DSP[k] + (float) outpos;         \
    r = floorf (w);                                         \
    q = x * (w - r);                                        \
    n = ((unsigned int) r) & BUF_MASK_SAMPLES;              \
    BUF[n] += x - q;                                        \
    n = (n + 1) & BUF_MASK_SAMPLES;                         \
    BUF[n] += q;}

    /* This is just a bum filter to take some high-end off. */
#if 1
#define FILTER_C(W0,W1,I) {           \
    float temp = x;                   \
    x = ((W0) * x) + ((W1) * z[(I)]); \
    z[(I)] = temp; }
#else
#define FILTER_C(W0,W1,I) {           \
    x = ((W0) * x) + ((W1) * z[(I)]); \
    z[(I)] = x;}
#endif

#define EQ_IIR(W,X,Y) {                                     \
    float temp = (X) - (W[a1] * W[z0]) - (W[a2] * W[z1]);   \
    Y = (temp * W[b0]) + (W[b1] * W[z0]) + (W[b2] * W[z1]); \
    W[z1] = W[z0]; \
    W[z0] = temp;}

    /* 1) apply filters A,B -- horn-speaker characteristics
     * intput: x
     * output: x', leak
     */

    EQ_IIR(hafw, x, x);
    EQ_IIR(hbfw, x, x);

    leak = x * leakage;

#ifdef HORN_COMB_FILTER

#define COMB(WP,RP,BP,ES,FB,X) { \
    X += ((*(RP)++) * (FB));     \
    *(WP)++ = X;                 \
    if ((RP) == (ES)) RP = BP;   \
    if ((WP) == (ES)) WP = BP;}

    /* only causes hiss-noise - in particular on 'E-4,F-4' ~660Hz
     * no audible benefit to leslie effect so far, needs tweaking
     */
    COMB(cb0wp, cb0rp, cb0bp, cb0es, cb0fb, x);
    COMB(cb1wp, cb1rp, cb1bp, cb1es, cb1fb, x);
#endif

    /* 2) now do doppler shift for the horn -- FM
     * intput: x' (filtered x)
     * output: HLbuf, HRbuf, leak
     */

    /* --- STATIC HORN FILTER --- */
    /* HORN PRIMARY */
    HN_MOTION(0, HLbuf, hnFwdDispl, bfw, adx0, adi0);
    HN_MOTION(1, HRbuf, hnBwdDispl, bbw, adx0, adi0);
    ADDHIST(adx0, adi0, x);

    /* HORN FIRST REFLECTION FILTER */
    FILTER_C(0.4, 0.4, 0);

    /* HORN FIRST REFLECTION */
    HN_MOTION(2, HLbuf, hnBwdDispl, bbw, adx1, adi1);
    HN_MOTION(3, HRbuf, hnFwdDispl, bfw, adx1, adi1);
    ADDHIST(adx1, adi1, x);

    /* HORN SECOND REFLECTION FILTER */
    FILTER_C(0.4, 0.4, 1);

    /* HORN SECOND REFLECTION */
    HN_MOTION(4, HLbuf, hnFwdDispl, bfw, adx2, adi2);
    HN_MOTION(5, HRbuf, hnBwdDispl, bbw, adx2, adi2);
    ADDHIST(adx2, adi2, x);

    /* 1A) do doppler shift for drum (actually orig signal -- FM
     * intput: x
     * output: DLbuf, DRbuf
     */

    x = xx; // x is used in macros, xx is the original input siganl

    /* --- DRUM --- */
    DR_MOTION(0, DLbuf, drFwdDispl);
    DR_MOTION(1, DRbuf, drBwdDispl);

    /* DRUM FIRST REFLECTION FILTER */
    FILTER_C(0.4, 0.4, 2);

    /* DRUM FIRST REFLECTION */
    DR_MOTION(2, DLbuf, drBwdDispl);
    DR_MOTION(3, DRbuf, drFwdDispl);

    /* DRUM SECOND REFLECTION FILTER */
    FILTER_C(0.4, 0.4, 3);

    /* DRUM SECOND REFLECTION */
    DR_MOTION(4, DLbuf, drFwdDispl);
    DR_MOTION(5, DRbuf, drBwdDispl);


    /* 1B) apply filter to drum-signal - and add horn */

    {
      float y;
      EQ_IIR(drfL, DLbuf[outpos], y);
      if (outL)
	*outL++ = (float) (y + (hornLevel * HLbuf[outpos]) + leak);
      if (outHL)
	*outHL++ = (hornLevel * HLbuf[outpos]) + leak;
      if (outDL)
	*outDL++ = y;

      EQ_IIR(drfR, DRbuf[outpos], y);
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

    hornAngleGRD = (hornAngleGRD + hornIncrUI);
    hornAngleGRD = hornAngleGRD - floor(hornAngleGRD);
    hornAngle = hornAngleGRD * DISPLC_SIZE;

    drumAngleGRD = (drumAngleGRD + drumIncrUI);
    drumAngleGRD = drumAngleGRD - floor(drumAngleGRD);
    drumAngle = drumAngleGRD * DISPLC_SIZE;
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
