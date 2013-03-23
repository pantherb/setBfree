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
 * cfgParser.h
 */
#ifndef _CFGPARSER_H_
#define _CFGPARSER_H_

/* some filters - in particular butterworth shelfing -
 * end up producing denormal-values when fed with zeros */
#define DENORMAL_HACK (1e-20)
#define IS_DENORMAL(f) (((*(unsigned int *)&f)&0x7f800000)==0)

typedef struct _configContext {
  char * fname;
  int    linenr;
  const char * name;
  const char * value;
} ConfigContext;

enum conftype {
	CFG_TEXT = 0,
	CFG_DOUBLE,
	CFG_FLOAT,
	CFG_INT,
	CFG_LAST
};

typedef struct _configDoc {
  char * name; /**< parameter name */
	enum conftype type;
  char * dflt; /**< default value as text */
  char * desc; /**< descition */
} ConfigDoc;

extern void parseConfigurationLine (
				    void * instance,
				    char * fname,
				    int    lineNumber,
				    char * oneLine);

extern int  parseConfigurationFile (void * instance, char * fname);
extern void dumpConfigDoc ();
extern int evaluateConfigKeyValue(void *inst, const char *key, const char *value);
extern void showConfigfileContext (ConfigContext * cfg, char * msg);
extern void configIntUnparsable (ConfigContext * cfg);
extern void configIntOutOfRange (ConfigContext * cfg, int min, int max);
extern void configDoubleUnparsable (ConfigContext * cfg);

extern void setConfigRangeInt (int * vp, ConfigContext * cfg);
extern void setConfigInt (int * vp, ConfigContext * cfg);
extern void setConfigDouble (double * vp, ConfigContext * cfg);
extern const char * getConfigValue (ConfigContext * cfg);
extern int  getConfigParameter_d (char * par,
				  ConfigContext * cfg,
				  double * dp);
extern int  getConfigParameter_i (char * par,
				  ConfigContext * cfg,
				  int * dp);
extern int  getConfigParameter_f (char * par,
				  ConfigContext * cfg,
				  float * dp);

extern int getConfigParameter_ir (char * par,
				  ConfigContext * cfg,
				  int * ip,
				  int lowInc,
				  int highInc);

extern int getConfigParameter_dr (char * par,
				  ConfigContext * cfg,
				  double * dp,
				  double lowInc,
				  double highInc);

extern int getConfigParameter_fr (char * par,
				  ConfigContext * cfg,
				  float * fp,
				  float lowInc,
				  float highInc);
#endif /* _CONFIG_H_ */
