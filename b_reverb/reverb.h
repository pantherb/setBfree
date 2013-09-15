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

#ifndef REVERB_H
#define REVERB_H


#define RV_NZ 7
struct b_reverb {
	/* static buffers, pointers */
	float *delays[RV_NZ]; /**< delay line buffer */

	float * idx0[RV_NZ];	/**< Reset pointer ref delays[]*/
	float * idxp[RV_NZ];	/**< Index pointer ref delays[]*/
	float * endp[RV_NZ];	/**< End pointer   ref delays[]*/

	float gain[RV_NZ];    /**< feedback gains */
	float yy1; /**< Previous output sample */
	float y_1; /**< Feedback sample */

	/* static config */
	int end[RV_NZ];
	double SampleRateD;

	/* dynamic config */
	float inputGain;	/**< Input gain value */
	float fbk;	/**< Feedback gain */
	float wet;	/**< Output dry gain */
	float dry;	/**< Output wet gain */

};

#include "../src/cfgParser.h"
extern struct b_reverb *allocReverb();
void freeReverb(struct b_reverb *r);

extern int reverbConfig (struct b_reverb *r, ConfigContext * cfg);

extern const ConfigDoc *reverbDoc ();

extern void setReverbInputGain (struct b_reverb *r, float g);

extern void setReverbOutputGain (struct b_reverb *r, float g);

extern void setReverbMix (struct b_reverb *r, float g);

extern void setReverbDry (struct b_reverb *r, float g);

extern void setReverbWet (struct b_reverb *r, float g);

extern void initReverb (struct b_reverb *r, void *m, double rate);

extern float * reverb (struct b_reverb *r, const float * inbuf, float * outbuf, size_t bufferLengthSamples);

#endif /* REVERB_H */
