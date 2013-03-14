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

/*
 * vibrato.c
 * Vibrato scanner simulator.
 * 25-oct-2004/FK Updated default settings.
 * 26-sep-2004/FK Comment management.
 * 15-aug-2004/FK Adapted from scanner.c for float samples. Disabled
 *                the effect of the effectEnabled flag. Control is now
 *                wielded by the caller (as it should be).
 * 04-apr-2004/FK This must be the oldest piece of code that remains.
 *                Added MIDI controller support and edited some comments.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

#include "main.h"
#include "tonegen.h"
#include "cfgParser.h"
#include "midi.h"
#include "vibrato.h"

/*
 * The increment is encoded using a 16-bit fixed-point mantissa.
 * This means that an increment of 1.0 is represented by 2^16, or (1<<16).
 * This quantity is added to inPos using ordinary arithmetic.
 *
 * When writing at inPos, the position is divided into (h.l).
 * The integer part, h, is the first sample to write to. It receives
 * x(1.0 - l) + y(h) of the input sample. That is, we add the fraction to
 * the value already present. The next sample, receives x*l + y(h+1).
 * In order to prevent repeated echoes, the buffer is cleared by the
 * read operation.
 *
 * We need to translate these values into fixed-point integer aritmetic.
 * Since the fraction is 16-bit, they are represented by l/(2^16).
 * Thus we do (x * l) >> 16 to compute (x * l).
 * Having that, we simply do  x - xl to arrive at the value to add to x(h).
 *
 * In order to avoid a buffer overrun at (h+1), we must use a temporary
 * variable for the address of (h+1) and wrap it back to 0 if it equals
 * the length of the buffer.
 *
 * Increments less than 1.0 will be perceived as a rise in pitch, because
 * they are the ones first seen by the reader, and the energy will be
 * compressed into fewer samples than in the source. Conversely, increments
 * above 1.0 will be perceived as a drop in pitch, because the information
 * tries to escape from the reader, and the energy is distributed across
 * more samples than in the source.
 *
 * Increments must be less than 2.0, or some samples will be skipped which
 * will cause nasty clicks or noise, depending on the output DACs.
 */

#define INCTBL_MASK 0x07ffffff	/* Fixed point */
#define BUF_MASK_SAMPLES 0x000003FF
#define BUF_MASK_POSN    0x03FFFFFF /* Fixed-point mask */

/*
 * Sets the scanner frequency. It operates at a fixed frequency beacuse
 * it is driven from the tonegenerator motor which runs at a steady 1200
 * or 1500 rpm (50 Hz models). The usual frequency is somewhere between
 * 7 or 8 Hz.
 */
static void setScannerFrequency (struct b_vibrato *v, double Hertz) {
  v->vibFqHertz = Hertz;
  v->statorIncrement =
    (unsigned int) (((v->vibFqHertz * INCTBL_SIZE) / SampleRateD) * 65536.0);
}

/*
 * Controls the amount of vibrato to apply by selecting the proper lookup
 * table for the processing routine.
 */
void setVibrato (void *t, int select) {
  struct b_vibrato *v = &(((struct b_tonegen*)t)->inst_vibrato);

  switch (select & 3) {

  case 0:			/* disable */
    v->effectEnabled = FALSE;
    break;

  case 1:
    v->effectEnabled = TRUE;
    v->offsetTable = v->offset1Table;
    break;

  case 2:
    v->effectEnabled = TRUE;
    v->offsetTable = v->offset2Table;
    break;

  case 3:
    v->effectEnabled = TRUE;
    v->offsetTable = v->offset3Table;
    break;
  }

  v->mixedBuffers = select & CHO_;
}

/*
 * Implements the vibrato knob response to a MIDI controller.
 */
static void setVibratoFromMIDI (void *t, unsigned char u) {
  switch (u / 22) {
  case 0:
    setVibrato (t, VIB1);
    break;
  case 1:
    setVibrato (t, CHO1);
    break;
  case 2:
    setVibrato (t, VIB2);
    break;
  case 3:
    setVibrato (t, CHO2);
    break;
  case 4:
    setVibrato (t, VIB3);
    break;
  case 5:
    setVibrato (t, CHO3);
    break;
  }
}

/*
 * Vibrato routing.
 */
static void setVibratoRoutingFromMIDI (void *t, unsigned char uc) {
  struct b_tonegen * inst_synth = (struct b_tonegen *) t;
  switch (uc / 32) {
  case 0:
    setVibratoUpper (inst_synth, FALSE);
    setVibratoLower (inst_synth, FALSE);
    break;
  case 1:
    setVibratoUpper (inst_synth, FALSE);
    setVibratoLower (inst_synth, TRUE);
    break;
  case 2:
    setVibratoUpper (inst_synth, TRUE);
    setVibratoLower (inst_synth, FALSE);
    break;
  case 3:
    setVibratoUpper (inst_synth, TRUE);
    setVibratoLower (inst_synth, TRUE);
    break;
  }
}

/*
 * Initialises tables.
 */
static void initIncrementTables (struct b_vibrato *v) {
  int i;
  double S = 65536.0;
  double voff1;
  double voff2;
  double voff3;

  voff1 = v->vib1OffAmp;
  voff2 = v->vib2OffAmp;
  voff3 = v->vib3OffAmp;

  for (i = 0; i < BUF_SIZE_BYTES; i++) { v->vibBuffer[i]=0; }

  /*
   * The offset tables contains fixed-point offsets from the writer's
   * standard positions. The offsets run ahead for half a cycle and lag
   * behind the writer for the other half. The amplitude applied determines
   * the distance ahead and behind (they are symmetric) and thus the
   * perceived Doppler effect. It is, after all, just a variable delay.
   */

  for (i = 0; i < INCTBL_SIZE; i++) {
    double m = sin ((2.0 * M_PI * i) / INCTBL_SIZE);
    v->offset1Table [i] = (unsigned int) ((1.0 + voff1 + (m * v->vib1OffAmp)) * S);
    v->offset2Table [i] = (unsigned int) ((1.0 + voff2 + (m * v->vib2OffAmp)) * S);
    v->offset3Table [i] = (unsigned int) ((1.0 + voff3 + (m * v->vib3OffAmp)) * S);
  }

#ifdef DEBUG_DUMP_SCANNER
  {
    FILE * fp;
    char * debugfile = "scanner.log";
    if ((fp = fopen (debugfile, "w")) != NULL) {

      fprintf (fp,
	       "statorIncrement = 0x%08x %g\n",
	       v->statorIncrement,
	       ((double) v->statorIncrement) / S);

      fprintf (fp, "voff1 = %g,  voff2 = %g, voff3 = %g\n",
	       voff1, voff2, voff3);

      fprintf (fp, "offset1Table\n");
      for (i = 0; i < INCTBL_SIZE; i++) {
	fprintf (fp,
		 "%4d 0x%08x %g\n",
		 i,
		 v->offset1Table[i],
		 ((double) v->offset1Table[i]) / S);
      }
      fclose (fp);
    }
    else {
      perror (debugfile);
      exit (1);
    }
  }
#endif /* DEBUG */
}

/*
 * Initialises this module.
 */
void resetVibrato (void *t) {
  struct b_vibrato *v = &(((struct b_tonegen*)t)->inst_vibrato);

  v->offsetTable = v->offset3Table;
  v->stator = 0;
  v->statorIncrement = 0;

  v->outPos =  BUF_MASK_SAMPLES / 2;

  v->vib1OffAmp = 3.0;
  v->vib2OffAmp = 6.0;
  v->vib3OffAmp = 9.0;

  v->vibFqHertz = 7.25;

  v->mixedBuffers = FALSE;
  v->effectEnabled = FALSE;
}

void initVibrato (void *t, void *m) {
  struct b_vibrato *v = &(((struct b_tonegen*)t)->inst_vibrato);
  setScannerFrequency (v, v->vibFqHertz);
  initIncrementTables (v);
  setVibrato (t, 0);
  useMIDIControlFunction (m, "vibrato.knob", setVibratoFromMIDI, t);
  useMIDIControlFunction (m, "vibrato.routing", setVibratoRoutingFromMIDI, t);
}

/*
 * Configuration interface.
 */
int scannerConfig (void *t, ConfigContext * cfg) {
  struct b_vibrato *v = &(((struct b_tonegen*)t)->inst_vibrato);
  int ack = 0;
  double d;
  if ((ack = getConfigParameter_dr ("scanner.hz",
				    cfg,
				    &d,
				    4.0, 22.0)) == 1) {
    setScannerFrequency (v, d);
  }
  else if ((ack = getConfigParameter_dr ("scanner.modulation.v1",
					 cfg,
					 &v->vib1OffAmp,
					 0.0, 12.0)) == 1) {}
  else if ((ack = getConfigParameter_dr ("scanner.modulation.v2",
					 cfg,
					 &v->vib2OffAmp,
					 0.0, 12.0)) == 1) {}
  else if ((ack = getConfigParameter_dr ("scanner.modulation.v3",
					 cfg,
					 &v->vib3OffAmp,
					 0.0, 12.0)) == 1) {}
  return ack;
} /* scannerConfig */

static const ConfigDoc doc[] = {
  {"scanner.hz", CFG_DOUBLE, "7.25", "range: [4..22]"},
  {"scanner.modulation.v1", CFG_DOUBLE, "3.0", "range: [0..12]"},
  {"scanner.modulation.v2", CFG_DOUBLE, "6.0", "range: [0..12]"},
  {"scanner.modulation.v3", CFG_DOUBLE, "9.0", "range: [0..12]"},
  {NULL}
};

const ConfigDoc *scannerDoc () {
  return doc;
}

/*
 * Floating-point version of vibrato scanner.
 * Since this is a variable delay, delayed samples take a rest in vibBuffer
 * between calls to this function.
 */
float * vibratoProc (struct b_vibrato* v, float * inbuffer, float * outbuffer, size_t bufferLengthSamples)
{
  const float fnorm   = 1.0 / 65536.0;
  const float mixnorm = 0.7071067811865475; /* 1/sqrt(2) */
  int i;
  float * xp = inbuffer;
  float * yp = outbuffer;

  for (i = 0; i < bufferLengthSamples; i++) {
    /* Fetch the next input sample */
    const float x = *xp++;
    /* Determine the fixed point writing position. This is relative to */
    /* the current output position (outpos). */
    const unsigned int j =
      ((v->outPos << 16) + v->offsetTable[v->stator >> 16]) & BUF_MASK_POSN;
    /* Convert fixpoint writing position to integer sample */
    const int h = j >> 16;
    /* And the following sample, possibly wrapping the delay buffer. */
    const int k = (h + 1) & BUF_MASK_SAMPLES;
    /* Drop the integer part of the fixpoint position. */
    const float f = fnorm * ((float) (j & 0xFFFF));
    /* Amplify incoming sample and normalise */
    const float g = f * x;

    /* Write to delay buffer */
    v->vibBuffer[h] += x - g;
    v->vibBuffer[k] += g;

    if (v->mixedBuffers) {
      *yp++ = (x + v->vibBuffer[v->outPos]) * mixnorm;
    }
    else {
      *yp++ = v->vibBuffer[v->outPos];
    }

    /* Zero delay buffer at the reading position. */
    v->vibBuffer[v->outPos] = 0;
    /* Update the reading position, wrapping back to start if needed. */
    v->outPos = (v->outPos + 1) & BUF_MASK_SAMPLES;
    /* Update the delay amount index. */
    v->stator = (v->stator + v->statorIncrement) & INCTBL_MASK;
  }

  return outbuffer;
}

/* vi:set ts=8 sts=2 sw=2: */
