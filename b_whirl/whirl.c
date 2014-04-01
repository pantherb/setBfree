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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

//#define DEBUG_SPEED  // debug acceleration,deceleration
//#define HORN_COMB_FILTER

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "eqcomp.h"
#include "whirl.h"

#define DISPLC_SIZE ((unsigned int) (1 << 11))
#define DISPLC_MASK ((DISPLC_SIZE) - 1)

#define BUF_SIZE_SAMPLES ((unsigned int) (1 << 11))
#define BUF_MASK_SAMPLES (BUF_SIZE_SAMPLES - 1)

void initValues(struct b_whirl *w) {
  unsigned int i;

  w->bypass=0;
  w->hnBreakPos=0;
  w->drBreakPos=0;

  for (i=0; i<4; ++i)
    w->z[i] = 0;

  /* The current angle of rotating elements */
  w->hornAngleGRD=0; /* 0..1 */
  w->drumAngleGRD=0;

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

  /* target speed  - RPM */
  w->hornRPMslow = 60.0 * 0.672;
  w->hornRPMfast = 60.0 * 7.056;
  w->drumRPMslow = 60.0 * 0.600;
  w->drumRPMfast = 60.0 * 5.955;

  /* time constants [s] -- first order differential */
  w->hornAcc = 0.161;
  w->hornDec = 0.321;
  w->drumAcc = 4.127;
  w->drumDec = 1.371;

  w->hornAcDc = w->drumAcDc = 0;

  /* angular speed - unit: radians / sample / (2*M_PI) */
  w->hornIncrGRD = 0; ///< current angular speed
  w->drumIncrGRD = 0; ///< current angular speed

  w->hornTarget = 0; ///< target angular speed
  w->drumTarget = 0; ///< target angular speed

  /*
   * Spacing between reflections in samples. The first can't be zero, since
   * we must allow for the swing of the extent to wander close to the reader.
   */

  w->hornSpacing[0] = 12.0; /* Primary */
  w->hornSpacing[1] = 18.0;
  w->hornSpacing[2] = 53.0; /* First reflection */
  w->hornSpacing[3] = 50.0;
  w->hornSpacing[4] = 106.0; /* Secondary reflection */
  w->hornSpacing[5] = 116.0;

  w->hornRadiusCm = 19.2;
  w->drumRadiusCm = 22.0;

  w->airSpeed = 340.0; /* Meters per second */
  w->micDistCm= 42.0;  /* From mic to origin */

  w->drumSpacing[0] = 36.0;
  w->drumSpacing[1] = 39.0;
  w->drumSpacing[2] = 79.0;
  w->drumSpacing[3] = 86.0;
  w->drumSpacing[4] = 123.0;
  w->drumSpacing[5] = 116.0;

  w->outpos=0;

  memset(w->drfR, 0, 8*sizeof(float));
  w->lpT = 8;		/* high shelf */
  w->lpF = 811.9695;	/* Frequency */
  w->lpQ =   1.6016;	/* Q, bandwidth */
  w->lpG = -38.9291;	/* Gain */

  memset(w->hafw, 0, 8*sizeof(float));
  w->haT = 0;		/* low pass */
  w->haF = 4500;	/* 3900.0; 25-nov-04 */
  w->haQ = 2.7456;	/* 1.4; 25-nov-04 */
  w->haG = -30.0; 	/* 0.0; 25-nov-04 */

  memset(w->hbfw, 0, 8*sizeof(float));
  w->hbT = 7;		/* low shelf */
  w->hbF = 300.0;
  w->hbQ =   1.0;
  w->hbG = -30.0;

  w->hornLevel = 0.7;
  w->leakLevel = 0.15;
  w->leakage = 0;

#ifdef HORN_COMB_FILTER
  memset(w->comb0, 0 , sizeof(float) * COMB_SIZE);
  w->cb0fb = -0.55;
  w->cb0dl = 38;

  memset(w->comb1, 0 , sizeof(float) * COMB_SIZE);
  w->cb1fb = -0.3508;
  w->cb1dl = 120;
#else
  w->cb0fb = 0;
  w->cb0dl = 0;
  w->cb1fb = 0;
  w->cb1dl = 0;
#endif

  // TODO zero arrays..
  memset(w->HLbuf, 0, sizeof(float) * WHIRL_BUF_SIZE_SAMPLES);
  memset(w->HRbuf, 0, sizeof(float) * WHIRL_BUF_SIZE_SAMPLES);
  memset(w->DLbuf, 0, sizeof(float) * WHIRL_BUF_SIZE_SAMPLES);
  memset(w->DRbuf, 0, sizeof(float) * WHIRL_BUF_SIZE_SAMPLES);

  memset(w->hnFwdDispl, 0, sizeof(float) * WHIRL_DISPLC_SIZE);
  memset(w->drFwdDispl, 0, sizeof(float) * WHIRL_DISPLC_SIZE);

  memset(w->hnBwdDispl, 0, sizeof(float) * WHIRL_DISPLC_SIZE);
  memset(w->drBwdDispl, 0, sizeof(float) * WHIRL_DISPLC_SIZE);

  for (i=0; i < WHIRL_DISPLC_SIZE; i++) {
    memset(&w->bfw[i], 0, sizeof(struct _bw));
    memset(&w->bbw[i], 0, sizeof(struct _bw));
  }

  memset(w->adx0, 0, sizeof(float) * AGBUF);
  memset(w->adx1, 0, sizeof(float) * AGBUF);
  memset(w->adx2, 0, sizeof(float) * AGBUF);
  w->adi0 = w->adi1 = w->adi2 = 0;

  //hornPhase, drumPhase -> initTables()
}

struct b_whirl *allocWhirl() {
  struct b_whirl *w = (struct b_whirl*) calloc(1, sizeof(struct b_whirl));
  if (!w) return NULL;
  initValues(w);
  return (w);
}

void freeWhirl(struct b_whirl *w) {
  free(w);
}


/*
 *
 */
static void setIIRFilter (float W[],
			  int T,
			  const double F,
			  const double Q,
			  const double G,
			  const double SR) {
  double C[6];
  eqCompute (T, F, Q, G, C, SR);
  W[a1] = C[EQC_A1];
  W[a2] = C[EQC_A2];
  W[b0] = C[EQC_B0];
  W[b1] = C[EQC_B1];
  W[b2] = C[EQC_B2];
}

/*
 * Sets the revolution selection.
 */
void setRevSelect (struct b_whirl *w, int n) {
  int i;

  w->revSelect = n % revSelectEnd;
  i = w->revselects[w->revSelect];
  useRevOption(w, i);
}

void useRevOption (struct b_whirl *w, int n) {
  int i = n % 9;

#if 0 // not in RT callback
  printf ("\rREV:%d %d ", n, i); fflush (stdout);
#endif

  w->hornTarget = w->revoptions[i].hornTarget;
  w->drumTarget = w->revoptions[i].drumTarget;

  if (w->hornIncrGRD < w->hornTarget) {
    w->hornAcDc = 1;
  }
  else if (w->hornTarget < w->hornIncrGRD) {
    w->hornAcDc = -1;
  }
  if (w->drumIncrGRD < w->drumTarget) {
    w->drumAcDc = 1;
  }
  else if (w->drumTarget < w->drumIncrGRD) {
    w->drumAcDc = -1;
  }

  notifyControlChangeByName(w->midi_cfg_ptr, "rotary.speed-select", n * 15);
}

void advanceRevSelect (struct b_whirl *w) {
  setRevSelect (w, w->revSelect + 1);
}

static void revControlAll (void *d, unsigned char u) {
  struct b_whirl *w = (struct b_whirl *) d;
  useRevOption (w, (int) (u / 15)); // 0..8
}

static void revControl (void *d, unsigned char u) {
  struct b_whirl *w = (struct b_whirl *) d;
  setRevSelect (w, (int) (u / 32)); // 3 modes only - stop, slow, fast, stop
}

/* 11-may-2004/FK Hack to make the pedal work. See midiIn.c */
void setWhirlSustainPedal (void *d, unsigned char u) {
  struct b_whirl *w = (struct b_whirl *) d;
  if (u) {
    setRevSelect (w, (w->revSelect == 1) ? 2 : 1);
  }
}

static void setRevOption (struct b_whirl *w,
			  int i,
			  double hnTgt,
			  double drTgt) {
  w->revoptions[i].hornTarget = hnTgt;
  w->revoptions[i].drumTarget = drTgt;
}

void computeRotationSpeeds (struct b_whirl *w) {
  const double hfast = w->hornRPMfast / (w->SampleRateD * 60.0);
  const double hslow = w->hornRPMslow / (w->SampleRateD * 60.0);
  const double hstop = 0;
  const double dfast = w->drumRPMfast / (w->SampleRateD * 60.0);
  const double dslow = w->drumRPMslow / (w->SampleRateD * 60.0);
  const double dstop = 0;

  setRevOption (w, 8, hfast, dfast);
  setRevOption (w, 7, hfast, dslow);
  setRevOption (w, 6, hfast, dstop);
  setRevOption (w, 5, hslow, dfast);
  setRevOption (w, 4, hslow, dslow);
  setRevOption (w, 3, hslow, dstop);
  setRevOption (w, 2, hstop, dfast);
  setRevOption (w, 1, hstop, dslow);
  setRevOption (w, 0, hstop, dstop);

  w->revselects[0] = 0;		/* stop */
  w->revselects[1] = 4;		/* both slow */
  w->revselects[2] = 8;		/* both fast */
  w->revselects[3] = 4;		/* both slow */
  setRevSelect(w, w->revSelect);
}

/*
 *
 */
#define ipolmark(degrees, level) { \
  ipx = degrees; \
  ipy = level; \
}

/*
 *
 */
static void _ipoldraw (struct b_whirl *sw, double degrees, double level, int partial, double *ipx, double *ipy) {
  double d;
  double e;
  double range;
  int fromIndex;
  int toIndex;
  int i;

  d = *ipx;
  while (d < 0.0) d += 360.0;
  fromIndex = (int) ((d * (double) DISPLC_SIZE) / 360.0);

  *ipx = degrees;

  e = *ipx;
  while (e < d) e += 360.0;
  toIndex = (int) ((e * (double) DISPLC_SIZE) / 360.0);

  range = (double) (toIndex - fromIndex);
  for (i = fromIndex; i <= toIndex; i++) {
    double x = (double) (i - fromIndex);
    double w = (*ipy) + ((x / range) * (level - (*ipy)));
    sw->bfw[i & DISPLC_MASK].b[partial] = (float) w;
  }

  *ipy = level;
} /* ipoldraw */

#define ipoldraw(degrees, level, partial) _ipoldraw(w, degrees, level, partial, &ipx, &ipy)

/*
 *
 */
static void initTables (struct b_whirl *w) {
  unsigned int i;
  unsigned int j;
  double ipx;
  double ipy;
  double sum;
  const double hornRadiusSamples = (w->hornRadiusCm * w->SampleRateD/100.0) / w->airSpeed;
  const double drumRadiusSamples = (w->drumRadiusCm * w->SampleRateD/100.0) / w->airSpeed;
  const double micDistSamples    = (w->micDistCm    * w->SampleRateD/100.0) / w->airSpeed;

  for (i = 0; i < DISPLC_SIZE; i++) {
    /* Compute angle around the circle */
    double v = (2.0 * M_PI * (double) i) / (double) DISPLC_SIZE;
    /* Distance between the mic and the rotor korda */
    double a = micDistSamples - (hornRadiusSamples * cos (v));
    /* Distance between rotor and mic-origin line */
    double b = hornRadiusSamples * sin (v);

    w->hnFwdDispl[i] = sqrt ((a * a) + (b * b));
    w->hnBwdDispl[DISPLC_SIZE - (i + 1)] = w->hnFwdDispl[i];

    a = micDistSamples - (drumRadiusSamples * cos (v));
    b = drumRadiusSamples * sin (v);
    w->drFwdDispl[i] = sqrt ((a * a) + (b * b));
    w->drBwdDispl[DISPLC_SIZE - (i + 1)] = w->drFwdDispl[i];
  }

  w->hornPhase[0] = 0;
  w->hornPhase[1] = DISPLC_SIZE >> 1;

  w->hornPhase[2] = ((DISPLC_SIZE * 2) / 6);
  w->hornPhase[3] = ((DISPLC_SIZE * 5) / 6);

  w->hornPhase[4] = ((DISPLC_SIZE * 1) / 6);
  w->hornPhase[5] = ((DISPLC_SIZE * 4) / 6);

  for (i = 0; i < 6; i++) {
    w->hornSpacing[i] = w->hornSpacing[i] * w->SampleRateD / 22100.0 + hornRadiusSamples + 1.0;
  }

  w->drumPhase[0] = 0;
  w->drumPhase[1] = DISPLC_SIZE >> 1;

  w->drumPhase[2] = ((DISPLC_SIZE * 2) / 6);
  w->drumPhase[3] = ((DISPLC_SIZE * 5) / 6);

  w->drumPhase[4] = ((DISPLC_SIZE * 1) / 6);
  w->drumPhase[5] = ((DISPLC_SIZE * 4) / 6);

  for (i = 0; i < 6; i++) {
    w->drumSpacing[i] = w->drumSpacing[i] * w->SampleRateD / 22100.0 + drumRadiusSamples + 1.0;
  }

  setIIRFilter (w->drfL, w->lpT, w->lpF, w->lpQ, w->lpG, w->SampleRateD);
  setIIRFilter (w->drfR, w->lpT, w->lpF, w->lpQ, w->lpG, w->SampleRateD);
  setIIRFilter (w->hafw, w->haT, w->haF, w->haQ, w->haG, w->SampleRateD);
  setIIRFilter (w->hbfw, w->hbT, w->hbF, w->hbQ, w->hbG, w->SampleRateD);

#ifdef HORN_COMB_FILTER
  w->cb0rp = &(w->comb0[COMB_SIZE - w->cb0dl]);
  w->cb0wp = &(w->comb0[0]);
  w->cb0bp = &(w->comb0[0]);
  w->cb0es = &(w->comb0[COMB_SIZE]);

  w->cb1rp = &(w->comb1[COMB_SIZE - w->cb1dl]);
  w->cb1wp = &(w->comb1[0]);
  w->cb1bp = &(w->comb1[0]);
  w->cb1es = &(w->comb1[COMB_SIZE]);
#endif

  ipx = ipy = 0.0;
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
      colsum += fabs (w->bfw[i].b[j]);
    }
    if (sum < colsum) {
      sum = colsum;
    }
  }
  /* Apply normalisation */
  for (i = 0; i < DISPLC_SIZE; i++) {
    for (j = 0; j < 5; j++) {
      w->bfw[i].b[j] *= 1.0 / sum;
      w->bbw[DISPLC_SIZE - i - 1].b[j] = w->bfw[i].b[j];
    }
  }

} /* initTables */

/*
 * Displays the settings of a filter.
 */
static void displayFilter (const char * id, int T, float F, float Q, float G) {
#if 0 // not in RT callback
  const char * type = eqGetTypeString (T);
  printf ("\n%s:T=%3.3s:F=%10.4f:Q=%10.4f:G=%10.4f", id, type, F, Q, G);
  fflush (stdout);
#endif
}

#define UPDATE_A_FILTER { \
  setIIRFilter (w->hafw, w->haT, w->haF, w->haQ, w->haG, w->SampleRateD); \
  displayFilter ("Horn A", w->haT, w->haF, w->haQ, w->haG); \
}

#define UPDATE_B_FILTER { \
  setIIRFilter (w->hbfw, w->hbT, w->hbF, w->hbQ, w->hbG, w->SampleRateD); \
  displayFilter ("Horn B", w->hbT, w->hbF, w->hbQ, w->hbG); \
}

#define UPDATE_D_FILTER { \
  setIIRFilter (w->drfL, w->lpT, w->lpF, w->lpQ, w->lpG, w->SampleRateD); \
  setIIRFilter (w->drfR, w->lpT, w->lpF, w->lpQ, w->lpG, w->SampleRateD); \
  displayFilter ("Drum", w->lpT, w->lpF, w->lpQ, w->lpG); \
}


/*
 * Sets the type of the A horn IIR filter.
 */
static void setHornFilterAType (void *d, unsigned char uc) {
  struct b_whirl *w = (struct b_whirl *) d;
  w->haT = (int) (uc / 15);
  UPDATE_A_FILTER;
}

void isetHornFilterAType (struct b_whirl *w, int v) {
  w->haT = (int) (v % 9);
  UPDATE_A_FILTER;
}

/*
 * Sets the cutoff frequency of the A horn IIR filter.
 */
static void setHornFilterAFrequency (void *d, unsigned char uc) {
  struct b_whirl *w = (struct b_whirl *) d;
  double u = (double) uc;
  double minv = 250.0;
  double maxv = 8000.0;
  w->haF = minv + ((maxv - minv) * ((u * u) / 16129.0));
  UPDATE_A_FILTER;
}

void fsetHornFilterAFrequency (struct b_whirl *w, float v) {
  if (v<250.0 || v> 8000.0) return;
  w->haF = v;
  UPDATE_A_FILTER;
}

/*
 * Sets the Q value of the A horn IIR filter.
 */
static void setHornFilterAQ (void *d, unsigned char uc) {
  struct b_whirl *w = (struct b_whirl *) d;
  double u = (double) uc;
  double minv = 0.01;
  double maxv = 6.00;
  w->haQ = minv + ((maxv - minv) * (u / 127.0));
  UPDATE_A_FILTER;
}

void fsetHornFilterAQ (struct b_whirl *w, float v) {
  if (v<0.01 || v> 6.0) return;
  w->haQ = v;
  UPDATE_A_FILTER;
}

/*
 * Sets the Gain value of the A horn IIR filter.
 */
static void setHornFilterAGain (void *d, unsigned char uc) {
  struct b_whirl *w = (struct b_whirl *) d;
  double u = (double) uc;
  double minv = -48.0;
  double maxv =  48.0;
  w->haG = minv + ((maxv - minv) * (u / 127.0));
  UPDATE_A_FILTER;
}

void fsetHornFilterAGain (struct b_whirl *w, float v) {
  if (v<-48.0 || v> 48.0) return;
  w->haG = v;
  UPDATE_A_FILTER;
}


static void setHornFilterBType (void *d, unsigned char uc) {
  struct b_whirl *w = (struct b_whirl *) d;
  w->hbT = (int) (uc / 15);
  UPDATE_B_FILTER;
}

void isetHornFilterBType (struct b_whirl *w, int v) {
  w->hbT = (int) (v % 9);
  UPDATE_B_FILTER;
}

static void setHornFilterBFrequency (void *d, unsigned char uc) {
  struct b_whirl *w = (struct b_whirl *) d;
  double u = (double) uc;
  double minv = 250.0;
  double maxv = 8000.0;
  w->hbF = minv + ((maxv - minv) * ((u * u) / 16129.0));
  UPDATE_B_FILTER;
}

void fsetHornFilterBFrequency (struct b_whirl *w, float v) {
  if (v<250.0 || v> 8000.0) return;
  w->hbF = v;
  UPDATE_B_FILTER;
}

static void setHornFilterBQ (void *d, unsigned char uc) {
  struct b_whirl *w = (struct b_whirl *) d;
  double u = (double) uc;
  double minv = 0.01;
  double maxv = 6.00;
  w->hbQ = minv + ((maxv - minv) * (u / 127.0));
  UPDATE_B_FILTER;
}

void fsetHornFilterBQ (struct b_whirl *w, float v) {
  if (v<0.01 || v> 6.0) return;
  w->hbQ = v;
  UPDATE_B_FILTER;
}

static void setHornFilterBGain (void *d, unsigned char uc) {
  struct b_whirl *w = (struct b_whirl *) d;
  double u = (double) uc;
  double minv = -48.0;
  double maxv =  48.0;
  w->hbG = minv + ((maxv - minv) * (u / 127.0));
  UPDATE_B_FILTER;
}

void fsetHornFilterBGain (struct b_whirl *w, float v) {
  if (v<-48.0 || v> 48.0) return;
  w->hbG = v;
  UPDATE_B_FILTER;
}

void isetDrumFilterType (struct b_whirl *w, int v) {
  w->lpT = (int) (v % 9);
  UPDATE_D_FILTER;
}

void fsetDrumFilterFrequency (struct b_whirl *w, float v) {
  if (v<20.0 || v> 8000.0) return;
  w->lpF = v;
  UPDATE_D_FILTER;
}

void fsetDrumFilterQ (struct b_whirl *w, float v) {
  if (v<0.01 || v> 6.0) return;
  w->lpQ = v;
  UPDATE_D_FILTER;
}

void fsetDrumFilterGain (struct b_whirl *w, float v) {
  if (v<-48.0 || v> 48.0) return;
  w->lpG = v;
  UPDATE_D_FILTER;
}

void setHornBreakPosition (void *d, unsigned char uc) {
  struct b_whirl *w = (struct b_whirl *) d;
  w->hnBreakPos = (double)uc/127.0;
}

void setDrumBreakPosition (void *d, unsigned char uc) {
  struct b_whirl *w = (struct b_whirl *) d;
  w->drBreakPos = (double)uc/127.0;
}

void setHornAcceleration (void *d, unsigned char uc) {
  struct b_whirl *w = (struct b_whirl *) d;
  w->hornAcc = .01 + (double)uc/80.0;
}

void setHornDeceleration (void *d, unsigned char uc) {
  struct b_whirl *w = (struct b_whirl *) d;
  w->hornDec = .01 + (double)uc/80.0;
}

void setDrumAcceleration (void *d, unsigned char uc) {
  struct b_whirl *w = (struct b_whirl *) d;
  w->drumAcc = .01 + (double)uc/14.0;
}

void setDrumDeceleration (void *d, unsigned char uc) {
  struct b_whirl *w = (struct b_whirl *) d;
  w->drumDec = .01 + (double)uc/14.0;
}

/*
 * This function initialises this module. It is run after whirlConfig.
 */
void initWhirl (struct b_whirl *w, void *m, double rate) {

  w->SampleRateD = rate;
  w->midi_cfg_ptr = m; // used for notify -- translate "rotary.speed-*"

  memset(w->HLbuf, 0, BUF_SIZE_SAMPLES);
  memset(w->HRbuf, 0, BUF_SIZE_SAMPLES);
  memset(w->DLbuf, 0, BUF_SIZE_SAMPLES);
  memset(w->DRbuf, 0, BUF_SIZE_SAMPLES);

  w->leakage = w->leakLevel * w->hornLevel;

  useMIDIControlFunction (m, "rotary.speed-toggle",    setWhirlSustainPedal, (void*)w);
  useMIDIControlFunction (m, "rotary.speed-preset",    revControl, (void*)w);
  useMIDIControlFunction (m, "rotary.speed-select",    revControlAll, (void*)w);

  useMIDIControlFunction (m, "whirl.horn.filter.a.type", setHornFilterAType, (void*)w);
  useMIDIControlFunction (m, "whirl.horn.filter.a.hz",   setHornFilterAFrequency, (void*)w);
  useMIDIControlFunction (m, "whirl.horn.filter.a.q",    setHornFilterAQ, (void*)w);
  useMIDIControlFunction (m, "whirl.horn.filter.a.gain", setHornFilterAGain, (void*)w);
  useMIDIControlFunction (m, "whirl.horn.filter.b.type", setHornFilterBType, (void*)w);
  useMIDIControlFunction (m, "whirl.horn.filter.b.hz",   setHornFilterBFrequency, (void*)w);
  useMIDIControlFunction (m, "whirl.horn.filter.b.q",    setHornFilterBQ, (void*)w);
  useMIDIControlFunction (m, "whirl.horn.filter.b.gain", setHornFilterBGain, (void*)w);

  useMIDIControlFunction (m, "whirl.horn.breakpos", setHornBreakPosition, (void*)w);
  useMIDIControlFunction (m, "whirl.drum.breakpos", setDrumBreakPosition, (void*)w);

  useMIDIControlFunction (m, "whirl.horn.acceleration", setHornAcceleration, (void*)w);
  useMIDIControlFunction (m, "whirl.horn.deceleration", setHornDeceleration, (void*)w);
  useMIDIControlFunction (m, "whirl.drum.acceleration", setDrumAcceleration, (void*)w);
  useMIDIControlFunction (m, "whirl.drum.deceleration", setDrumDeceleration, (void*)w);

  initTables (w);
  computeRotationSpeeds(w);
}

/*
 * Configuration interface.
 */
int whirlConfig (struct b_whirl *w, ConfigContext * cfg) {
  double d;
  int k;
  int rtn = 1;
  if (getConfigParameter_d ("whirl.horn.slowrpm", cfg, &d) == 1) {
    w->hornRPMslow = (float) d;
  }
  else if (getConfigParameter_d ("whirl.horn.fastrpm", cfg, &d) == 1) {
    w->hornRPMfast = (float) d;
  }
  else if (getConfigParameter_d ("whirl.horn.acceleration", cfg, &d) == 1) {
    w->hornAcc = (float) d;
  }
  else if (getConfigParameter_d ("whirl.horn.deceleration", cfg, &d) == 1) {
    w->hornDec = (float) d;
  }
  else if (getConfigParameter_d ("whirl.drum.slowrpm", cfg, &d) == 1) {
    w->drumRPMslow = (float) d;
  }
  else if (getConfigParameter_d ("whirl.drum.fastrpm", cfg, &d) == 1) {
    w->drumRPMfast = (float) d;
  }
  else if (getConfigParameter_d ("whirl.drum.acceleration", cfg, &d) == 1) {
    w->drumAcc = (float) d;
  }
  else if (getConfigParameter_d ("whirl.drum.deceleration", cfg, &d) == 1) {
    w->drumDec = (float) d;
  }
  else if (getConfigParameter_d ("whirl.horn.radius", cfg, &d) == 1) {
    w->hornRadiusCm = (float) d;
  }
  else if (getConfigParameter_d ("whirl.drum.radius", cfg, &d) == 1) {
    w->drumRadiusCm = (float) d;
  }
  else if (getConfigParameter_d ("whirl.horn.level", cfg, &d) == 1) {
    w->hornLevel = (float) d;
  }
  else if (getConfigParameter_d ("whirl.horn.leak", cfg, &d) == 1) {
    w->leakLevel = (float) d;
  }
  else if (getConfigParameter_d ("whirl.mic.distance", cfg, &d) == 1) {
    w->micDistCm = (float) d;
  }
  else if (getConfigParameter_ir
	   ("whirl.drum.filter.type", cfg, &k, 0, 8) == 1) {
    w->lpT = k;
  }
  else if (getConfigParameter_d ("whirl.drum.filter.q", cfg, &d) == 1) {
    w->lpQ =  d;
  }
  else if (getConfigParameter_d ("whirl.drum.filter.hz", cfg, &d) == 1) {
    w->lpF =  d;
  }
  else if (getConfigParameter_d ("whirl.drum.filter.gain", cfg, &d) == 1) {
    w->lpG =  d;
  }
  else if (getConfigParameter_ir
	   ("whirl.horn.filter.a.type", cfg, &k, 0, 8) == 1) {
    w->haT = k;
  }
  else if (getConfigParameter_d ("whirl.horn.filter.a.hz", cfg, &d) == 1) {
    w->haF = (double) d;
  }
  else if (getConfigParameter_d ("whirl.horn.filter.a.q", cfg, &d) == 1) {
    w->haQ = (double) d;
  }
  else if (getConfigParameter_d ("whirl.horn.filter.a.gain", cfg, &d) == 1) {
    w->haG = (double) d;
  }
  else if (getConfigParameter_ir
	   ("whirl.horn.filter.b.type", cfg, &k, 0, 8) == 1) {
    w->hbT = k;
  }
  else if (getConfigParameter_d ("whirl.horn.filter.b.hz", cfg, &d) == 1) {
    w->hbF = (double) d;
  }
  else if (getConfigParameter_d ("whirl.horn.filter.b.q", cfg, &d) == 1) {
    w->hbQ = (double) d;
  }
  else if (getConfigParameter_d ("whirl.horn.filter.b.gain", cfg, &d) == 1) {
    w->hbG = (double) d;
  }
  else if (getConfigParameter_d ("whirl.horn.comb.a.feedback", cfg, &d) == 1) {
    w->cb0fb = (double) d;
  }
  else if (getConfigParameter_i ("whirl.horn.comb.a.delay", cfg, &k) == 1) {
    w->cb0dl = k;
  }
  else if (getConfigParameter_d ("whirl.horn.comb.b.feedback", cfg, &d) == 1) {
    w->cb1fb = (double) d;
  }
  else if (getConfigParameter_i ("whirl.horn.comb.b.delay", cfg, &k) == 1) {
    w->cb1dl = k;
  }
  else if (getConfigParameter_i ("whirl.speed-preset", cfg, &k) == 1) {
    w->revSelect = k % revSelectEnd;
  }
  else if (getConfigParameter_ir ("whirl.bypass", cfg, &k, 0, 1) == 1) {
    w->bypass = k;
  }
  else if (getConfigParameter_dr ("whirl.horn.breakpos", cfg, &d, 0, 1.0) == 1) {
    w->hnBreakPos = (double) d;
  }
  else if (getConfigParameter_dr ("whirl.drum.breakpos", cfg, &d, 0, 1.0) == 1) {
    w->drBreakPos = (double) d;
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
void whirlProc2 (struct b_whirl *w,
		 const float * inbuffer,
		 float * outL,  float * outR,
		 float * outHL, float * outHR,
		 float * outDL, float * outDR,
		 size_t bufferLengthSamples) {

  const float * xp = inbuffer;
  unsigned int i;

  if (w->bypass) {
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
  if (w->hornAcDc) {
    const double l = exp(-1.0/(w->SampleRateD / bufferLengthSamples * (w->hornAcDc>0? w->hornAcc : w->hornDec )));
    w->hornIncrGRD += (1-l) * (w->hornTarget - w->hornIncrGRD);

    if (fabs(w->hornTarget - w->hornIncrGRD) < (1.0/360.0/w->SampleRateD) ) {
#ifdef DEBUG_SPEED
      printf("AcDc Horn off\n");
#endif
      w->hornAcDc = 0;
      w->hornIncrGRD = w->hornTarget;
    }
  }

  if (w->drumAcDc) {
    const double l = exp(-1.0/(w->SampleRateD / bufferLengthSamples * (w->drumAcDc>0? w->drumAcc: w->drumDec )));
    w->drumIncrGRD += (1-l) * (w->drumTarget - w->drumIncrGRD);

    if (fabs(w->drumTarget - w->drumIncrGRD) < (1.0/360.0/w->SampleRateD)) {
#ifdef DEBUG_SPEED
      printf("ACDC Drum off\n");
#endif
      w->drumAcDc = 0;
      w->drumIncrGRD = w->drumTarget;
    }
  }

#if 1
  /* break position -- don't stop anywhere..
     the original Leslie can not do this, sometimes the horn is aimed at the back of
     the cabinet when it comes to a halt, which results in a less than desirable sound.

     continue to slowly move the horn and drum to the center position after it actually
     came to a stop.
   */
  if (w->hnBreakPos>0) {
    const double targetPos= w->hnBreakPos - floor(w->hnBreakPos);
    if (!w->hornAcDc && w->hornIncrGRD==0 && w->hornAngleGRD!=targetPos) {
      w->hornAngleGRD += 1.0/400.0;
      w->hornAngleGRD = w->hornAngleGRD - floor(w->hornAngleGRD);
      if ((w->hornAngleGRD-targetPos) < (1.0/360.0)) w->hornAngleGRD=targetPos;
    }
  }
  if (w->drBreakPos>0) {
    const double targetPos= w->drBreakPos - floor(w->drBreakPos);
    if (!w->drumAcDc && w->drumIncrGRD==0 && w->drumAngleGRD!=targetPos) {
      w->drumAngleGRD += 1.0/400.0;
      w->drumAngleGRD = w->drumAngleGRD - floor(w->drumAngleGRD);
      if ((w->drumAngleGRD-targetPos) < (1.0/360.0)) w->drumAngleGRD=targetPos;
    }
  }
#endif

  /* localize struct variables */
  double hornAngleGRD = w->hornAngleGRD;
  double drumAngleGRD = w->drumAngleGRD;
  unsigned int outpos = w->outpos;

  const float leakage = w->leakage;
  const float hornLevel = w->hornLevel;
  const double hornIncrGRD = w->hornIncrGRD;
  const double drumIncrGRD = w->drumIncrGRD;

  const int * const hornPhase = w->hornPhase;
  const int * const drumPhase = w->drumPhase;
  const float * const hornSpacing = w->hornSpacing;
  const float * const drumSpacing = w->drumSpacing;
  const float * const hnFwdDispl = w->hnFwdDispl;
  const float * const hnBwdDispl = w->hnBwdDispl;
  const float * const drFwdDispl = w->drFwdDispl;
  const float * const drBwdDispl = w->drBwdDispl;

  float * const hafw = w->hafw;
  float * const hbfw = w->hbfw;
  float * const HLbuf = w->HLbuf;
  float * const HRbuf = w->HRbuf;
  float * const DLbuf = w->DLbuf;
  float * const DRbuf = w->DRbuf;
  float * const adx0 = w->adx0;
  float * const adx1 = w->adx1;
  float * const adx2 = w->adx2;
  float * const drfL = w->drfL;
  float * const drfR = w->drfR;
  float * const z = w->z;

  const struct _bw * const bfw = w->bfw;
  const struct _bw * const bbw = w->bfw;


  int hornAngle = hornAngleGRD * DISPLC_SIZE;
  int drumAngle = drumAngleGRD * DISPLC_SIZE;

#ifdef DEBUG_SPEED
  char const * const acdc[3]= {"<","#",">"};
  static int fgh=0;
  if ((fgh++ % (int)(w->SampleRateD/128/5) ) ==0) {
    printf ("H:%.3f D:%.3f | HS:%.3f DS:%.3f [Hz]| HT:%.2f DT:%.2f [Hz]| %s %s\n",
	(double)hornAngle/DISPLC_SIZE, (double)drumAngle/DISPLC_SIZE,
	w->SampleRateD*(double)hornIncrGRD, w->SampleRateD*(double)drumIncrGRD,
	w->SampleRateD*(double)w->hornTarget, w->SampleRateD*(double)w->drumTarget,
	acdc[w->hornAcDc+1], acdc[w->drumAcDc+1]
	);
  }
#endif

  /* process each sample */
  for (i = 0; i < bufferLengthSamples; i++) {
    unsigned int k;
    unsigned int n;
    float q;
    float r;
    float t;
    float x = (float) (*xp++) + DENORMAL_HACK;
    float xa;
    float xx = x;
    float leak = 0;

    /* 0) define macros for code below */
#define ADDHIST(DX,DI,XS) {      \
    DI = (DI + AGMASK) & AGMASK; \
    DX[DI] = XS;}

#define HN_MOTION(P,BUF,DSP,BW,DX,DI) {                     \
    k = ((hornAngle + hornPhase[(P)]) & DISPLC_MASK);       \
    t = hornSpacing[(P)] + DSP[k] + (float) outpos;         \
    r = floorf (t);                                         \
    xa  = BW[k].b[0] * x;                                   \
    xa += BW[k].b[1] * DX[(DI)];                            \
    xa += BW[k].b[2] * DX[((DI)+1) & AGMASK];               \
    xa += BW[k].b[3] * DX[((DI)+2) & AGMASK];               \
    xa += BW[k].b[4] * DX[((DI)+3) & AGMASK];               \
    q = xa * (t - r);                                       \
    n = ((unsigned int) r) & BUF_MASK_SAMPLES;              \
    BUF[n] += xa - q;                                       \
    n = (n + 1) & BUF_MASK_SAMPLES;                         \
    BUF[n] += q;}

#define DR_MOTION(P,BUF,DSP) {                              \
    k = ((drumAngle + drumPhase[(P)]) & DISPLC_MASK);       \
    t = drumSpacing[(P)] + DSP[k] + (float) outpos;         \
    r = floorf (t);                                         \
    q = x * (t - r);                                        \
    n = ((unsigned int) r) & BUF_MASK_SAMPLES;              \
    BUF[n] += x - q;                                        \
    n = (n + 1) & BUF_MASK_SAMPLES;                         \
    BUF[n] += q;}

    /* This is just a bum filter to take some high-end off. */
#define FILTER_C(W0,W1,I) {           \
    float temp = x;                   \
    x = ((W0) * x) + ((W1) * z[(I)]); \
    z[(I)] = temp; }

#define EQ_IIR(W,X,Y) {                                     \
    float temp = (X) - (W[a1] * W[z0]) - (W[a2] * W[z1]);   \
    Y = (temp * W[b0]) + (W[b1] * W[z0]) + (W[b2] * W[z1]); \
    W[z1] = W[z0]; \
    W[z0] = temp;}

#ifdef HORN_COMB_FILTER

#define COMB(WP,RP,BP,ES,FB,X) { \
    X += ((*(RP)++) * (FB));     \
    *(WP)++ = X;                 \
    if ((RP) == (ES)) RP = BP;   \
    if ((WP) == (ES)) WP = BP;}

#endif

    /* 1) apply filters A,B -- horn-speaker characteristics
     * input: x
     * output: x', leak
     */

    EQ_IIR(hafw, x, x);
    EQ_IIR(hbfw, x, x);

    leak = x * leakage;

#ifdef HORN_COMB_FILTER
    /* only causes hiss-noise - in particular on 'E-4,F-4' ~660Hz
     * no audible benefit to leslie effect so far, needs tweaking
     */
    COMB(w->cb0wp, w->cb0rp, w->cb0bp, w->cb0es, w->cb0fb, x);
    COMB(w->cb1wp, w->cb1rp, w->cb1bp, w->cb1es, w->cb1fb, x);
#endif

    /* 2) now do doppler shift for the horn -- FM
     * input: x' (filtered x)
     * output: HLbuf, HRbuf, leak
     */

    /* --- STATIC HORN FILTER --- */
    /* HORN PRIMARY */
    HN_MOTION(0, HLbuf, hnFwdDispl, bfw, adx0, w->adi0);
    HN_MOTION(1, HRbuf, hnBwdDispl, bbw, adx0, w->adi0);
    ADDHIST(adx0, w->adi0, x);

    /* HORN FIRST REFLECTION FILTER */
    FILTER_C(0.4, 0.4, 0);

    /* HORN FIRST REFLECTION */
    HN_MOTION(2, HLbuf, hnBwdDispl, bbw, adx1, w->adi1);
    HN_MOTION(3, HRbuf, hnFwdDispl, bfw, adx1, w->adi1);
    ADDHIST(adx1, w->adi1, x);

    /* HORN SECOND REFLECTION FILTER */
    FILTER_C(0.4, 0.4, 1);

    /* HORN SECOND REFLECTION */
    HN_MOTION(4, HLbuf, hnFwdDispl, bfw, adx2, w->adi2);
    HN_MOTION(5, HRbuf, hnBwdDispl, bbw, adx2, w->adi2);
    ADDHIST(adx2, w->adi2, x);

    /* 1A) do doppler shift for drum (actually orig signal -- FM
     * input: x
     * output: DLbuf, DRbuf
     */

    x = xx; // use original input signal ('x' was modified by horn filters)

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
	*outL++ = y + hornLevel * HLbuf[outpos] + leak;
      if (outHL)
	*outHL++ = hornLevel * HLbuf[outpos] + leak;
      if (outDL)
	*outDL++ = y;

      EQ_IIR(drfR, DRbuf[outpos], y);
      if (outR)
	*outR++ =  y + hornLevel * HRbuf[outpos] + leak;
      if (outHR)
	*outHR++ = hornLevel * HRbuf[outpos] + leak;
      if (outDR)
	*outDR++ = y;
    }

    HLbuf[outpos] = 0.0;
    HRbuf[outpos] = 0.0;
    DLbuf[outpos] = 0.0;
    DRbuf[outpos] = 0.0;

    /* rotate speakers */

    outpos = (outpos + 1) & BUF_MASK_SAMPLES;

    hornAngleGRD = (hornAngleGRD + hornIncrGRD);
    hornAngleGRD = hornAngleGRD - floor(hornAngleGRD); // limit to [0..1]
    hornAngle = hornAngleGRD * DISPLC_SIZE;

    drumAngleGRD = (drumAngleGRD + drumIncrGRD);
    drumAngleGRD = drumAngleGRD - floor(drumAngleGRD); // limit to [0..1]
    drumAngle = drumAngleGRD * DISPLC_SIZE;
  }

  /* copy back variables */
  w->hornAngleGRD = hornAngleGRD;
  w->drumAngleGRD = drumAngleGRD;
  w->outpos = outpos;
}

void whirlProc (struct b_whirl *w,
		       const float * inbuffer,
		       float * outbL,
		       float * outbR,
		       size_t bufferLengthSamples)
{
  whirlProc2(w, inbuffer, outbL, outbR,
      NULL, NULL,
      NULL, NULL,
      bufferLengthSamples);
}

/* vi:set ts=8 sts=2 sw=2: */
