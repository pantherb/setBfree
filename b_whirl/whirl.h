/* setBfree - DSP tonewheel organ
 *
 * Copyright (C) 2003-2004 Fredrik Kilander <fk@dsv.su.se>
 * Copyright (C) 2008-2012 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2010 Ken Restivo <ken@restivo.org>
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

/* whirl.h */
#ifndef WHIRL_H
#define WHIRL_H

#include "../src/cfgParser.h" // ConfigContext
#include "../src/midi.h" // useMIDIControlFunction

extern int whirlConfig (ConfigContext * cfg);
extern const ConfigDoc *whirlDoc ();

extern void initWhirl ();
extern void setRevSelect (int n);

extern void whirlProc (const float * inbuffer,
		       float * outbL,
		       float * outbR,
		       size_t bufferLengthSamples);

extern void whirlProc2 (const float * inbuffer,
		        float * outL, float * outR,
		        float * outHL, float * outHR,
		        float * outDL, float * outDR,
		        size_t bufferLengthSamples);

#define WHIRL_FAST 2
#define WHIRL_SLOW 1
#define WHIRL_STOP 0

void useRevOption (int n);
void isetHornFilterAType (int v);
void fsetHornFilterAFrequency (float v);
void fsetHornFilterAQ (float v);
void fsetHornFilterAGain (float v);
void isetHornFilterBType (int v);
void fsetHornFilterBFrequency (float v);
void fsetHornFilterBQ (float v);
void fsetHornFilterBGain (float v);
#endif /* WHIRL_H */
/* vi:set ts=8 sts=2 sw=2: */
