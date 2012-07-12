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

extern int pgmConfig (ConfigContext * cfg);
extern const ConfigDoc *pgmDoc ();

extern void installProgram (unsigned char uc);

extern void listProgrammes (FILE * fp);
extern int walkProgrammes (int clear);

extern void setDisplayPgmChanges (int doDisplay);

extern int bindToProgram (char * fileName,
			  int    lineNumber,
			  int    pgmnr,
			  char * sym,
			  char * val);

#endif /* PROGRAM_H */
