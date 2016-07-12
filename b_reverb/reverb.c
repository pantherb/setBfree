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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * effect initialization is done in 3 steps:
 * - allocReverb()   -- prepare instance, initialize default config
 * - reverbConfig()  -- set configuration variables (optional)
 * - initReverb()    -- derive static variables from cfg, allocate buffers
 *   Note: SampleRate is unknown until step (3) initReverb()
 *
 *
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
 *    https://ccrma.stanford.edu/~jos/waveguide/Schroeder_Reverberators.html
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
#ifndef CONFIGDOCONLY

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#include "reverb.h"
#include "../src/midi.h" // useMIDIControlFunction

struct b_reverb *allocReverb() {
  int i;
  struct b_reverb *r = (struct b_reverb*) calloc(1, sizeof(struct b_reverb));
  if (!r) {
    return NULL;
    fprintf (stderr, "FATAL: memory allocation failed for reverb.\n");
    exit(1);
  }
  r->inputGain = 0.1;	/* Input gain value */
  r->fbk = -0.015;	/* Feedback gain */
  r->wet = 0.1;		/* Output dry gain */
  r->dry = 0.9;		/* Output wet gain */

  /* These are all  1/sqrt(2) = 0.7071067811865475 */
  r->gain[0] = sqrtf(0.5);	/* FBCF (feedback combfilter) */
  r->gain[1] = sqrtf(0.5);	/* FBCF */
  r->gain[2] = sqrtf(0.5);	/* FBCF */
  r->gain[3] = sqrtf(0.5);	/* FBCF */

  r->gain[4] = sqrtf(0.5);	/* AP (all-pass filter) */
  r->gain[5] = sqrtf(0.5);	/* AP */
  r->gain[6] = sqrtf(0.5);	/* AP */

  /* delay lines */
  r->end[0] = 2999;
  r->end[1] = 2331;
  r->end[2] = 1893;
  r->end[3] = 1097;

  /* all pass filters */
  r->end[4] = 1051;
  r->end[5] = 337;
  r->end[6] = 113;

  for (i=0; i< RV_NZ; ++i) {
    r->delays[i]= NULL;
  }

  r->yy1 = 0.0;
  r->y_1 = 0.0;

  return r;
}

void freeReverb(struct b_reverb *r) {
  int i;
  for (i = 0; i < RV_NZ; ++i) {
    free(r->delays[i]);
  }
  free(r);
}

/* used during initialization, set array end pointers */
static void setReverbPointers (struct b_reverb *r, int i) {
  if ((0 <= i) && (i < RV_NZ)) {
    int e = (r->end[i] * r->SampleRateD / 22050.0);
    e = e | 1;
    r->delays[i] = (float *) realloc((void *)r->delays[i], (e + 2) * sizeof(float));
    if (!r->delays[i]) {
      fprintf (stderr, "FATAL: memory allocation failed for reverb.\n");
      exit(1);
    } else {
      memset(r->delays[i], 0 , (e + 2) * sizeof(float));
    }
    r->endp[i] = r->delays[i] + e + 1;
    r->idx0[i] = r->idxp[i] = &(r->delays[i][0]);
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

void setReverbMixFromMIDI (void *rev, unsigned char v) {
  struct b_reverb *r = (struct b_reverb *) rev;
  setReverbMix(r, (float)v/127.0);
}


#if 0 // unused
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
    if (0 <= s) {
      r->end[i] = s;
      setReverbPointers (r, i);
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
#endif

/*
 *
 */
int reverbConfig (struct b_reverb *r, ConfigContext * cfg) {
  int ack = 1;
  double d;
  if (getConfigParameter_d ("reverb.wet", cfg, &d) == 1) {
    r->wet = d;
  }
  else if (getConfigParameter_d ("reverb.dry", cfg, &d) == 1) {
    r->dry = d;
  }
  else if (getConfigParameter_d ("reverb.inputgain", cfg, &d) == 1) {
    setReverbInputGain (r, (float) d);
  }
  else if (getConfigParameter_d ("reverb.outputgain", cfg, &d) == 1) {
    setReverbOutputGain (r, (float) d);
  }
  else if (getConfigParameter_dr ("reverb.mix", cfg, &d, 0, 1.0) == 1) {
    setReverbMix (r, (float) d);
  }
  else {
    ack=0;
  }
  return ack;
}


/*
 *
 */
void initReverb (struct b_reverb *r, void *m, double rate) {
  int i;
  r->SampleRateD = rate;
  for (i = 0; i < RV_NZ; i++) {
    setReverbPointers (r, i);
  }
  setReverbInputGain (r, r->inputGain);
  useMIDIControlFunction (m, "reverb.mix", setReverbMixFromMIDI, r);
}

/*
 *
 */
float * reverb (struct b_reverb *r,
		const float * inbuf,
		float * outbuf,
		size_t bufferLengthSamples)
{
  float ** const idxp = r->idxp;
  float * const * const endp = r->endp;
  float * const * const idx0 = r->idx0;
  const float * const gain = r->gain;
  const float inputGain = r->inputGain;
  const float fbk = r->fbk;
  const float wet = r->wet;
  const float dry = r->dry;

  unsigned int i;
  const float * xp =  inbuf;
  float * yp =  outbuf;

  float y_1 = r->y_1;
  float yy1 = r->yy1;

  for (i = 0; i < bufferLengthSamples; i++) {
    int j;
    float y;
    const float xo = (*xp++);
    const float x = y_1 + (inputGain * xo);
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

  r->y_1 = y_1 + DENORMAL_HACK;
  r->yy1 = yy1 + DENORMAL_HACK;
  return outbuf;
}


#else
# include "cfgParser.h"
#endif // CONFIGDOCONLY

static const ConfigDoc doc[] = {
  {"reverb.wet", CFG_DOUBLE, "0.1", "Reverb Wet signal level; range [0..1]", INCOMPLETE_DOC},
  {"reverb.dry", CFG_DOUBLE, "0.9", "Reverb Dry signal level; range [0..1]", INCOMPLETE_DOC},
  {"reverb.inputgain", CFG_DOUBLE, "0.1", "Reverb Input Gain", "dB", 0.01, 0.5, 2.0},
  {"reverb.outputgain", CFG_DOUBLE, "1.0", "Reverb Output Gain (modifies dry/wet)", INCOMPLETE_DOC},
  {"reverb.mix", CFG_DOUBLE, "0.1", "Reverb Mix (modifies dry/wet).", INCOMPLETE_DOC},
  DOC_SENTINEL
};

const ConfigDoc *reverbDoc () {
  return doc;
}

/* vi:set ts=8 sts=2 sw=2: */
