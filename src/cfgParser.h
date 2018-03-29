/* setBfree - DSP tonewheel organ
 *
 * Copyright (C) 2003-2004 Fredrik Kilander <fk@dsv.su.se>
 * Copyright (C) 2008-2015 Robin Gareus <robin@gareus.org>
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
 * cfgParser.h
 */
#ifndef _CFGPARSER_H_
#define _CFGPARSER_H_

#include <stdio.h>

/* clang-format off */
#define INCOMPLETE_DOC "", 0, 0, 0
#define DOC_SENTINEL {NULL, CFG_TEXT, "", "", "", 0, 0, 0}
/* clang-format on */

/* some filters - in particular butterworth shelfing -
 * end up producing denormal-values when fed with zeros */
#define DENORMAL_HACK (1e-14)
#define IS_DENORMAL(f) (((*(unsigned int *)&f)&0x7f800000)==0)

typedef struct _configContext {
  const char * fname;
  int    linenr;
  const char * name;
  const char * value;
} ConfigContext;

enum conftype {
  CFG_TEXT = 0,
  CFG_DOUBLE,
  CFG_DECIBEL, /**< double; only relevant for GUI-formatting and ui_step */
  CFG_FLOAT,
  CFG_INT,
  CFG_LAST
};


typedef struct _configDoc {
  const char * name; /**< parameter name */
  enum conftype type;/**< parameter type */
  char const * dflt; /**< default value as text */
  char const * desc; /**< descition */
  char const * unit; /**< unit */
  const float min, max; /**< min/max range where applicable or 0,0 for unbound */
  const float ui_step;  /**< suggested step size for GUI */
} ConfigDoc;

void parseConfigurationLine (
    void * instance,
    const char * fname,
    int    lineNumber,
    char * oneLine);

int  parseConfigurationFile (void * instance, const char * fname);
void dumpConfigDoc ();
int evaluateConfigKeyValue(void *inst, const char *key, const char *value);
void showConfigfileContext (ConfigContext * cfg, const char * msg);
void configIntUnparsable (ConfigContext * cfg);
void configIntOutOfRange (ConfigContext * cfg, int min, int max);
void configDoubleUnparsable (ConfigContext * cfg);

void setConfigRangeInt (int * vp, ConfigContext * cfg);
void setConfigInt (int * vp, ConfigContext * cfg);
void setConfigDouble (double * vp, ConfigContext * cfg);
const char * getConfigValue (ConfigContext * cfg);
int  getConfigParameter_d (const char * par,
                           ConfigContext * cfg,
                           double * dp);
int  getConfigParameter_i (const char * par,
                           ConfigContext * cfg,
                           int * dp);
int  getConfigParameter_f (const char * par,
                           ConfigContext * cfg,
                           float * dp);

int getConfigParameter_ir (const char * par,
                           ConfigContext * cfg,
                           int * ip,
                           int lowInc,
                           int highInc);

int getConfigParameter_dr (const char * par,
                           ConfigContext * cfg,
                           double * dp,
                           double lowInc,
                           double highInc);

int getConfigParameter_fr (const char * par,
                           ConfigContext * cfg,
                           float * fp,
                           float lowInc,
                           float highInc);
#endif /* _CONFIG_H_ */
