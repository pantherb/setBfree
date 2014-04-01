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

/* Flag bits used in the first field */

#define FL_INUSE  0x0001	/* Record is in use */

#define FL_DRAWBR 0x0002	/* Set drawbars */

#define FL_ATKENV 0x0004	/* Attack envelope */
#define FL_ATKCKL 0x0008	/* Attack Click level */
#define FL_ATKCKD 0x0010	/* Attack click duration */

#define FL_RLSENV 0x0020	/* Release envelope */
#define FL_RLSCKL 0x0040	/* Release level */
#define FL_RLSCKD 0x0080	/* Release duration */

#define FL_SCANNR 0x0100	/* Vibrato scanner modulation depth */

#define FL_PRCENA 0x0200	/* Percussion on/off */
#define FL_PRCVOL 0x0400	/* Percussion soft/normal */
#define FL_PRCSPD 0x0800	/* Percussion slow/fast */
#define FL_PRCHRM 0x1000	/* Percussion 2nd/3rd */

#define FL_OVRSEL 0x2000	/* Overdrive on/off */

#define FL_ROTENA 0x4000	/* Rotary on/off */
#define FL_ROTSPS 0x8000	/* Rotary speed select */

#define FL_RVBMIX 0x00010000	/* Reverb on/off */

#define FL_DRWRND 0x00020000	/* Randomize drawbars */
#define FL_KSPLTL 0x00040000	/* Keyboard split point lower/upper */

#define FL_LOWDRW 0x00080000	/* Lower manual drawbars */
#define FL_PDLDRW 0x00100000	/* Pedal drawbars */

#define FL_KSPLTP 0x00200000	/* Keyboard split point pedal/lower */

#define FL_TRA_PD 0x00400000	/* Transpose for pedal split region */
#define FL_TRA_LM 0x00800000	/* Transpose for lower split region */
#define FL_TRA_UM 0x01000000	/* Transpose for upper split region */
#define FL_TRANSP 0x02000000	/* Global transpose */
#define FL_TRCH_A 0x04000000	/* Channel A (upper) transpose */
#define FL_TRCH_B 0x08000000	/* Channel B (lower) transpose */
#define FL_TRCH_C 0x10000000	/* Channel C (pedal) transpose */

#define FL_VCRUPR 0x20000000	/* Vib/cho upper manual routing */
#define FL_VCRLWR 0x40000000	/* Vib/cho lower manual routing */


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
extern int saveProgramm(void *inst, int pgm, char *name, int flagmask);
extern void exportProgramms(struct b_programme *p, FILE * fp);
extern void writeProgramm(int pgmNr, Programme *p, const char *sep, FILE * fp);

extern struct b_programme *allocProgs ();
extern void freeProgs (struct b_programme *p);

extern int bindToProgram (void * pp,
			  const char * fileName,
			  const int    lineNumber,
			  const int    pgmnr,
			  const char * sym,
			  const char * val);

#endif /* PROGRAM_H */
