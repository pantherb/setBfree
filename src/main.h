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

#ifndef MAIN_H
#define MAIN_H

#include "cfgParser.h"

/* TODO: split those into common.h
 *or better just replace TRUE/FALSE throughout the program
 */
#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#if 0 // drawbar presets -- unused ?
#define B___16 0
#define B5_1_3 1
#define B____8 2
#define B____4 3
#define B2_2_3 4
#define B____2 5
#define B1_3_5 6
#define B1_1_3 7
#define B____1 8
#endif
#define B_size 9

extern int mainConfig (ConfigContext * cfg);
extern const ConfigDoc *mainDoc ();
extern void listCCAssignments(void *mctl, FILE * fp);

extern double SampleRateD;

#endif /* MAIN_H */
