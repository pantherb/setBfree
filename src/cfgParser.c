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
 * config.c --- Parse a configuration file and build a key,value structure.
 * 14-may-2004/FK Dropped rotsim.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <assert.h>

#ifndef CFG_MAIN

#include "main.h"
#include "global_inst.h"
#include "program.h"
#ifdef HAVE_ZITACONVOLVE
#include "convolution.h"
#endif

#endif /* CFG_MAIN */

#include "cfgParser.h"

#define LINEBUFSZ 2048

/*
 * Each configurable module implements this function. The implementation
 * is idempotent. The most recent call defines the parameter's value.
 */
static int distributeParameter (b_instance* inst, ConfigContext * cfg) {

  int n = 0;

#ifdef CFG_MAIN

  printf ("%s:%d:[%s]=[%s]\n",
	  cfg->fname,
	  cfg->linenr,
	  cfg->name,
 	  cfg->value);

#else

  n += mainConfig (cfg);
  n += midiConfig (inst->midicfg, cfg);
  n += pgmConfig (cfg);
  n += oscConfig (inst->synth, cfg);
  n += scannerConfig (inst->synth, cfg);
  n += ampConfig (cfg);
  n += whirlConfig (inst->whirl, cfg);
  n += reverbConfig (inst->reverb, cfg);
#ifdef HAVE_ZITACONVOLVE
  n += convolutionConfig(cfg);
#endif

  if (n == 0) {
    printf ("%s:%d:%s=%s:Not claimed by any module.\n",
	    cfg->fname,
	    cfg->linenr,
	    cfg->name,
	    cfg->value);
  }

#endif /* CFG_MAIN */

  return n;
}

/*
 *
 */
void parseConfigurationLine (void *inst, char * fname, int lineNumber, char * oneLine) {
  char delim[] = "=\n";
  char * s = oneLine;
  char * name;
  char * value;

  while (isspace (*s)) s++;	/* Skip over leading spaces */
  if (*s == '\0') return;	/* Skip empty lines */
  if (*s == '#') return;	/* Skip comment lines */
  if (*s == '=') {
    fprintf (stderr,
	     "%s:line %d: empty parameter name.\n",
	     fname,
	     lineNumber);
    return;
  }

  if ((name = strtok (s, delim)) != NULL) {
    int i;

	for (i = strlen (name) - 1; isspace (name[i]); name[i] = '\0', i--);

	if ((value = strtok (NULL, delim)) != NULL) {
	  char * t;
	  while (isspace (value[0])) value++;
	  for (t = value; *t != '\0'; t++) {
	    if (*t == '#') {	/* Terminate value at comment */
	      *t = '\0';
	      break;
	    }
	  }
	}
	else {
	  value = "";
	}

	i = strlen (value);
	if (0 < i) {
	  for (i = i - 1;  isspace (value[i]); value[i] = '\0', i--);
	}

	if (strcasecmp (name, "config.read") == 0) {
	  parseConfigurationFile (inst, value);
	}
	else {
	  ConfigContext cfg;
	  cfg.fname  = fname;
	  cfg.linenr = lineNumber;
	  cfg.name   = name;
	  cfg.value  = value;
	  distributeParameter ((b_instance*) inst, & cfg);
	}
      }

}

#ifndef CFG_MAIN
/* text representation of enum conftype */
const char *conftypenames[CFG_LAST] = { "S", "D", "F", "I" };

/*
 *
 */
void formatDoc (char *modulename, const ConfigDoc *d) {
  printf("Parameters for '%s':\n", modulename);
  while (d && d->name) {
    if (strlen(d->name) >= 40) {
      fprintf(stderr, "PROPERTY NAME IS TOO LONG -- PLEASE REPORT THIS BUG\n");
    }
    printf("  %-40s   %s%s (%s)\n", d->name,
	conftypenames[d->type],
	(getCCFunctionId(d->name)<0)?" ":"*",
	(strlen(d->dflt)>0)?d->dflt:"?");
    if (strlen(d->desc)>0) {
      // TODO: word-wrap description, with indent 4
      printf("    %s\n", d->desc);
    }
    d++;
  }
  printf("\n");
}

extern const char * filterTypeNames [10]; //defined in b3_whirl/eqcomp.c
/*
 *
 */
void dumpConfigDoc () {
  printf(
      "Instrument Properties:\n"
      "  Below is a list of available property-value pairs and their default\n"
      "  values. The default value is omitted \"(-)\" for properties which\n"
      "  contain an array of values.\n"
      "  \n"
      "  The type identifiers are:\n"
      "  'S': text-string, 'I': integer, 'F': float, 'D': double-precision float.\n"
      "  \n"
      "  Properties marked with an asterisk (*), are available as MIDI CC\n"
      "  functions. When used as CC, the values 0-127 (MIDI data) is mapped\n"
      "  to a range of values appropriate to the function.\n"
      "  In config-files or on the command-line you must you the type as\n"
      "  specified e.g. \"osc.temperament=gear60 osc.wiring-crosstalk=0.2\"\n"
      "\n"
      );

  formatDoc("Main", mainDoc());
  formatDoc("MIDI Parser", midiDoc());
  formatDoc("MIDI Program Parser", pgmDoc());
  formatDoc("Tone Generator", oscDoc());
  formatDoc("Vibrato Effect", scannerDoc());
  formatDoc("Preamp/Overdrive Effect", ampDoc());
  formatDoc("Leslie Cabinet Effect", whirlDoc());
  formatDoc("Reverb Effect", reverbDoc());
#ifdef HAVE_ZITACONVOLVE
  formatDoc("Convolution Reverb Effect", convolutionDoc());
#endif

  printf("Filter Types (for Leslie):\n");
  int i;
  for (i=0;i<9;i++) {
    printf("  %d    %s\n", i, filterTypeNames[i]);
  }
  printf("Note that the gain parameter does not apply to type 0 Low-Pass-Filters.\n\n");

  printf(
  "Additional MIDI Control-Command Functions:\n"
  " These properties can not be modified directly, but are meant to be mapped\n"
  " to MIDI-controllers (see \"General Information\" above)\n"
  " e.g. \"midi.controller.upper.70=upper.drawbar16\".\n"
  );

  printf(
  "  {upper|lower|pedal}.drawbar<NUM>           I* (-)\n"
  "    where <NUM> is one of [16, 513, 8, 4, 223, 2, 135 , 113, 1].\n"
  "    Set MIDI-Controller IDs to adjust given drawbar. --\n"
  "    The range is inversely mapped to the position of the drawbar, so that fader-like controllers work in reverse, like real drawbars. Note that the MIDI controller values are quantized into 0 ... 8 to correspond to the nine discrete positions of the original drawbar system:\n"
  "    0:8 (loudest), 1-15:7, 16-31:6,  32-47:5, 48-63:4, 64-79:3, 80-92:2, 96-110:1, 111-127:0(off)\n"
  "  rotary.speed-preset                        I* (-)\n"
  "    set horn and drum speed; 0-stop, 1:slow, 2:fast\n"
  "  rotary.speed-toggle                        I* (-)\n"
  "    toggle rotary.speed-preset between 1/2\n"
  "  rotary.speed-select                        I* (-)\n"
  "    low-level access function 0..8 (3^2 combinations) [stop/slow/fast]^[horn|drum]\n"
  "  swellpedal1                                D* (0.7)\n"
  "    set swell pedal gain\n"
  "  swellpedal2                                D* (0.7)\n"
  "    identical to swellpedal1\n"
  "  vibrato.knob                               I* (0)\n"
  "   <22:vibrato1, <44:chorus1, <66:vibrato2, <88:chorus2, <110vibrato3, >=110:chorus3\n"
  "  vibrato.routing                            I* (0)\n"
  "    <32:off, <64:lower, <96:upper, >=96:both\n"
  "  percussion.enable                          I* (0)\n"
  "    <16:off, <63:normal, <112:soft, >=112:off\n"
  "  percussion.decay                           I* (0)\n"
  "    <64: fast-decay, >=64 slow decay\n"
  "  percussion.harmonic                        I* (0)\n"
  "    <64: third harmonic, >=64 second harmonic\n"

  );

  printf("\n");
}
#endif

/*
 *
 */
int parseConfigurationFile (void *inst, char * fname) {
  int lineNumber = 0;
  char lineBuf [LINEBUFSZ];
  FILE * fp;

  if ((fp = fopen (fname, "r")) == NULL) {
    perror (fname);
    return -1;
  }
  else {

    while (fgets (lineBuf, LINEBUFSZ, fp) != NULL) {
      lineNumber += 1;		/* Increment the linenumber. */
      parseConfigurationLine (inst, fname, lineNumber, lineBuf);
    }

    fclose (fp);
  }

  return 0;
}

/*
 *
 */
void showConfigfileContext (ConfigContext * cfg, char * msg) {

  assert (cfg != NULL);
  assert (msg != NULL);

  fprintf (stderr,
	   "%s:line %d:name %s:value %s:%s\n",
	   cfg->fname,
	   cfg->linenr,
	   cfg->name,
	   cfg->value,
	   msg);
}

/*
 *
 */
void configIntUnparsable (ConfigContext * cfg) {
  assert (cfg != NULL);
  showConfigfileContext (cfg, "value is not an integer");
}

/*
 *
 */
void configDoubleUnparsable (ConfigContext * cfg) {
  assert (cfg != NULL);
  showConfigfileContext (cfg, "value is not a real");
}

/*
 *
 */
void configDoubleOutOfRange (ConfigContext * cfg,
			     double lowest,
			     double highest) {
  char buf[256];
  assert (cfg != NULL);
  sprintf (buf, "real value out of range (%lf -- %lf)", lowest, highest);
  showConfigfileContext (cfg, buf);
}

/*
 *
 */
void configIntOutOfRange (ConfigContext * cfg, int lowest, int highest) {
  char buf[256];
  assert (cfg != NULL);
  sprintf (buf, "integer value out of range (%d -- %d)", lowest, highest);
  showConfigfileContext (cfg, buf);
}

/*
 * Assigns an sample range integer from the configuration context's value.
 * If the value is real (contains a period), it is interpreted as a
 * normalised quantity (-1.0 -- 1.0) and multiplied with 32767.0.
 * Otherwise the value is interpreted to be an integer and is assigned
 * as it is.
 */
void setConfigRangeInt (int * vp, ConfigContext * cfg) {

  assert (vp != NULL);
  assert (cfg != NULL);

  if (strchr (cfg->value, '.') != NULL) {
    double d;
    if (sscanf (cfg->value, "%lf", &d) == 1) {
      *vp = (int) (32767.0 * d);
    }
    else {
      configDoubleUnparsable (cfg);
    }
  }
  else {
    int k;
    if (sscanf (cfg->value, "%d", &k) == 1) {
      *vp = k;
    }
    else {
      configIntUnparsable (cfg);
    }
  }

}

/*
 * Assigns an integer from the configuration context's value.
 */
void setConfigInt (int * vp, ConfigContext * cfg) {
  int k;
  assert (vp != NULL);
  assert (cfg != NULL);
  if (sscanf (cfg->value, "%d", &k) == 1) {
    *vp = k;
  }
  else {
    configIntUnparsable (cfg);
  }
}

/*
 * Assigns a double from the configuration context's value.
 */
void setConfigDouble (double * vp, ConfigContext * cfg) {
  double d;
  assert (vp != NULL);
  assert (cfg != NULL);
  if (sscanf (cfg->value, "%lf", &d) == 1) {
    *vp = d;
  }
  else {
    configDoubleUnparsable (cfg);
  }
}

/*
 * Returns the configuration entry's value string.
 */
char * getConfigValue (ConfigContext * cfg) {
  return cfg->value;
}

/*
 * Helper function for other module's config routines.
 * This routine compares the supplied parameter name with the name
 * in the configuration context struct and when a match is found,
 * attempts to set the target of the supplied storage pointer.
 * @param par  Pointer to the parameter name string.
 * @param cfg  Pointer to the configuration context.
 * @param dp   Pointer the double to set if a match is found.
 * @returns    Zero if the parameter name did not match the name in
 *             the context, 1 if it did and an assignment was made
 *             -1 if the name matched but no assignment was made.
 */
int getConfigParameter_d (char * par, ConfigContext * cfg, double * dp) {

  assert (par != NULL);
  assert (cfg != NULL);
  assert (dp  != NULL);

  if (strcasecmp (cfg->name, par) == 0) {
    double a;
    if (sscanf (cfg->value, "%lf", &a) == 1) {
      *dp = a;
    }
    else {
      configDoubleUnparsable (cfg);
      return -1;
    }
    return 1;
  }
  return 0;
}

/*
 * Helper function for other module's config routines.
 * This routine compares the supplied parameter name with the name
 * in the configuration context struct and when a match is found,
 * attempts to set the target of the supplied storage pointer.
 * @param par  Pointer to the parameter name string.
 * @param cfg  Pointer to the configuration context.
 * @param ip   Pointer the int to set if a match is found.
 * @returns    Zero if the parameter name did not match the name in
 *             the context, 1 if it did and an assignment was made
 *             -1 if the name matched but no assignment was made.
 */
int getConfigParameter_i (char * par, ConfigContext * cfg, int * ip) {

  assert (par != NULL);
  assert (cfg != NULL);
  assert (ip  != NULL);

  if (strcasecmp (cfg->name, par) == 0) {
    int i;
    if (sscanf (cfg->value, "%d", &i) == 1) {
      *ip = i;
    }
    else {
      configIntUnparsable (cfg);
      return -1;
    }
    return 1;
  }
  return 0;
}

/*
 * Helper function for other module's config routines.
 * This routine compares the supplied parameter name with the name
 * in the configuration context struct and when a match is found,
 * attempts to set the target of the supplied storage pointer.
 * @param par  Pointer to the parameter name string.
 * @param cfg  Pointer to the configuration context.
 * @param fp   Pointer the float to set if a match is found.
 * @returns    Zero if the parameter name did not match the name in
 *             the context, 1 if it did and an assignment was made
 *             -1 if the name matched but no assignment was made.
 */
int getConfigParameter_f (char * par, ConfigContext * cfg, float * fp) {

  assert (par != NULL);
  assert (cfg != NULL);
  assert (fp  != NULL);

  if (strcasecmp (cfg->name, par) == 0) {
    float a;
    if (sscanf (cfg->value, "%f", &a) == 1) {
      *fp = a;
    }
    else {
      configDoubleUnparsable (cfg);
      return -1;
    }
    return 1;
  }
  return 0;
}

/*
 * Range-checking version of getConfigParameter_i(...)
 * @param par  Pointer to the parameter name string.
 * @param cfg  Pointer to the configuration context.
 * @param fp   Pointer the float to set if a match is found.
 * @param lowInc Low limit (inclusive)
 * @param highInc High limit (inclusive)
 * @returns    Zero if the parameter name did not match the name in
 *             the context, 1 if it did and an assignment was made
 *             -1 if the name matched but no assignment was made.
 */
int getConfigParameter_ir (char * par,
			   ConfigContext * cfg,
			   int * ip,
			   int lowInc,
			   int highInc) {
  int k;
  int rtn;

  assert (ip != NULL);
  assert (lowInc <= highInc);

  rtn = getConfigParameter_i (par, cfg, &k);
  if (rtn == 1) {
    if ((lowInc <= k) && (k <= highInc)) {
      *ip = k;
    }
    else {
      fprintf (stderr, "Value for config parameter %s is out range (%d--%d).",
	      cfg->name,
	      lowInc,
	      highInc);
      rtn = -1;
    }
  }
  return rtn;
}

/*
 * Double-type with range-checking.
 * @returns    Zero if the parameter name did not match the name in
 *             the context, 1 if it did and an assignment was made
 *             -1 if the name matched but no assignment was made.
 */
int getConfigParameter_dr (char * par,
			   ConfigContext * cfg,
			   double * dp,
			   double lowInc,
			   double highInc) {
  double d;
  int rtn;

  assert (dp != NULL);
  assert (lowInc <= highInc);

  rtn = getConfigParameter_d (par, cfg, &d);
  if (rtn == 1) {
    if ((lowInc <= d) && (d <= highInc)) {
      *dp = d;
    }
    else {
      configDoubleOutOfRange (cfg, lowInc, highInc);
      rtn = -1;
    }
  }
  return rtn;
}

/*
 * Float-type with range-checking.
 * @returns    Zero if the parameter name did not match the name in
 *             the context, 1 if it did and an assignment was made
 *             -1 if the name matched but no assignment was made.
 */
int getConfigParameter_fr (char * par,
			   ConfigContext * cfg,
			   float * fp,
			   float lowInc,
			   float highInc) {
  double d;
  int rtn;

  assert (fp != NULL);
  assert (lowInc <= highInc);

  rtn = getConfigParameter_dr (par, cfg, &d, (double) lowInc, (double)highInc);
  if (rtn == 1) {
    *fp = d;
  }
  return rtn;
}

#ifdef CFG_MAIN
int main (int argc, char **argv) {
  if (argc < 2) return -1;
  parseConfigurationFile (NULL, argv[1]);
  return 0;
}
#endif /* CFG_MAIN */

/* vi:set ts=8 sts=2 sw=2: */
