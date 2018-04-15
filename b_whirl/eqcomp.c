/* setBfree - DSP tonewheel organ
 *
 * Copyright (C) 2003-2004 Fredrik Kilander <fk@dsv.su.se>
 * Copyright (C) 2008-2018 Robin Gareus <robin@gareus.org>
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define _XOPEN_SOURCE 700
/* clang-format off */

#include "eqcomp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

/* Computes biquad EQ filter settings.
 * Based on 'Cookbook formulae for audio EQ biquad filter coefficents'
 * by Robert Bristow-Johnson <robert@wavemechanics.com>
 *
 *
 * The functions below compute the coefficients for a single IIR stage.
 * Consider cascading two or more stages if you need a more powerful filter.
 * A single stage can easily become unstable if the parameters request too
 * much - the roundoff errors due to finite word lengths accumulate in the
 * feedback loop and result in, well, feedback.
 * Here a Direct Form I usage:
 *   y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
 * If that means nothing to you, here is a short explanation:
 *   y[n]   : The current output sample
 *   y[n-1] : The previous output sample
 *   y[n-2] : The output sample before the previous output sample
 *   x[n]   : The current input sample
 *   x[n-1] : The previous input sample
 *   x[n-2] : The input sample before the previous input sample
 * a1, a2, b0, b1, b2 : Filter coefficients.
 * Coefficient a0 is not present because it has already been used to
 * normalize the other coefficients.
 *
 * However, I usually use a Direct Form II implementation:
 *   static float Z0, Z1, Z2;
 *   ...
 *   Z0 = x - (a1 * Z1) - (a2 * Z2);
 *   y = (Z0 * b0) + (Z1 * b1) + (Z2 * b2);
 *   Z2 = Z1;
 *   Z1 = Z0;
 *
 * The functions take as their last argument an array of double or float.
 * In the eqcom.h file are constants that give the indexes for the six
 * coefficients. See each function for further information.
 */

const char* filterTypeNames[10] = {
	"LPF low-pass",
	"HPF high-pass",
	"BF0 band-pass 0",
	"BF1 band-pass 1",
	"NOT notch",
	"APF all-pass",
	"PEQ peaking eq",
	"LSH low shelf",
	"HSH high shelf",
	"???"
};

const char*
eqGetTypeString (int t)
{
	return filterTypeNames[((0 <= t) && (t < 9)) ? t : 9];
}

/*
 * eqCompute
 * Computes the filter coefficients for the given filter type.
 * @param type The filter type (see eqcomp.h)
 * @param fqHz Cutoff or resonant frequency in Hertz.
 * @param Q    The bandwidth of the filter (0.2 - 3)
 * @param dbG  Gain in dB (for certain types only, see below)
 * @param C    Array[6] of coefficients.
 */
void
eqCompute (int     type,
           double  fqHz,
           double  Q,
           double  dbG,
           double* C,
           double  SampleRateD)
{
	double A     = pow (10.0, (dbG / 40.0));
	double omega = (2.0 * M_PI * fqHz) / SampleRateD;
	double sin_  = sin (omega);
	double cos_  = cos (omega);
	double alpha = sin_ / (2.0 * Q);
	double beta  = sqrt (A) / Q;

	switch (type) {

		case EQC_LPF: /* Low Pass */
			C[EQC_B0] = (1.0 - cos_) / 2.0;
			C[EQC_B1] =  1.0 - cos_;
			C[EQC_B2] = (1.0 - cos_) / 2.0;
			C[EQC_A0] =  1.0 + alpha;
			C[EQC_A1] = -2.0 * cos_;
			C[EQC_A2] =  1.0 - alpha;
			break;

		case EQC_HPF: /* High Pass */
			C[EQC_B0] =  (1.0 + cos_) / 2.0;
			C[EQC_B1] = -(1.0 + cos_);
			C[EQC_B2] =  (1.0 + cos_) / 2.0;
			C[EQC_A0] =   1.0 + alpha;
			C[EQC_A1] =  -2.0 * cos_;
			C[EQC_A2] =   1.0 - alpha;
			break;

		case EQC_BPF0: /* Constant skirt gain, peak gain = Q */
			C[EQC_B0] =  sin_ / 2.0;
			C[EQC_B1] =  0.0;
			C[EQC_B2] = -sin_ / 2.0;
			C[EQC_A0] =  1.0 + alpha;
			C[EQC_A1] = -2.0 * cos_;
			C[EQC_A2] =  1.0 - alpha;
			break;

		case EQC_BPF1: /* Constant 0 dB peak gain */
			C[EQC_B0] =  alpha;
			C[EQC_B1] =  0.0;
			C[EQC_B2] = -alpha;
			C[EQC_A0] =  1.0 + alpha;
			C[EQC_A1] = -2.0 * cos_;
			C[EQC_A2] =  1.0 - alpha;
			break;

		case EQC_NOTCH: /* Notch filter */
			C[EQC_B0] =  1.0;
			C[EQC_B1] = -2.0 * cos_;
			C[EQC_B2] =  1.0;
			C[EQC_A0] =  1.0 + alpha;
			C[EQC_A1] = -2.0 * cos_;
			C[EQC_A2] =  1.0 - alpha;
			break;

		case EQC_APF: /* Allpass filter */
			C[EQC_B0] =  1.0 - alpha;
			C[EQC_B1] = -2.0 * cos_;
			C[EQC_B2] =  1.0 + alpha;
			C[EQC_A0] =  1.0 + alpha;
			C[EQC_A1] = -2.0 * cos_;
			C[EQC_A2] =  1.0 - alpha;
			break;

		case EQC_PEQ: /* Peaking EQ */
			C[EQC_B0] =  1.0 + (alpha * A);
			C[EQC_B1] = -2.0 * cos_;
			C[EQC_B2] =  1.0 - (alpha * A);
			C[EQC_A0] =  1.0 + (alpha / A);
			C[EQC_A1] = -2.0 * cos_;
			C[EQC_A2] =  1.0 - (alpha / A);
			break;

		case EQC_LOW: /* Low shelf */
			C[EQC_B0] =         A * ((A + 1) - ((A - 1) * cos_) + (beta * sin_));
			C[EQC_B1] = (2.0 * A) * ((A - 1) - ((A + 1) * cos_));
			C[EQC_B2] =         A * ((A + 1) - ((A - 1) * cos_) - (beta * sin_));
			C[EQC_A0] =              (A + 1) + ((A - 1) * cos_) + (beta * sin_);
			C[EQC_A1] =      -2.0 * ((A - 1) + ((A + 1) * cos_));
			C[EQC_A2] =              (A + 1) + ((A - 1) * cos_) - (beta * sin_);
			break;

		case EQC_HIGH: /* High shelf */
			C[EQC_B0] =          A * ((A + 1) + ((A - 1) * cos_) + (beta * sin_));
			C[EQC_B1] = -(2.0 * A) * ((A - 1) + ((A + 1) * cos_));
			C[EQC_B2] =          A * ((A + 1) + ((A - 1) * cos_) - (beta * sin_));
			C[EQC_A0] =               (A + 1) - ((A - 1) * cos_) + (beta * sin_);
			C[EQC_A1] =        2.0 * ((A - 1) - ((A + 1) * cos_));
			C[EQC_A2] =               (A + 1) - ((A - 1) * cos_) - (beta * sin_);
			break;
	}

	C[EQC_B0] /= C[EQC_A0];
	C[EQC_B1] /= C[EQC_A0];
	C[EQC_B2] /= C[EQC_A0];
	C[EQC_A1] /= C[EQC_A0];
	C[EQC_A2] /= C[EQC_A0];

}
