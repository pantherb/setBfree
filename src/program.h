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

#ifndef PROGRAM_H
#define PROGRAM_H

#include "cfgParser.h"

#define MAXPROGS (129)

#define NAMESZ 22
#define NFLAGS 1		/* The nof flag fields in Programme struct */
typedef struct _programme {
  char name [NAMESZ];
  unsigned int flags[NFLAGS];
  unsigned int drawbars[9];
  unsigned int lowerDrawbars[9];
  unsigned int pedalDrawbars[9];
  short        keyAttackEnvelope; // unused
  float        keyAttackClickLevel; // unused
  float        keyAttackClickDuration; // unused
  short        keyReleaseEnvelope; // unused
  float        keyReleaseClickLevel; // unused
  float        keyReleaseClickDuration; // unused
  short        scanner;
  short        percussionEnabled;
  short        percussionVolume;
  short        percussionSpeed;
  short        percussionHarmonic;
  short        overdriveSelect;
  short        rotaryEnabled; // unused
  short        rotarySpeedSelect;
  float        reverbMix;
  short        keyboardSplitLower;
  short        keyboardSplitPedals;
  short        transpose[7];
} Programme;

struct b_programme {
/**
 * This is to compensate for MIDI controllers that number the programs
 * from 1 to 128 on their interface. Internally we use 0-127, as does
 * MIDI.
 */
	int MIDIControllerPgmOffset;
	int previousPgmNr;
	Programme programmes[MAXPROGS];
};

extern int pgmConfig (struct b_programme *p, ConfigContext * cfg);
extern const ConfigDoc *pgmDoc ();

extern void installProgram (void *inst, unsigned char uc);

extern void listProgrammes (struct b_programme *p, FILE * fp);
extern int walkProgrammes (struct b_programme *p, int clear);
extern void loopProgammes (struct b_programme *p, int all, void (*cb)(int, int, const char*, void *), void *arg);
extern int formatProgram(Programme *p, char *out, int maxlen);

extern struct b_programme *allocProgs ();
extern void freeProgs (struct b_programme *p);

extern int bindToProgram (void * pp,
			  char * fileName,
			  int    lineNumber,
			  int    pgmnr,
			  char * sym,
			  char * val);

#endif /* PROGRAM_H */
