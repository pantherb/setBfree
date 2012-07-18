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

#define NZ 7

static int end[NZ] = {
  2999,
  2331,
  1893,
  1097,

  1051,
  337,
  113
};

/* These are all  1/sqrt(2) = 0.7071067811865475 */

static float gain[NZ] = {
  0.7071067811865475,		/* FBCF (feedback combfilter) */
  0.7071067811865475,		/* FBCF */
  0.7071067811865475,		/* FBCF */
  0.7071067811865475,		/* FBCF */

  0.7071067811865475,		/* AP (all-pass filter) */
  0.7071067811865475,		/* AP */
  0.7071067811865475		/* AP */
};

static float delays[NZ][MAXDELAY];

static float * idx0[NZ];	/* Reset pointer */
static float * idxp[NZ];	/* Index pointer */
static float * endp[NZ];	/* End pointer */

static float inputGain   = 0.025;	/* Input gain value */

static float fbk = -0.015;	/* Feedback gain */
static float wet = 0.3;		/* Output dry gain */
static float dry = 0.7;		/* Output wet gain */

/*
 *
 */
static void setReverbEndPointer (int i) {
  if ((0 <= i) && (i < NZ)) {
    int e = (end[i] * SampleRateD / 22050.0);
    endp[i] = idx0[i] + e + 1;
  }
}

/*
 *
 */
void setReverbInputGain (float g) {
  inputGain = g;
}

/*
 *
 */
void setReverbOutputGain (float g) {
  float u = wet + dry;
  wet = g * (wet / u);
  dry = g * (dry / u);
}

/*
 * @param g  0.0 Dry ... 1.0 wet
 */
void setReverbMix (float g) {
  float u = wet + dry;
  wet = g * u;
  dry = u - (g * u);
}

/*
 *
 */
void setReverbFeedbackGainInDelay (int i, float g) {
  if ((0 <= i) && (i < NZ)) {
    gain[i] = g;
  }
}

/*
 *
 */
void setReverbSamplesInDelay (int i, int s) {
  if ((0 <= i) && (i < NZ)) {
    if ((0 <= s) && (s < MAXDELAY)) {
      end[i] = s;
      setReverbEndPointer (i);
    }
  }
}

/*
 *
 */
void setReverbFeedbackGain (float g) {
  fbk = g;
}

/*
 *
 */
void setReverbDry (float g) {
  dry = g;
}

/*
 *
 */
void setReverbWet (float g) {
  wet = g;
}

/*
 *
 */
int reverbConfig (ConfigContext * cfg) {
  int ack = 0;
  if (strcasecmp (cfg->name, "reverb.wet") == 0) {
    double v;
    if (sscanf (cfg->value, "%lf", &v) == 1) {
      wet = v;
      ack++;
    }
  }
  else if (strcasecmp (cfg->name, "reverb.dry") == 0) {
    double v;
    if (sscanf (cfg->value, "%lf", &v) == 1) {
      dry = v;
      ack++;
    }
  }
  else if (strcasecmp (cfg->name, "reverb.inputgain") == 0) {
    double v;
    if (sscanf (cfg->value, "%lf", &v) == 1) {
      setReverbInputGain ((float) v);
      ack++;
    }
  }
  else if (strcasecmp (cfg->name, "reverb.outputgain") == 0) {
    double v;
    if (sscanf (cfg->value, "%lf", &v) == 1) {
      setReverbOutputGain ((float) v);
      ack++;
    }
  }
  else if (strcasecmp (cfg->name, "reverb.mix") == 0) {
    double v;
    if (sscanf (cfg->value, "%lf", &v) == 1) {
      if ((0 <= v) && (v <= 1.0)) {
	setReverbMix ((float) v);
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
void initReverb () {
  int i;
  for (i = 0; i < NZ; i++) {
    idx0[i] = idxp[i] = &(delays[i][0]);
    setReverbEndPointer (i);
  }
  setReverbInputGain (inputGain);
}

/*
 *
 */
float * reverb (const float * inbuf,
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
    float x = y_1 + (inputGain * xo);
    float xa = 0.0;

    /* First we do four feedback comb filters (ie parallel delay lines,
       each with a single tap at the end that feeds back at the start) */

    for (j = 0; j < 4; j++) {
      y = (*idxp[j]);
      *idxp[j] = x + (gain[j] * y);
      if (endp[j] <= ++(idxp[j])) idxp[j] = idx0[j];
      xa += y;
    }

    for (; j < 7; j++) {
      y = (*idxp[j]);
      *idxp[j] = gain[j] * (xa + y);
      if (endp[j] <= ++(idxp[j])) idxp[j] = idx0[j];
      xa = y - xa;
    }

    y = 0.5 * (xa + yy1);
    yy1 = y;
    y_1 = fbk * xa;

    *yp++ = ((wet * y) + (dry * xo));
  }
  return outbuf;
}
/* vi:set ts=8 sts=2 sw=2: */
