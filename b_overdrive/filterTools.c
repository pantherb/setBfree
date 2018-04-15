/* setBfree - DSP tonewheel organ
 *
 * Copyright (C) 2003-2004 Fredrik Kilander <fk@dsv.su.se>
 * Copyright (C) 2008-2018 Robin Gareus <robin@gareus.org>
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

/* filterTools.c */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "filterTools.h"

/* ************************************************************************
 * Windowing functions. These functions return a scale factor between 0-1.
 * It is applied to an array of signals so that they may be scaled
 * according to their position in the array.
 *
 * @param i  Value w's position in array (m/2 is middle element)
 * @param m  Total number of elements.
 *
 * For example, if the kernel has three elements, w[0], w[1] and w[2], then
 * the calls to the Hamming window function would be:
 *
 * w[0] = wdw_Hamming (w[0], 0, 3);
 *
 * The middle element is typically not called because the sinc function
 * commonly used for the left and right halfs collapse there into a
 * division by zero.
 *
 * w[2] = wdw_Hamming (w[2], 2, 3);
 *
 * The argument to cos() will typically go from 0-2pi, ie a full circle.
 * So as i (element index) is incremented from 0 to m-1, the Hanning window
 * expression   0.5 - (0.5 * cos(f(i)))  will take the values
 *
 *             [0, ..., 0.5, ..., 1.0, ..., 0.5, ..., 0]
 *              0pi     0.5pi     1pi       1.5pi    2pi
 *
 * which is the positive half of a sinusoid. The shape of the resulting
 * curve is modified by altering the coefficients slightly (Hamming) or
 * by introducing a touch of the second harmonic (Blackman).
 */

double
wdw_Hamming (int i, int m)
{
	assert (i < m);
	return (0.54 - (0.46 * cos ((2.0 * M_PI * (double)i) / (double)(m - 1))));
}

double
wdw_Blackman (int i, int m)
{
	assert (i < m);
	return (0.42 - (0.50 * cos (2.0 * M_PI * ((double)i / (double)(m - 1)))) + (0.08 * cos (4.0 * M_PI * ((double)i / (double)(m - 1)))));
}

double
wdw_Hanning (int i, int m)
{
	assert (i < m);
	return (0.5 - (0.5 * cos ((2.0 * M_PI * (double)i) / (double)(m - 1))));
}

/* ************************************************************************
 *
 * Creates a low-pass filter and applies the requested window function.
 *
 * @param fc  Cutoff frequency
 * @param wdw Requested window function
 * @param a   Kernel weights.
 * @param m   Length of filter kernel (should be odd).
 */

void
sincApply (double fc, int wdw, double a[], int m)
{
	int    i;
	int    Mp = m - 1;
	double sum;

	for (i = 0; i < m; i++) {
		if ((i - (Mp / 2)) == 0) {
			a[i] = 2.0 * M_PI * fc;
		} else {
			double k = (double)(i - (Mp / 2));
			a[i]     = sin (2.0 * M_PI * fc * k) / k;
			/* Select window here */
			switch (wdw) {
				case WDW_HAMMING:
					a[i] *= wdw_Hamming (i, m);
					break;
				case WDW_BLACKMAN:
					a[i] *= wdw_Blackman (i, m);
					break;
				case WDW_HANNING:
					a[i] *= wdw_Hanning (i, m);
					break;
				default:
					assert (0);
			}
		}
	}

	/* Sum all weights */

	for (i = 0, sum = 0.0; i < m; i++) {
		sum += a[i];
	}

	/* Normalize to unit gain */

	for (i = 0; i < m; i++) {
		a[i] /= sum;
	}
}
