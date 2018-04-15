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

#ifndef EQCOMP_H
#define EQCOMP_H

/* clang-format off */

/* Filter type selection values */
#define EQC_LPF   0 /* lowpass filter */
#define EQC_HPF   1 /* highpass filter */
#define EQC_BPF0  2 /* Bandpass filter with constant skirt gain */
#define EQC_BPF1  3 /* Bandpass filter with 0 db peak gain  */
#define EQC_NOTCH 4 /* Notch filter */
#define EQC_APF   5 /* Allpass filter */
#define EQC_PEQ   6 /* Peaking eq filter */
#define EQC_LOW   7 /* Low shelf filter */
#define EQC_HIGH  8 /* High shelf filter */

/* Coefficient access symbols */
#define EQC_B0 0
#define EQC_B1 1
#define EQC_B2 2
#define EQC_A0 3
#define EQC_A1 4
#define EQC_A2 5

/* clang-format on */

/* Function prototypes */
extern const char* eqGetTypeString (int t);

extern void eqCompute (int     type,
                       double  fqHz,
                       double  Q,
                       double  dbG,
                       double* C,
                       double  SampleRateD);

#endif /* EQCOMP_H */
