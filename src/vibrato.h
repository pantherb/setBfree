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

#ifndef SCANNER_H
#define SCANNER_H

#include "cfgParser.h"

#define VIB1 0x01
#define VIB2 0x02
#define VIB3 0x03
#define CHO_ 0x80		/* The bit that turns on chorus. */
#define CHO1 0x81
#define CHO2 0x82
#define CHO3 0x83

extern void setVibrato (int select);

extern void setScannerAdvance (int forward);

extern void initVibrato ();

extern int scannerConfig (ConfigContext * cfg);
extern const ConfigDoc *scannerDoc ();

extern float * vibratoProc (float * inbuffer, float * outbuffer, size_t bufferLengthSamples);

#endif /* SCANNER_H */
