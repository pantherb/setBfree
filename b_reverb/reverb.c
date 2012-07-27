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

/* reverb.c
 * Fredrik Kilander
 * 12-apr-2003/FK Adapted from springReverb2.c
 * The reverb algorithm was pieced together from two sources:
 *
 * 1. Philip Edelbrock, 'Room Acoustics Modeling',
 *    http://pefarina.eng.unipr.it/Aurora/SAW/RoomSim.html
 *
 *    All-pass filter implementation; placing the all-pass filters after
 *    the delay lines.
 *
 * 2. Julius O. Smith III, 'Digital Waveguide Modeling of Musical Instruments',
 *    'A Schroeder Reverberator called JCRev',
 *    http://www-ccrma.stanford.edu/~jos/waveguide/Schroeder_Reverberator...
 *
 *    All-pass filter delay lengths.
 *
 * The end result has a schematic like this:
 *
 *   +-----[ D0 ]-------+
 *   | 	 	       	|
 *   +-----[ D1 ]-------+
 *   | 		       (+)---[AP4]---[AP5]---[AP6]-----+
 *   +-----[ D2 ]-------+			       |
 *   | 	 	      	|			       |
 *   +-----[ D3 ]-------+			       |
 *   | 	 	      				       |
 *  (+)-----------------------------------<feedback]---+
 *   |		       	       	       	       	       |
 *  /\						     [LPF]
 *  inputGain					       |
 *  --						      ---
 *   |						      wet
 *   |						      \/
 *   |						       |
 *   +-----------------------------------[dry>--------(+)
 *   |						       |
 *  /\						      ---
 *  inputNormalization				outputScaling
 *  --						      \/
 *   |						       |
 *   x						       y
 *
 * The delays (D0, D1, D2, D3) are structured thus:
 *
 * x---(+)--[ Dn ]--+---y
 *      |           |
 *      +----<Gn]---+
 *
 * Index numbers 0, 1, 2 and 3 refers to the delays.
 *
 * The all-pass filters AP4, AP5, AP6:
 *
 *    +----------[-1>-----------+
 *    |                         |
 * x--+--(+)--[Gn>--[ Dn ]--+--(+)--y
 *        |                 |
 *        +-----<+1]--------+
 *
 * Index numbers 4, 5 and 6 refers to the all-pass filters.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#include "reverb.h"

#define MAXDELAY 16384 // TODO alloc dynamically size dependend on SampleRate
extern double SampleRateD;


struct b_reverb *allocReverb() {
  int i;
  struct b_reverb *r = (struct b_reverb*) calloc(1, sizeof(struct b_reverb));
  if (!r) {
    return NULL;
    fprintf (stderr, "FATAL: memory allocation failed for reverb.\n");
    exit(1);
  }
  r->inputGain = 0.025;	/* Input gain value */
  r->fbk = -0.015;	/* Feedback gain */
  r->wet = 0.3;		/* Output dry gain */
  r->dry = 0.7;		/* Output wet gain */

  /* These are all  1/sqrt(2) = 0.7071067811865475 */
  r->gain[0] = 0.7071067811865475;	/* FBCF (feedback combfilter) */
  r->gain[1] = 0.7071067811865475;	/* FBCF */
  r->gain[2] = 0.7071067811865475;	/* FBCF */
  r->gain[3] = 0.7071067811865475;	/* FBCF */

  r->gain[4] = 0.7071067811865475;	/* AP (all-pass filter) */
  r->gain[5] = 0.7071067811865475;	/* AP */
  r->gain[6] = 0.7071067811865475;	/* AP */

  r->end[0] = 2999;
  r->end[1] = 2331;
  r->end[2] = 1893;
  r->end[3] = 1097;

  r->end[4] = 1051;
  r->end[5] = 337;
  r->end[6] = 113;

  for (i=0; i< RV_NZ; ++i) {
    r->delays[i]= calloc(MAXDELAY, sizeof(float));
    if (!r->delays[i]) {
      fprintf (stderr, "FATAL: memory allocation failed for reverb.\n");
      exit(1);
    }
  }
  return r;
}

void freeReverb(struct b_reverb *r) {
  int i;
  for (i=0; i< RV_NZ; ++i) {
    free(r->delays[i]);
  }
  free(r);
}

/*
 *
 */
static void setReverbEndPointer (struct b_reverb *r, int i) {
  if ((0 <= i) && (i < RV_NZ)) {
    int e = (r->end[i] * SampleRateD / 22050.0);
    r->endp[i] = r->idx0[i] + e + 1;
  }
}

/*
 *
 */
void setReverbInputGain (struct b_reverb *r, float g) {
  r->inputGain = g;
}

/*
 *
 */
void setReverbOutputGain (struct b_reverb *r, float g) {
  float u = r->wet + r->dry;
  r->wet = g * (r->wet / u);
  r->dry = g * (r->dry / u);
}

/*
 * @param g  0.0 Dry ... 1.0 wet
 */
void setReverbMix (struct b_reverb *r, float g) {
  float u = r->wet + r->dry;
  r->wet = g * u;
  r->dry = u - (g * u);
}

/*
 *
 */
void setReverbFeedbackGainInDelay (struct b_reverb *r, int i, float g) {
  if ((0 <= i) && (i < RV_NZ)) {
    r->gain[i] = g;
  }
}

/*
 *
 */
void setReverbSamplesInDelay (struct b_reverb *r, int i, int s) {
  if ((0 <= i) && (i < RV_NZ)) {
    if ((0 <= s) && (s < MAXDELAY)) {
      r->end[i] = s;
      setReverbEndPointer (r, i);
    }
  }
}

/*
 *
 */
void setReverbFeedbackGain (struct b_reverb *r, float g) {
  r->fbk = g;
}

/*
 *
 */
void setReverbDry (struct b_reverb *r, float g) {
  r->dry = g;
}

/*
 *
 */
void setReverbWet (struct b_reverb *r, float g) {
  r->wet = g;
}

/*
 *
 */
int reverbConfig (struct b_reverb *r, ConfigContext * cfg) {
  int ack = 0;
  if (strcasecmp (cfg->name, "reverb.wet") == 0) {
    double v;
    if (sscanf (cfg->value, "%lf", &v) == 1) {
      r->wet = v;
      ack++;
    }
  }
  else if (strcasecmp (cfg->name, "reverb.dry") == 0) {
    double v;
    if (sscanf (cfg->value, "%lf", &v) == 1) {
      r->dry = v;
      ack++;
    }
  }
  else if (strcasecmp (cfg->name, "reverb.inputgain") == 0) {
    double v;
    if (sscanf (cfg->value, "%lf", &v) == 1) {
      setReverbInputGain (r, (float) v);
      ack++;
    }
  }
  else if (strcasecmp (cfg->name, "reverb.outputgain") == 0) {
    double v;
    if (sscanf (cfg->value, "%lf", &v) == 1) {
      setReverbOutputGain (r, (float) v);
      ack++;
    }
  }
  else if (strcasecmp (cfg->name, "reverb.mix") == 0) {
    double v;
    if (sscanf (cfg->value, "%lf", &v) == 1) {
      if ((0 <= v) && (v <= 1.0)) {
	setReverbMix (r, (float) v);
	ack++;
      }
    }
  }

  return ack;
}

static const ConfigDoc doc[] = {
  {"reverb.wet", CFG_DOUBLE, "0.3", "Wet signal level; range [0..1]"},
  {"reverb.dry", CFG_DOUBLE, "0.7", "Dry signal level; range [0..1]"},
  {"reverb.inputgain", CFG_DOUBLE, "0.025", "Input Gain; range [0..1]"},
  {"reverb.outputgain", CFG_DOUBLE, "1.0", "Note: modifies dry/wet."},
  {"reverb.mix", CFG_DOUBLE, "0.3", "Note: modifies dry/wet."},
  {NULL}
};

const ConfigDoc *reverbDoc () {
  return doc;
}


/*
 *
 */
void initReverb (struct b_reverb *r) {
  int i;
  for (i = 0; i < RV_NZ; i++) {
    r->idx0[i] = r->idxp[i] = &(r->delays[i][0]);
    setReverbEndPointer (r, i);
  }
  setReverbInputGain (r, r->inputGain);
}

/*
 *
 */
float * reverb (struct b_reverb *r,
		const float * inbuf,
		float * outbuf,
		size_t bufferLengthSamples)
{
  int i;
  const float * xp =  inbuf;
  float * yp =  outbuf;
  static float yy1 = 0;		/* Previous output sample */
  static float y_1 = 0;		/* Feedback sample */

  for (i = 0; i < bufferLengthSamples; i++) {
    int j;
    float y;
    float xo = (*xp++);
    float x = y_1 + (r->inputGain * xo);
    float xa = 0.0;

    /* First we do four feedback comb filters (ie parallel delay lines,
       each with a single tap at the end that feeds back at the start) */

    for (j = 0; j < 4; j++) {
      y = (*r->idxp[j]);
      *r->idxp[j] = x + (r->gain[j] * y);
      if (r->endp[j] <= ++(r->idxp[j])) r->idxp[j] = r->idx0[j];
      xa += y;
    }

    for (; j < 7; j++) {
      y = (*r->idxp[j]);
      *r->idxp[j] = r->gain[j] * (xa + y);
      if (r->endp[j] <= ++(r->idxp[j])) r->idxp[j] = r->idx0[j];
      xa = y - xa;
    }

    y = 0.5 * (xa + yy1);
    yy1 = y;
    y_1 = r->fbk * xa;

    *yp++ = ((r->wet * y) + (r->dry * xo));
  }
  return outbuf;
}
/* vi:set ts=8 sts=2 sw=2: */
