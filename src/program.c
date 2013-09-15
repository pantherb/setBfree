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
 * program.c
 * 17-sep-2004/FK Upgraded to pedal/lower/upper splitpoints.
 * 22-aug-2004/FK Added MIDIControllerPgmOffset parameter and config.
 * 21-aug-2004/FK Replaced include of preamp.h with overdrive.h.
 * 14-may-2004/FK Replacing rotsim module with whirl.
 * 10-may-2003/FK New syntax and parser in a separate file.
 * 2001-12-28/FK
 *
 * Manager for program change.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

#include "main.h"
#include "global_inst.h"
#include "program.h"

#define SET_TRUE 1
#define SET_NONE 0
#define SET_FALSE -1

#define MESSAGEBUFFERSIZE 256

#define FILE_BUFFER_SIZE 2048


#define ANY_TRSP (FL_TRA_PL | FL_TRA_LM | FL_TRA_UM | FL_TRANSP | \
                  FL_TRCH_A | FL_TRCH_B | FL_TRCH_C)

/* Indices to the transpose array in struct _programme. */

#define TR_TRANSP 0		/* Global transpose value */
#define TR_CHNL_A 1		/* Channel A transpose */
#define TR_CHNL_B 2		/* Channel B transpose */
#define TR_CHNL_C 3		/* Channel C transpose */
#define TR_CHA_UM 4		/* Channel A upper split region */
#define TR_CHA_LM 5		/* Channel A lower split region */
#define TR_CHA_PD 6		/* Channel A pedal split region */

/*
 * The   short scanner   field has the following bit assignments:
 *
 * 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * -----------------------------------------------
 *                                            0  1  Mod. depth select:  1
 *                                            1  0  Mod. depth select:  2
 *                                            1  1  Mod. depth select:  3
 *                          0                       vibrato
 *                          1                       chorus
 *                       0                          lower manual NO vib/cho
 *                       1                          lower manual to vib/cho
 *                     0                            upper manual NO vib/cho
 *                     1                            upper manual to vib/cho
 */

#ifndef PRG_MAIN
#include "defaultpgm.h"
#include "midi.h"
#endif

/* Property codes; used internally to identity the parameter controlled. */

enum propertyId {
  pr_Name,
  pr_Drawbars,
  pr_LowerDrawbars,
  pr_PedalDrawbars,
  pr_KeyAttackEnvelope,
  pr_KeyAttackClickLevel,
  pr_KeyAttackClickDuration,
  pr_KeyReleaseEnvelope,
  pr_KeyReleaseClickLevel,
  pr_KeyReleaseClickDuration,
  pr_Scanner,
  pr_VibratoUpper,
  pr_VibratoLower,
  pr_PercussionEnabled,
  pr_PercussionVolume,
  pr_PercussionSpeed,
  pr_PercussionHarmonic,
  pr_OverdriveSelect,
  pr_RotaryEnabled,
  pr_RotarySpeedSelect,
  pr_ReverbMix,
  pr_KeyboardSplitLower,
  pr_KeyboardSplitPedals,
  pr_TransposeSplitPedals,
  pr_TransposeSplitLower,
  pr_TransposeSplitUpper,
  pr_Transpose,
  pr_TransposeUpper,
  pr_TransposeLower,
  pr_TransposePedals,
  pr_void
};

typedef struct _symbolmap {
  char * propertyName;
  int property;
} SymbolMap;

/*
 * This table maps from the string keywords used in the .prg file
 * to the internal property symbols.
 */

static const SymbolMap propertySymbols [] = {
  {"name",           pr_Name},
  {"drawbars",       pr_Drawbars},
  {"drawbarsupper",  pr_Drawbars},
  {"drawbarslower",  pr_LowerDrawbars},
  {"drawbarspedals", pr_PedalDrawbars},
  {"attackenv",      pr_KeyAttackEnvelope},
  {"attacklvl",      pr_KeyAttackClickLevel},
  {"attackdur",      pr_KeyAttackClickDuration},
  {"vibrato",        pr_Scanner},
  {"vibratoknob",    pr_Scanner},
  {"vibratoupper",   pr_VibratoUpper},
  {"vibratolower",   pr_VibratoLower},
  {"perc",           pr_PercussionEnabled},
  {"percvol",        pr_PercussionVolume},
  {"percspeed",      pr_PercussionSpeed},
  {"percharm",       pr_PercussionHarmonic},
  {"overdrive",      pr_OverdriveSelect},
  {"rotary",         pr_RotaryEnabled},
  {"rotaryspeed",    pr_RotarySpeedSelect},
  {"reverbmix",      pr_ReverbMix},
  {"keysplitlower",  pr_KeyboardSplitLower},
  {"keysplitpedals", pr_KeyboardSplitPedals},
  {"trssplitpedals", pr_TransposeSplitPedals},
  {"trssplitlower",  pr_TransposeSplitLower},
  {"trssplitupper",  pr_TransposeSplitUpper},
  {"transpose",      pr_Transpose},
  {"transposeupper", pr_TransposeUpper},
  {"transposelower", pr_TransposeLower},
  {"transposepedals",pr_TransposePedals},
  {NULL, pr_void}
};

/* ---------------------------------------------------------------- */

/**
 * Look up the property string and return the internal property value.
 */
static int getPropertyIndex (char * sym) {
  int i;
  for (i = 0; propertySymbols[i].propertyName != NULL; i++) {
    if (!strcasecmp (propertySymbols[i].propertyName, sym)) {
      return propertySymbols[i].property;
    }
  }
  return -1;
}

/* ======================================================================== */

/**
 * Prints a message followed by the given filename and linenumber.
 * Returns the given return code, so that it may be called in a return
 * statement from within a parsing function.
 */
static int stateMessage (char * fileName,
			 int lineNumber,
			 char * msg,
			 int code) {
  fprintf (stderr, "%s in file %s on line %d\n", msg, fileName, lineNumber);
  return code;
}

/**
 * Parses a drawbar registration.
 * @param drw        The drawbar registration string.
 * @param bar        Array of intergers where the registration is stored.
 * @param lineNumber The linenumber in the input file.
 * @param fileName   The name of the current input file.
 */
static int parseDrawbarRegistration (char * drw,
				     unsigned int bar[],
				     int    lineNumber,
				     char * fileName) {

  char msg[MESSAGEBUFFERSIZE];
  int bus = 0;
  char * t = drw;

  while (bus < 9) {
    if (*t == '\0') {
      sprintf (msg, "Drawbar registration incomplete '%s'", drw);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    if ((isspace (*t)) || (*t == '-') || (*t == '_')) {
      t++;
      continue;
    }
    if (('0' <= *t) && (*t <= '8')) {
      bar[bus] = *t - '0';
      t++;
      bus++;
      continue;
    }
    else {
      sprintf (msg, "Illegal char in drawbar registration '%c'", *t);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
  }

  return 0;
}



/**
 * Return TRUE if the supplied string can be interpreted as an enabling arg.
 */
static int isAffirmative (char * value) {
  int n;
  if (!strcasecmp (value, "on")) return TRUE;
  if (!strcasecmp (value, "yes")) return TRUE;
  if (!strcasecmp (value, "true")) return TRUE;
  if (!strcasecmp (value, "enabled")) return TRUE;
  if (sscanf (value, "%d", &n) == 1) {
    if (n != 0) return TRUE;
  }
  return FALSE;
}

/**
 * Return TRUE if the supplied string can be interpreted as a disabling arg.
 */
static int isNegatory (char * value) {
  int n;
  if (!strcasecmp (value, "off")) return TRUE;
  if (!strcasecmp (value, "no")) return TRUE;
  if (!strcasecmp (value, "none")) return TRUE;
  if (!strcasecmp (value, "false")) return TRUE;
  if (!strcasecmp (value, "disabled")) return TRUE;
  if (sscanf (value, "%d", &n) == 1) {
    if (n == 0) return TRUE;
  }
  return FALSE;
}

/**
 * This function parses a transpose argument. It expects an integer in the
 * range -127 .. 127. It is a helper function to bindToProgram () below.
 */
static int parseTranspose (char * val, int * vp, char * msg) {
  if (sscanf (val, "%d", vp) == 0) {
    sprintf (msg, "Transpose: integer expected : '%s'", val);
    return -1;
  }
  else if (((*vp) < -127) || (127 < (*vp))) {
    sprintf (msg, "Transpose: argument out of range : '%s'", val);
    return -1;
  }
  return 0;
}

/**
 * This function is called from the syntax parser in file pgmParser.c.
 * Return: 0 OK, non-zero error.
 */
int bindToProgram (void * pp,
		   char * fileName,
		   int    lineNumber,
		   int    pgmnr,
		   char * sym,
		   char * val)
{
  struct b_programme *p = (struct b_programme *)pp;
  int prop;
  char msg[MESSAGEBUFFERSIZE];
  float fv;
  int iv;
  int rtn;
  Programme * PGM;

  /* Check the program number */

  if ((pgmnr < 0) || (MAXPROGS <= pgmnr)) {
    sprintf (msg, "Program number %d out of range", pgmnr);
    return stateMessage (fileName, lineNumber, msg, -1);
  }

  PGM = &(p->programmes[pgmnr]);

  /* If this is a new program number, clear the property flags */

  if (pgmnr != p->previousPgmNr) {
    PGM->flags[0] = 0;
    p->previousPgmNr = pgmnr;
  }

  /* Scan for a matching property symbol */

  prop = getPropertyIndex (sym);

  if (prop < 0) {
    sprintf (msg, "Unrecognized property '%s'", sym);
    return stateMessage (fileName, lineNumber, msg, -1);
  }

  switch (prop) {

  case pr_Name:
    strncpy (PGM->name, val, NAMESZ);
    PGM->name[NAMESZ-1] = '\0';
    PGM->flags[0] |= FL_INUSE;
    break;

  case pr_Drawbars:
    if (!strcasecmp (val, "random")) {
      PGM->flags[0] |= (FL_INUSE | FL_DRAWBR | FL_DRWRND);
    }
    else if (!parseDrawbarRegistration (val,
					PGM->drawbars,
					lineNumber,
					fileName)) {
      PGM->flags[0] |= (FL_INUSE|FL_DRAWBR);
    }
    else {
      return -1;
    }
    break;

  case pr_LowerDrawbars:
    if (!strcasecmp (val, "random")) {
      PGM->flags[0] |= (FL_INUSE | FL_LOWDRW | FL_DRWRND);
    } else if (!parseDrawbarRegistration (val,
					  PGM->lowerDrawbars,
					  lineNumber,
					  fileName)) {
      PGM->flags[0] |= (FL_INUSE | FL_LOWDRW);
    } else {
      return -1;
    }
    break;

  case pr_PedalDrawbars:
    if (!strcasecmp (val, "random")) {
      PGM->flags[0] |= (FL_INUSE | FL_PDLDRW | FL_DRWRND);
    } else if (!parseDrawbarRegistration (val,
					  PGM->pedalDrawbars,
					  lineNumber,
					  fileName)) {
      PGM->flags[0] |= (FL_INUSE | FL_PDLDRW);
    } else {
      return -1;
    }
    break;

  case pr_Scanner:
    if (!strcasecmp (val, "v1")) {
      PGM->scanner = (PGM->scanner & 0xFF00) | VIB1;	/* in vibrato.h */
      PGM->flags[0] |= (FL_INUSE|FL_SCANNR);
    }
    else if (!strcasecmp (val, "v2")) {
      PGM->scanner = (PGM->scanner & 0xFF00) | VIB2;	/* in vibrato.h */
      PGM->flags[0] |= (FL_INUSE|FL_SCANNR);
    }
    else if (!strcasecmp (val, "v3")) {
      PGM->scanner = (PGM->scanner & 0xFF00) | VIB3;	/* in vibrato.h */
      PGM->flags[0] |= (FL_INUSE|FL_SCANNR);
    }
    else if (!strcasecmp (val, "c1")) {
      PGM->scanner = (PGM->scanner & 0xFF00) | CHO1;	/* in vibrato.h */
      PGM->flags[0] |= (FL_INUSE|FL_SCANNR);
    }
    else if (!strcasecmp (val, "c2")) {
      PGM->scanner = (PGM->scanner & 0xFF00) | CHO2;	/* in vibrato.h */
      PGM->flags[0] |= (FL_INUSE|FL_SCANNR);
    }
    else if (!strcasecmp (val, "c3")) {
      PGM->scanner = (PGM->scanner & 0xFF00) | CHO3;	/* in vibrato.h */
      PGM->flags[0] |= (FL_INUSE|FL_SCANNR);
    }
    else {
      sprintf (msg, "Unrecognized vibrato value '%s'", val);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    break;

  case pr_VibratoUpper:
    if (isNegatory (val)) {
      PGM->scanner &= ~0x200;
      PGM->flags[0] |= (FL_INUSE|FL_VCRUPR);
    }
    else if (isAffirmative (val)) {
      PGM->scanner |= 0x200;
      PGM->flags[0] |= (FL_INUSE|FL_VCRUPR);
    }
    else {
      sprintf (msg, "Unrecognized keyword '%s'", val);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    break;			/* pr_VibratoUpper */

  case pr_VibratoLower:
    if (isNegatory (val)) {
      PGM->scanner &= ~0x100;
      PGM->flags[0] |= (FL_INUSE|FL_VCRLWR);
    }
    else if (isAffirmative (val)) {
      PGM->scanner |= 0x100;
      PGM->flags[0] |= (FL_INUSE|FL_VCRLWR);
    }
    else {
      sprintf (msg, "Unrecognized keyword '%s'", val);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    break;			/* pr_VibratoUpper */

  case pr_PercussionEnabled:
    if (isAffirmative (val)) {
      PGM->percussionEnabled = TRUE;
      PGM->flags[0] |= (FL_INUSE|FL_PRCENA);
    }
    else if (isNegatory (val)) {
      PGM->percussionEnabled = FALSE;
      PGM->flags[0] |= (FL_INUSE|FL_PRCENA);
    }
    else {
      sprintf (msg, "Unrecognized percussion enabled value '%s'", val);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    break;

  case pr_PercussionVolume:
    if (!strcasecmp (val, "normal") ||
	!strcasecmp (val, "high")   ||
	!strcasecmp (val, "hi")) {
      PGM->percussionVolume = FALSE;
      PGM->flags[0] |= (FL_INUSE|FL_PRCVOL);
    }
    else if (!strcasecmp (val, "soft") ||
	     !strcasecmp (val, "low")  ||
	     !strcasecmp (val, "lo")) {
      PGM->percussionVolume = TRUE;
      PGM->flags[0] |= (FL_INUSE|FL_PRCVOL);
    }
    else {
      sprintf (msg, "Unrecognized percussion volume argument '%s'", val);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    break;

  case pr_PercussionSpeed:
    if (!strcasecmp (val, "fast") ||
	!strcasecmp (val, "high") ||
	!strcasecmp (val, "hi")) {
      PGM->percussionSpeed = TRUE;
      PGM->flags[0] |= (FL_INUSE|FL_PRCSPD);
    }
    else if (!strcasecmp (val, "slow") ||
	     !strcasecmp (val, "low")  ||
	     !strcasecmp (val, "lo")) {
      PGM->percussionSpeed = FALSE;
      PGM->flags[0] |= (FL_INUSE|FL_PRCSPD);
    }
    else {
      sprintf (msg, "Unrecognized percussion speed argument '%s'", val);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    break;

  case pr_PercussionHarmonic:
    if (!strcasecmp (val, "second") ||
	!strcasecmp (val, "2nd")    ||
	!strcasecmp (val, "low")    ||
	!strcasecmp (val, "lo")) {
      PGM->percussionHarmonic = TRUE;
      PGM->flags[0] |= (FL_INUSE|FL_PRCHRM);
    }
    else if (!strcasecmp (val, "third") ||
	     !strcasecmp (val, "3rd")   ||
	     !strcasecmp (val, "high")  ||
	     !strcasecmp (val, "hi")) {
      PGM->percussionHarmonic = FALSE;
      PGM->flags[0] |= (FL_INUSE|FL_PRCHRM);
    }
    else {
      sprintf (msg, "Unrecognized percussion harmonic option '%s'", val);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    break;

  case pr_OverdriveSelect:
    if (isNegatory (val)) {
      PGM->overdriveSelect = TRUE;
      PGM->flags[0] |= (FL_INUSE|FL_OVRSEL);
    }
    else if (isAffirmative (val)) {
      PGM->overdriveSelect = FALSE;
      PGM->flags[0] |= (FL_INUSE|FL_OVRSEL);
    }
    else {
      sprintf (msg, "Unrecognized overdrive select argument '%s'", val);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    break;

  case pr_RotarySpeedSelect:
    if (!strcasecmp (val, "tremolo") ||
	!strcasecmp (val, "fast")    ||
	!strcasecmp (val, "high")    ||
	!strcasecmp (val, "hi")) {
      PGM->rotarySpeedSelect = WHIRL_FAST;
      PGM->flags[0] |= (FL_INUSE|FL_ROTSPS);
    }
    else if (!strcasecmp (val, "chorale") ||
	     !strcasecmp (val, "slow")    ||
	     !strcasecmp (val, "low")     ||
	     !strcasecmp (val, "lo")) {
      PGM->rotarySpeedSelect = WHIRL_SLOW;
      PGM->flags[0] |= (FL_INUSE|FL_ROTSPS);
    }
    else if (!strcasecmp (val, "stop")  ||
	     !strcasecmp (val, "zero")  ||
	     !strcasecmp (val, "break") ||
	     !strcasecmp (val, "stopped")) {
      PGM->rotarySpeedSelect = WHIRL_STOP;
      PGM->flags[0] |= (FL_INUSE|FL_ROTSPS);
    }
    else {
      sprintf (msg, "Unrecognized rotary speed argument '%s'", val);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    break;

  case pr_ReverbMix:
    PGM->flags[0] |= (FL_INUSE|FL_RVBMIX);
    if (sscanf (val, "%f", &fv) == 0) {
      sprintf (msg, "Unrecognized reverb mix value : '%s'", val);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    else if ((fv < 0.0) || (1.0 < fv)) {
      sprintf (msg, "Reverb mix value out of range : %f", fv);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    else {
      PGM->reverbMix = fv;
    }
    break;

  case pr_KeyboardSplitLower:
    PGM->flags[0] |= (FL_INUSE|FL_KSPLTL);
    if (sscanf (val, "%d", &iv) == 0) {
      sprintf (msg, "Lower split: unparsable MIDI note number : '%s'", val);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    else if ((iv < 0) || (127 < iv)) {
      sprintf (msg, "Lower split: MIDI note number out of range: '%s'", val);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    else {
      PGM->keyboardSplitLower = (short) iv;
    }
    break;

  case pr_KeyboardSplitPedals:
    PGM->flags[0] |= (FL_INUSE|FL_KSPLTP);
    if (sscanf (val, "%d", &iv) == 0) {
      sprintf (msg, "Pedal split: unparsable MIDI note number : '%s'", val);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    else if ((iv < 0) || (127 < iv)) {
      sprintf (msg, "Pedal split: MIDI note number out of range: '%s'", val);
      return stateMessage (fileName, lineNumber, msg, -1);
    }
    else {
      PGM->keyboardSplitPedals = (short) iv;
    }
    break;

    /*
     * A macro to avoid tedious repetition of code. The parameter F is
     * the bit in the 0th flag word for the parameter. The parameter I is
     * the index in the PGM->transpose[] array.
     * This macro is obviously very context-dependent and is therefore
     * undefined as soon as we do not need it anymore.
     */

#define SET_TRANSPOSE(F,I) PGM->flags[0] |= (FL_INUSE|(F)); \
  if ((rtn = parseTranspose (val, &iv, msg))) { \
    return stateMessage (fileName, lineNumber, msg, rtn); \
  } else { \
    PGM->transpose[(I)] = iv; \
  }

  case pr_TransposeSplitPedals:
    SET_TRANSPOSE(FL_TRA_PD, TR_CHA_PD);
    break;

  case pr_TransposeSplitLower:
    SET_TRANSPOSE(FL_TRA_LM, TR_CHA_LM);
    break;

  case pr_TransposeSplitUpper:
    SET_TRANSPOSE(FL_TRA_UM, TR_CHA_UM);
    break;

  case pr_Transpose:
    SET_TRANSPOSE(FL_TRANSP, TR_TRANSP);
    break;

  case pr_TransposeUpper:
    SET_TRANSPOSE(FL_TRCH_A, TR_CHNL_A);
    break;

  case pr_TransposeLower:
    SET_TRANSPOSE(FL_TRCH_B, TR_CHNL_B);
    break;

  case pr_TransposePedals:
    SET_TRANSPOSE(FL_TRCH_C, TR_CHNL_C);
    break;

#undef SET_TRANSPOSE

  } /* switch property */

  return 0;
}

#ifndef PRG_MAIN
static int format_drawbars(const unsigned int drawbars [], char *buf) {
  return sprintf (buf, "%c%c%c %c%c%c%c %c%c",
    '0' + drawbars[0],
    '0' + drawbars[1],
    '0' + drawbars[2],
    '0' + drawbars[3],
    '0' + drawbars[4],
    '0' + drawbars[5],
    '0' + drawbars[6],
    '0' + drawbars[7],
    '0' + drawbars[8]);
}
/**
 * Installs random values in the supplied array.
 * @param drawbars  Array where to store drawbar settings.
 * @param buf       If non-NULL a display string of the values is stored.
 */
static void randomizeDrawbars (unsigned int drawbars [], char * buf) {
  int i;

  for (i = 0; i < 9; i++) {
    drawbars[i] = rand () % 9;
  }

  if (buf != NULL) {
    format_drawbars(drawbars, buf);
  }
}

/**
 * This is the routine called by the MIDI parser when it detects
 * a Program Change message.
 */
void installProgram (void *instance, unsigned char uc) {
  int p = (int) uc;
  b_instance * inst = (b_instance*) instance;

  p += inst->progs->MIDIControllerPgmOffset;

  if ((0 < p) && (p < MAXPROGS)) {

    Programme * PGM = &(inst->progs->programmes[p]);
    unsigned int flags0 = PGM->flags[0];
#ifdef DEBUG_MIDI_PROGRAM_CHANGES
    char display[128];
#endif

    if (flags0 & FL_INUSE) {

#ifdef DEBUG_MIDI_PROGRAM_CHANGES
      strcpy (display, PGM->name);
#endif

      if (flags0 & FL_DRWRND) {
	char buf [32];

	if (flags0 & FL_DRAWBR) {
	  randomizeDrawbars (PGM->drawbars, buf);
#ifdef DEBUG_MIDI_PROGRAM_CHANGES
	  strcat (display, "UPR:");
	  strcat (display, buf);
#endif
	}

	if (flags0 & FL_LOWDRW) {
	  randomizeDrawbars (PGM->lowerDrawbars, buf);
#ifdef DEBUG_MIDI_PROGRAM_CHANGES
	  strcat (display, "LOW:");
	  strcat (display, buf);
#endif
	}

	if (flags0 & FL_PDLDRW) {
	  randomizeDrawbars (PGM->pedalDrawbars, buf);
#ifdef DEBUG_MIDI_PROGRAM_CHANGES
	  strcat (display, "PDL:");
	  strcat (display, buf);
#endif
	}
      }

#ifdef DEBUG_MIDI_PROGRAM_CHANGES
      /* this is not RT safe */
      //fprintf (stdout, "\rPGM: %s           \r", display); fflush (stdout);
      fprintf (stdout, "PGM: %s\n", display);
#endif

      if (flags0 & FL_DRAWBR) {
	setDrawBars (inst, 0, PGM->drawbars);
      }

      if (flags0 & FL_LOWDRW) {
	setDrawBars (inst, 1, PGM->lowerDrawbars);
      }

      if (flags0 & FL_PDLDRW) {
	setDrawBars (inst, 2, PGM->pedalDrawbars);
      }

      if (flags0 & FL_SCANNR) {
	//setVibrato (inst->synth, PGM->scanner & 0x00FF);
	assert((PGM->scanner & 0xff) > 0);
	int knob = ((PGM->scanner & 0xf) << 1) - ((PGM->scanner & CHO_) ? 1 : 2);
	callMIDIControlFunction(inst->midicfg, "vibrato.knob", knob * 23);
      }

      if (flags0 & FL_VCRUPR) {
	//setVibratoUpper (inst->synth, PGM->scanner & 0x200);
	int rt = getVibratoRouting(inst->synth) & ~0x2;
	rt |= (PGM->scanner & 0x200) ? 2 : 0;
	callMIDIControlFunction(inst->midicfg, "vibrato.routing", rt << 5);
      }

      if (flags0 & FL_VCRLWR) {
	//setVibratoLower (inst->synth, PGM->scanner & 0x100);
	int rt = getVibratoRouting(inst->synth) & ~0x1;
	rt |= (PGM->scanner & 0x100) ? 1 : 0;
	callMIDIControlFunction(inst->midicfg, "vibrato.routing", rt << 5);
      }

      if (flags0 & FL_PRCENA) {
	setPercussionEnabled (inst->synth, PGM->percussionEnabled);
	callMIDIControlFunction(inst->midicfg, "percussion.enable", PGM->percussionEnabled ? 127 : 0);
      }

      if (flags0 & FL_PRCVOL) {
	//setPercussionVolume (inst->synth, PGM->percussionVolume);
	callMIDIControlFunction(inst->midicfg, "percussion.volume", PGM->percussionVolume ? 127 : 0);
      }

      if (flags0 & FL_PRCSPD) {
	//setPercussionFast (inst->synth, PGM->percussionSpeed);
	callMIDIControlFunction(inst->midicfg, "percussion.decay", PGM->percussionSpeed ? 127 : 0);
      }

      if (flags0 & FL_PRCHRM) {
	//setPercussionFirst (inst->synth, PGM->percussionHarmonic);
	callMIDIControlFunction(inst->midicfg, "percussion.harmonic", PGM->percussionHarmonic ? 127 : 0);
      }

      if (flags0 & FL_OVRSEL) {
	//setClean (inst->preamp, PGM->overdriveSelect);
	callMIDIControlFunction(inst->midicfg, "overdrive.enable", PGM->overdriveSelect ? 0 : 127);
      }

      if (flags0 & FL_ROTENA) {
	/* Rotary enabled */
      }

      if (flags0 & FL_ROTSPS) {
	// setRevSelect (inst->whirl, (int) (PGM->rotarySpeedSelect));
	callMIDIControlFunction(inst->midicfg, "rotary.speed-preset", PGM->rotarySpeedSelect * 32);
      }

      if (flags0 & FL_RVBMIX) {
	//setReverbMix (inst->reverb, PGM->reverbMix);
	callMIDIControlFunction(inst->midicfg, "reverb.mix-preset", (PGM->reverbMix * 127.0));
      }

      /* TODO --  keyboard split & transpose are not yet saved */
      if (flags0 & (FL_KSPLTL|FL_KSPLTP|FL_TRA_PD|FL_TRA_LM|FL_TRA_UM)) {
	int b;
	b  = (flags0 & FL_KSPLTP) ?  1 : 0;
	b |= (flags0 & FL_KSPLTL) ?  2 : 0;
	b |= (flags0 & FL_TRA_PD) ?  4 : 0;
	b |= (flags0 & FL_TRA_LM) ?  8 : 0;
	b |= (flags0 & FL_TRA_UM) ? 16 : 0;
	setKeyboardSplitMulti (inst->midicfg, b,
			       PGM->keyboardSplitPedals,
			       PGM->keyboardSplitLower,
			       PGM->transpose[TR_CHA_PD],
			       PGM->transpose[TR_CHA_LM],
			       PGM->transpose[TR_CHA_UM]);
      }

      if (flags0 & FL_TRANSP) {
	setKeyboardTranspose (inst->midicfg, PGM->transpose[TR_TRANSP]);
      }

      if (flags0 & FL_TRCH_A) {
	setKeyboardTransposeA (inst->midicfg, PGM->transpose[TR_CHNL_A]);
      }

      if (flags0 & FL_TRCH_B) {
	setKeyboardTransposeB (inst->midicfg, PGM->transpose[TR_CHNL_B]);
      }

      if (flags0 & FL_TRCH_C) {
	setKeyboardTransposeC (inst->midicfg, PGM->transpose[TR_CHNL_C]);
      }

    }
  }
}


/**
 * Configures this modules.
 */
int pgmConfig (struct b_programme *p, ConfigContext * cfg) {
  int ack = 0;
  int ival;
  if ((ack = getConfigParameter_i ("pgm.controller.offset",
				   cfg, &ival)) == 1) {
    if (ival == 0 || ival == 1)
      p->MIDIControllerPgmOffset = ival;
  }

  return ack;
}
#endif

static const ConfigDoc doc[] = {
  {"pgm.controller.offset", CFG_INT, "1", "Compensate for MIDI controllers that number the programs from 1 to 128. Internally we use 0-127, as does MIDI. range: [0,1]"},
  {NULL}
};

const ConfigDoc *pgmDoc () {
  return doc;
}


#define MAXROWS 18
#define MAXCOLS  4

/**
 * Displays the number and names of the loaded programmes in a multicolumn
 * list on the given output stream.
 */
void listProgrammes (struct b_programme *p, FILE * fp) {
  int matrix [MAXROWS][MAXCOLS];
  int row;
  int col;
  int i;
  int mxUse = 0;
  int mxLimit = MAXROWS * MAXCOLS;

  fprintf(fp, "MIDI Program Table:\n");

  for (row = 0; row < MAXROWS; row++) {
    for (col = 0; col < MAXCOLS; col++) {
      matrix [row][col] = -1;
    }
  }

  for (i = row = col = 0; i < MAXPROGS; i++) {
    if (p->programmes[i].flags[0] & FL_INUSE) {
      if (mxUse < mxLimit) {
	matrix[row][col] = i;
	mxUse++;
	row++;
	if (MAXROWS <= row) {
	  row = 0;
	  col++;
	}
      }
    }
  }

  for (row = 0; row < MAXROWS; row++) {

    for (col = 0; col < MAXCOLS; col++) {
      int x = matrix[row][col];
      if (-1 < x) {
	fprintf (fp, "%3d:%-15.15s", x, p->programmes[x].name);
      }
      else {
	fprintf (fp, "%19s", " ");
      }
      if (col < 3) {
	fprintf (fp, " ");
      }
      else {
	fprintf (fp, "\n");
      }
    }
  }
}

/** walks through all available programs and counts the number of records
 * which are is in use.
 *
 * @param clear if set, all programs are erased
 */
int walkProgrammes (struct b_programme *p, int clear) {
  int cnt=0;
  int i;
  for (i=0; i < MAXPROGS; ++i) {
    if (clear) p->programmes[i].flags[0] &=~FL_INUSE;
    if (p->programmes[i].flags[0] & FL_INUSE) cnt++;
  }
  return cnt;
}

void loopProgammes (struct b_programme *p, int all,
  void (*cb)(int, int, const char*, void *), void *arg) {
  int i;
  int cnt=1;
  for (i=0 ; i < 128; ++i) {
    int pgmNr = i + p->MIDIControllerPgmOffset;
    if (all || p->programmes[pgmNr].flags[0] & FL_INUSE) {
      cb(cnt++, i + p->MIDIControllerPgmOffset, p->programmes[pgmNr].name, arg);
    }
  }
}

#ifndef PRG_MAIN
int formatProgram(Programme *p, char *out, int maxlen) {
  int len = 0;
  out[0]='\0';
  if (!(p->flags[0] & FL_INUSE)) {
    len += snprintf(out, maxlen, " --empty--\n");
    return len;
  }

  if (p->flags[0] & FL_DRAWBR) {
    len += snprintf(out+len, maxlen-len, "U: ");
    if (p->flags[0] & FL_DRWRND)
      len += snprintf(out+len, maxlen-len, "-random-");
    else
      len += format_drawbars(p->drawbars, out+len);
    len += snprintf(out+len, maxlen-len, "\n");
  }

  if (p->flags[0] & FL_LOWDRW) {
    len += snprintf(out+len, maxlen-len, "L: ");
    if (p->flags[0] & FL_DRWRND)
      len += snprintf(out+len, maxlen-len, "-random-");
    else
    len += format_drawbars(p->lowerDrawbars, out+len);
    len += snprintf(out+len, maxlen-len, "\n");
  }

  if (p->flags[0] & FL_PDLDRW) {
    len += snprintf(out+len, maxlen-len, "P: ");
    if (p->flags[0] & FL_DRWRND)
      len += snprintf(out+len, maxlen-len, "-random-");
    else
      len += format_drawbars(p->pedalDrawbars, out+len);
    len += snprintf(out+len, maxlen-len, "\n");
  }
  if (p->flags[0] & (FL_SCANNR|FL_VCRUPR|FL_VCRLWR)) {
    len += snprintf(out+len, maxlen-len, "vib: ");
    if (p->flags[0] & FL_SCANNR) {
      // FL_VCRUPR  FL_VCRLWR
      int knob = ((p->scanner & 0xf) << 1) - ((p->scanner & CHO_) ? 1 : 2);
      //len += snprintf(out+len, maxlen-len, "mode: ");
      switch (knob) {
	case 0: len += snprintf(out+len, maxlen-len, "v1 "); break;
	case 1: len += snprintf(out+len, maxlen-len, "c1 "); break;
	case 2: len += snprintf(out+len, maxlen-len, "v2 "); break;
	case 3: len += snprintf(out+len, maxlen-len, "c2 "); break;
	case 4: len += snprintf(out+len, maxlen-len, "v3 "); break;
	case 5: len += snprintf(out+len, maxlen-len, "c3 "); break;
	default: len += snprintf(out+len, maxlen-len, "? "); break;
      }
    }
    if (p->flags[0] & FL_VCRUPR) {
      len += snprintf(out+len, maxlen-len, "uppr: %s ", p->scanner & 0x200 ? "on" : "off");
    }
    if (p->flags[0] & FL_VCRLWR) {
      len += snprintf(out+len, maxlen-len, "lowr: %s ", p->scanner & 0x100 ? "on" : "off");
    }

    len += snprintf(out+len, maxlen-len, "\n");
  }
  if (p->flags[0] & (FL_PRCENA|FL_PRCVOL|FL_PRCSPD|FL_PRCHRM)) {
    len += snprintf(out+len, maxlen-len, "perc: ");
    if (p->flags[0] & FL_PRCENA) {
      len += snprintf(out+len, maxlen-len, "%s ", p->percussionEnabled ? "on" : "off");
    }
    if (p->flags[0] & FL_PRCVOL) {
      len += snprintf(out+len, maxlen-len, "%s ", p->percussionVolume ? "high" : "low");
    }
    if (p->flags[0] & FL_PRCSPD) {
      len += snprintf(out+len, maxlen-len, "%s ", p->percussionSpeed ? "fast" : "slow");
    }
    if (p->flags[0] & FL_PRCHRM) {
      len += snprintf(out+len, maxlen-len, "%s ", p->percussionHarmonic ? "2nd" : "3rd");
    }
    len += snprintf(out+len, maxlen-len, "\n");
  }
  if (p->flags[0] & FL_OVRSEL) {
    len += snprintf(out+len, maxlen-len, "overdrive: %s\n", p->overdriveSelect ? "bypass" : "on");
  }
  if (p->flags[0] & FL_ROTSPS) {
    len += snprintf(out+len, maxlen-len, "leslie: ");
    switch (p->rotarySpeedSelect){
      case WHIRL_FAST: len += snprintf(out+len, maxlen-len, "fast"); break;
      case WHIRL_SLOW: len += snprintf(out+len, maxlen-len, "slow"); break;
      case WHIRL_STOP: len += snprintf(out+len, maxlen-len, "stop"); break;
      default: len += snprintf(out+len, maxlen-len, "? "); break;
    }
    len += snprintf(out+len, maxlen-len, "\n");
  }
  if (p->flags[0] & FL_RVBMIX) {
    len += snprintf(out+len, maxlen-len, "reverb: %d%%\n", (int) rint(100.0*p->reverbMix));
  }
  if (p->flags[0] & (FL_KSPLTL|FL_KSPLTP|FL_TRA_PD|FL_TRA_LM|FL_TRA_UM)) {
    len += snprintf(out+len, maxlen-len, "keyboard-split change\n");
  }
  if (p->flags[0] & (FL_TRANSP|FL_TRCH_A|FL_TRCH_B|FL_TRCH_C)) {
    len += snprintf(out+len, maxlen-len, "transpose..\n");
  }
  return len;
}

static void save_pgm_state_cb(int fnid, const char *key, const char *kv, unsigned char val, void *arg) {
  Programme * PGM = (Programme*) arg;
  if      (!strcmp(key, "upper.drawbar16"))  {PGM->drawbars[0] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_DRAWBR;}
  else if (!strcmp(key, "upper.drawbar513")) {PGM->drawbars[1] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_DRAWBR;}
  else if (!strcmp(key, "upper.drawbar8"))   {PGM->drawbars[2] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_DRAWBR;}
  else if (!strcmp(key, "upper.drawbar4"))   {PGM->drawbars[3] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_DRAWBR;}
  else if (!strcmp(key, "upper.drawbar223")) {PGM->drawbars[4] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_DRAWBR;}
  else if (!strcmp(key, "upper.drawbar2"))   {PGM->drawbars[5] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_DRAWBR;}
  else if (!strcmp(key, "upper.drawbar135")) {PGM->drawbars[6] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_DRAWBR;}
  else if (!strcmp(key, "upper.drawbar113")) {PGM->drawbars[7] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_DRAWBR;}
  else if (!strcmp(key, "upper.drawbar1"))   {PGM->drawbars[8] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_DRAWBR;}

  else if (!strcmp(key, "lower.drawbar16"))  {PGM->lowerDrawbars[0] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_LOWDRW;}
  else if (!strcmp(key, "lower.drawbar513")) {PGM->lowerDrawbars[1] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_LOWDRW;}
  else if (!strcmp(key, "lower.drawbar8"))   {PGM->lowerDrawbars[2] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_LOWDRW;}
  else if (!strcmp(key, "lower.drawbar4"))   {PGM->lowerDrawbars[3] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_LOWDRW;}
  else if (!strcmp(key, "lower.drawbar223")) {PGM->lowerDrawbars[4] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_LOWDRW;}
  else if (!strcmp(key, "lower.drawbar2"))   {PGM->lowerDrawbars[5] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_LOWDRW;}
  else if (!strcmp(key, "lower.drawbar135")) {PGM->lowerDrawbars[6] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_LOWDRW;}
  else if (!strcmp(key, "lower.drawbar113")) {PGM->lowerDrawbars[7] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_LOWDRW;}
  else if (!strcmp(key, "lower.drawbar1"))   {PGM->lowerDrawbars[8] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_LOWDRW;}

  else if (!strcmp(key, "pedal.drawbar16"))  {PGM->pedalDrawbars[0] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_PDLDRW;}
  else if (!strcmp(key, "pedal.drawbar513")) {PGM->pedalDrawbars[1] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_PDLDRW;}
  else if (!strcmp(key, "pedal.drawbar8"))   {PGM->pedalDrawbars[2] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_PDLDRW;}
  else if (!strcmp(key, "pedal.drawbar4"))   {PGM->pedalDrawbars[3] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_PDLDRW;}
  else if (!strcmp(key, "pedal.drawbar223")) {PGM->pedalDrawbars[4] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_PDLDRW;}
  else if (!strcmp(key, "pedal.drawbar2"))   {PGM->pedalDrawbars[5] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_PDLDRW;}
  else if (!strcmp(key, "pedal.drawbar135")) {PGM->pedalDrawbars[6] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_PDLDRW;}
  else if (!strcmp(key, "pedal.drawbar113")) {PGM->pedalDrawbars[7] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_PDLDRW;}
  else if (!strcmp(key, "pedal.drawbar1"))   {PGM->pedalDrawbars[8] = rint((127-val)*8.0/127.0); PGM->flags[0] |= FL_PDLDRW;}

  else if (!strcmp(key, "percussion.enable"   )) {PGM->percussionEnabled = val > 63 ? TRUE : FALSE; PGM->flags[0] |= FL_PRCENA;}
  else if (!strcmp(key, "percussion.volume"   )) {PGM->percussionVolume = val/127.0; PGM->flags[0] |= FL_PRCVOL;}
  else if (!strcmp(key, "percussion.decay"    )) {PGM->percussionSpeed = val > 63 ? TRUE : FALSE; PGM->flags[0] |= FL_PRCSPD;}
  else if (!strcmp(key, "percussion.harmonic" )) {PGM->percussionHarmonic = val > 63 ? TRUE : FALSE; PGM->flags[0] |= FL_PRCHRM;}
  else if (!strcmp(key, "overdrive.enable"    )) {PGM->overdriveSelect = val > 63 ? FALSE : TRUE; PGM->flags[0] |= FL_OVRSEL;}
  else if (!strcmp(key, "reverb.mix"          )) {PGM->reverbMix = val/127.0; PGM->flags[0] |= FL_RVBMIX;}
  else if (!strcmp(key, "rotary.speed-select" )) {
    const int hr = (val / 45) % 3; // horn 0:off, 1:chorale  2:tremolo
    //const int bf = (val / 15) % 3; // drum 0:off, 1:chorale  2:tremolo
    if (hr == 0) PGM->rotarySpeedSelect = WHIRL_STOP;
    if (hr == 1) PGM->rotarySpeedSelect = WHIRL_SLOW;
    if (hr == 2) PGM->rotarySpeedSelect = WHIRL_FAST;
    PGM->flags[0] |= FL_ROTSPS;
  }
  else if (!strcmp(key, "vibrato.routing"     )) {
    PGM->scanner |= ((val>>5) & 1 ) ?  0x100 : 0; //lower
    PGM->scanner |= ((val>>5) & 2 ) ?  0x200 : 0; //upper
    PGM->flags[0] |= FL_VCRUPR | FL_VCRLWR;}
  else if (!strcmp(key, "vibrato.knob"        )) {
    int u = val / 23;
    if (u&1) {
      PGM->scanner |= CHO_;
      PGM->scanner |= (u>>1) + 1;
    } else {
      PGM->scanner |= (u>>1) + 1;
    }

    PGM->flags[0] |= FL_SCANNR;}
}

#include "state.h"
int saveProgramm(void *instance, int p, char *name, int flagmask) {
  b_instance * inst = (b_instance*) instance;
  p += inst->progs->MIDIControllerPgmOffset;
  if ((p < 0) || (p >= MAXPROGS) || !name) {
    return -1;
  }
  Programme * PGM = &(inst->progs->programmes[p]);
  memset(PGM, 0, sizeof(Programme));
  strcat(PGM->name, name);
  rc_loop_state(inst->state, save_pgm_state_cb, PGM);
  PGM->flags[0] &= ~flagmask;
  PGM->flags[0] |= FL_INUSE;
#if 0
  char tmp[256];
  formatProgram(PGM, tmp, 256);
  printf("SAVED STATE: %s\n", tmp);
#endif
  return 0;
}

void writeProgramm(int pgmNr, Programme *p, const char *sep, FILE * fp) {
  char tmp[24];
  fprintf(fp, "%d {%s  name=\"%s\"", pgmNr, sep, p->name);
  if ((p->flags[0] & FL_DRAWBR) && !(p->flags[0] & FL_DRWRND)) {
    format_drawbars(p->drawbars, tmp);
    fprintf(fp, "%s, drawbarsupper=\"%s\"", sep, tmp);
  }
  if ((p->flags[0] & FL_LOWDRW) && !(p->flags[0] & FL_DRWRND)) {
    format_drawbars(p->lowerDrawbars, tmp);
    fprintf(fp, "%s, drawbarslower=\"%s\"", sep, tmp);
  }
  if ((p->flags[0] & FL_PDLDRW) && !(p->flags[0] & FL_DRWRND)) {
    format_drawbars(p->pedalDrawbars, tmp);
    fprintf(fp, "%s, drawbarspedals=\"%s\"", sep, tmp);
  }
  if (p->flags[0] & FL_SCANNR) {
    int knob = ((p->scanner & 0xf) << 1) - ((p->scanner & CHO_) ? 1 : 2);
    fprintf(fp, "%s, vibrato=", sep);
    switch (knob) {
      case 0: fprintf(fp, "v1"); break;
      case 1: fprintf(fp, "c1"); break;
      case 2: fprintf(fp, "v2"); break;
      case 3: fprintf(fp, "c2"); break;
      case 4: fprintf(fp, "v3"); break;
      case 5: fprintf(fp, "c3"); break;
      default: break;  // XXX
    }
  }
  if (p->flags[0] & FL_VCRUPR) { fprintf(fp, "%s, vibratoupper=%s", sep, (p->scanner&0x200) ? "on" : "off"); }
  if (p->flags[0] & FL_VCRLWR) { fprintf(fp, "%s, vibratolower=%s", sep, (p->scanner&0x100) ? "on" : "off"); }
  if (p->flags[0] & FL_PRCENA) { fprintf(fp, "%s, perc=%s", sep, p->percussionEnabled ? "on" : "off"); }
  if (p->flags[0] & FL_PRCVOL) { fprintf(fp, "%s, percvol=%s", sep, p->percussionVolume ? "soft" : "normal"); }
  if (p->flags[0] & FL_PRCSPD) { fprintf(fp, "%s, percspeed=%s", sep, p->percussionSpeed ? "fast" : "slow"); }
  if (p->flags[0] & FL_PRCHRM) { fprintf(fp, "%s, percharm=%s", sep, p->percussionHarmonic ? "2nd" : "3rd"); }
  if (p->flags[0] & FL_OVRSEL) { fprintf(fp, "%s, overdrive=%s", sep, p->overdriveSelect ? "off" : "on"); }
  if (p->flags[0] & FL_RVBMIX) { fprintf(fp, "%s, reverbmix=%f", sep, p->reverbMix); }
  if (p->flags[0] & FL_ROTSPS) {
    fprintf(fp, "%s, rotaryspeed=", sep);
    switch(p->rotarySpeedSelect) {
      case WHIRL_FAST: fprintf(fp , "fast"); break;
      case WHIRL_SLOW: fprintf(fp , "slow"); break;
      case WHIRL_STOP: fprintf(fp , "stop"); break;
      default: break;  // XXX
    }
  }

  if (p->flags[0] & FL_KSPLTL) { fprintf(fp, "%s, keysplitlower=%d", sep, p->keyboardSplitLower); }
  if (p->flags[0] & FL_KSPLTP) { fprintf(fp, "%s, keysplitpedals=%d", sep, p->keyboardSplitPedals); }

  if (p->flags[0] & FL_TRANSP) { fprintf(fp, "%s, transpose=%d", sep, p->transpose[TR_TRANSP]); }
  if (p->flags[0] & FL_TRCH_A) { fprintf(fp, "%s, transposeupper=%d", sep, p->transpose[TR_CHNL_A]); }
  if (p->flags[0] & FL_TRCH_B) { fprintf(fp, "%s, transposelower=%d", sep, p->transpose[TR_CHNL_B]); }
  if (p->flags[0] & FL_TRCH_C) { fprintf(fp, "%s, transposepedals=%d", sep, p->transpose[TR_CHNL_C]); }

  if (p->flags[0] & FL_TRA_PD) { fprintf(fp, "%s, trssplitpedals=%d", sep, p->transpose[TR_CHA_PD]); }
  if (p->flags[0] & FL_TRA_LM) { fprintf(fp, "%s, trssplitlower=%d", sep, p->transpose[TR_CHA_LM]); }
  if (p->flags[0] & FL_TRA_UM) { fprintf(fp, "%s, trssplitupper=%d", sep, p->transpose[TR_CHA_UM]); }

  fprintf(fp, "%s}\n",sep);
}

void exportProgramms(struct b_programme *pgm, FILE * fp) {
  int i;
  for (i=0 ; i < 128; ++i) {
    int pgmNr = i + pgm->MIDIControllerPgmOffset;
    if (!(pgm->programmes[pgmNr].flags[0] & FL_INUSE)) {
      continue;
    }
    writeProgramm(pgmNr, &pgm->programmes[pgmNr], "\n    ", fp);
  }
}

struct b_programme *allocProgs() {
  struct b_programme *p = (struct b_programme*) calloc(1, sizeof(struct b_programme));
  if (!p) return NULL;
  p->previousPgmNr = -1;
  p->MIDIControllerPgmOffset = 1;
  memcpy(p->programmes, defaultprogrammes, sizeof(Programme) * MAXPROGS);
  return (p);
}

void freeProgs(struct b_programme *p) {
  free(p);
}
#endif


#ifdef PRG_MAIN
#include "pgmParser.h"

void hardcode_program (struct b_programme *p, FILE * fp) {
  int i;
  fprintf(fp, "/* generated by programd */\n");
  fprintf(fp, "static const Programme defaultprogrammes[MAXPROGS] = {\n");
  for (i=0 ; i < MAXPROGS; ++i) {
    int j;
    fprintf(fp,"  {");
    fprintf(fp,"\"%s\", {", p->programmes[i].name);
    for (j=0;j<1;++j) fprintf(fp,"%u, ", p->programmes[i].flags[j]);
    fprintf(fp,"}, {");
    for (j=0;j<9;++j) fprintf(fp,"%u, ", p->programmes[i].drawbars[j]);
    fprintf(fp,"}, {");
    for (j=0;j<9;++j) fprintf(fp,"%u, ", p->programmes[i].lowerDrawbars[j]);
    fprintf(fp,"}, {");
    for (j=0;j<9;++j) fprintf(fp,"%u, ", p->programmes[i].pedalDrawbars[j]);
    fprintf(fp,"}, ");
    fprintf(fp,"%d, ", p->programmes[i].keyAttackEnvelope);
    fprintf(fp,"%f, ", p->programmes[i].keyAttackClickLevel);
    fprintf(fp,"%f, ", p->programmes[i].keyAttackClickDuration);
    fprintf(fp,"%d, ", p->programmes[i].keyReleaseEnvelope);
    fprintf(fp,"%f, ", p->programmes[i].keyReleaseClickLevel);
    fprintf(fp,"%f, ", p->programmes[i].keyReleaseClickDuration);
    fprintf(fp,"%d, ", p->programmes[i].scanner);
    fprintf(fp,"%d, ", p->programmes[i].percussionEnabled);
    fprintf(fp,"%d, ", p->programmes[i].percussionVolume);
    fprintf(fp,"%d, ", p->programmes[i].percussionSpeed);
    fprintf(fp,"%d, ", p->programmes[i].percussionHarmonic);
    fprintf(fp,"%d, ", p->programmes[i].overdriveSelect);
    fprintf(fp,"%d, ", p->programmes[i].rotaryEnabled);
    fprintf(fp,"%d, ", p->programmes[i].rotarySpeedSelect);
    fprintf(fp,"%f, ", p->programmes[i].reverbMix);
    fprintf(fp,"%d, ", p->programmes[i].keyboardSplitLower);
    fprintf(fp,"%d, ", p->programmes[i].keyboardSplitPedals);
    fprintf(fp,"{");
    for (j=0;j<7;++j) fprintf(fp,"%d, ", p->programmes[i].transpose[j]);
    fprintf(fp,"}");

    fprintf(fp,"},\n");
  }
  fprintf(fp,"};\n");
}


int main (int argc, char **argv) {
  struct b_programme p;
  memset(&p, 0, sizeof(struct b_programme));
  p.previousPgmNr = -1;
  p.MIDIControllerPgmOffset = 1;

  if (argc < 2) return -1;
  if (loadProgrammeFile (&p, argv[1]) != 0 /* P_OK */) return -1;
  listProgrammes(&p, stderr);
  hardcode_program(&p, stdout);
  return 0;
}
#endif
/* vi:set ts=8 sts=2 sw=2: */
