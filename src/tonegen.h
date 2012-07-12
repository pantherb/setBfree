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
 * tonegen.h
 */
#ifndef TONEGEN_H
#define TONEGEN_H

#include "cfgParser.h"

#define TG_91FB00 0		/* 91 wheels no lower foldback */
#define TG_82FB09 1		/* 82 wheels, 9 note lower folback */
#define TG_91FB12 2		/* 91 wheels, 12 note lower foldback */

#define firstMIDINote 36
#define lastMIDINote 108

#define ENV_CLICK  0		/* Key-click spark noise simulation */
#define ENV_COSINE 1		/* Cosine, bell, sigmoid fade */
#define ENV_LINEAR 2		/* Linear fade */
#define ENV_SHELF  3		/* Step */
#define ENV_CLICKMODELS 4

#define NOF_BUSES 27		/* Nof of drawbars/buses */

extern void setToneGeneratorModel (int variant);
extern void setWavePrecision (double precision);
extern void setTuning (double refA_Hz);
extern void setVibratoUpper (int isEnabled);
extern void setVibratoLower (int isEnabled);
extern void setPercussionEnabled (int isEnabled);
extern void setPercussionVolume (int isSoft);
extern void setPercussionFast (int isFast);
extern void setPercussionFirst (int isFirst);
extern void setNormalPercussionPercent (int percent);
extern void setSoftPercussionPercent (int percent);
extern void setFastPercussionDecay (double seconds);
extern void setSlowPercussionDecay (double seconds);
extern void setEnvAttackModel (int model);
extern void setEnvReleaseModel (int model);
extern void setEnvAttackClickLevel (double u);
extern void setEnvReleaseClickLevel (double u);
extern void setKeyClick (int v);
extern int  oscConfig (ConfigContext * cfg);
extern const ConfigDoc *oscDoc ();
extern void initToneGenerator ();
extern void freeToneGenerator ();

extern void oscKeyOff (unsigned char midiNote);
extern void oscKeyOn (unsigned char midiNote);
extern void setDrawBars (unsigned int manual, unsigned int setting []);
extern void oscGenerateFragment (float * buf, size_t lengthSamples);
extern void diagActive ();
#endif /* TONEGEN_H */
