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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "whirl.h"
#include "eqcomp.h"

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define B3W_URI "http://gareus.org/oss/lv2/b_whirl#simple"
#define B3W_URI_EXT "http://gareus.org/oss/lv2/b_whirl#extended"
#define B3W_URI_MOD "http://gareus.org/oss/lv2/b_whirl#mod"

typedef enum {
	B3W_INPUT = 0,
	B3W_OUTL,
	B3W_OUTR,

	B3W_REVSELECT, // 3

	B3W_HORNLVL,
	B3W_DRUMLVL,
	B3W_DRUMWIDTH,

	B3W_HORNRPMSLOW, // 7
	B3W_HORNRPMFAST,
	B3W_HORNACCEL,
	B3W_HORNDECEL,
	B3W_HORNBRAKE,

	B3W_FILTATYPE, // 12
	B3W_FILTAFREQ,
	B3W_FILTAQUAL,
	B3W_FILTAGAIN,

	B3W_FILTBTYPE, // 16
	B3W_FILTBFREQ,
	B3W_FILTBQUAL,
	B3W_FILTBGAIN,

	B3W_DRUMRPMSLOW, // 20
	B3W_DRUMRPMFAST,
	B3W_DRUMACCEL,
	B3W_DRUMDECEL,
	B3W_DRUMBRAKE,

	B3W_FILTDTYPE, // 25
	B3W_FILTDFREQ,
	B3W_FILTDQUAL,
	B3W_FILTDGAIN,

	B3W_HORNLEAK, // 29
	B3W_HORNRADIUS,
	B3W_DRUMRADIUS,
	B3W_HORNOFFX,
	B3W_HORNOFFZ,
	B3W_MICDIST,

	B3W_HORNRPM, // 35
	B3W_DRUMRPM,

	B3W_HORNANG,
	B3W_DRUMANG,

	B3W_GUINOTIFY,
	B3W_LINKSPEED, // 40
	B3W_MICANGLE,
	B3W_HORNWIDTH,
} PortIndex;

typedef struct {
	float *type, *freq, *qual, *gain; // ports
	iir_t *W[2]; /* pointers to coeffs, drum-filter x2 */
	/* state */
	float _f, _q, _g;
	int _t;
} Filter;

typedef struct {
	/* audio ports */
	float *input, *outL, *outR;

	/* control ports */
	float *rev_select; // speed select 0..8

	float *horn_brake, *horn_accel, *horn_decel, *horn_slow, *horn_fast;
	float *drum_brake, *drum_accel, *drum_decel, *drum_slow, *drum_fast;

	float *horn_level, *drum_level;
	float *drum_width, *horn_leak, *horn_width;

	float *horn_radius, *drum_radius;
	float *horn_xoff, *horn_zoff, *mic_dist, *mic_angle;

	float *p_resend_trigger; // GUI retrigger
	float *p_link_speed; // GUI setting

	/* output ports */
	float *c_horm_rpm, *c_drum_rpm;
	float *c_horm_ang, *c_drum_ang;

	// Filter/EQ states & port-map
	Filter flt[3];

	/* internal state */
	float o_rev_select;
	float o_horn_brake, o_horn_accel, o_horn_decel, o_horn_slow, o_horn_fast;
	float o_drum_brake, o_drum_accel, o_drum_decel, o_drum_slow, o_drum_fast;
	float o_horn_level, o_drum_level;
	float o_drum_width, o_horn_leak, o_horn_width;

	float o_horn_radius, o_drum_radius;
	float o_horn_xoff, o_horn_zoff, o_mic_dist, o_mic_angle;

	int spd_horn, spd_drum, last_spd;

	// cached coefficients (for dB values)
	float x_drum_width;
	float x_dll, x_dlr, x_drl, x_drr;
	float x_horn_width;
	float x_hll, x_hlr, x_hrl, x_hrr;

	// fade in/out for re-configuration
	bool     fade_dir; // true: fade-out, false: fade-in
	uint32_t fade; // fade counter 0..FADED

	/* actual effect instance, whirl.c */
	struct b_whirl *whirl;

	/* misc */
	double   rate, nyq;   // sample-rate, max EQ freq
	float    lpf1, lpf2;  // Low-pass parameter
	int      resend_trigger;
	uint32_t resend_data_to_ui;
} B3W;


static LV2_Handle
instantiate (const LV2_Descriptor*     descriptor,
             double                    rate,
             const char*               bundle_path,
             const LV2_Feature* const* features)
{
	B3W* b3w = (B3W*)calloc (1, sizeof (B3W));
	if (!b3w) { return NULL ;}

	if (!(b3w->whirl = allocWhirl ())) {
		free (b3w);
		return NULL;
	}

	initWhirl (b3w->whirl, NULL, rate);

	// reference filters
	b3w->flt[0].W[0] = b3w->whirl->hafw;
	b3w->flt[1].W[0] = b3w->whirl->hbfw;
	b3w->flt[2].W[0] = b3w->whirl->drfL;
	b3w->flt[2].W[1] = b3w->whirl->drfR;

	// get defaults
	b3w->flt[0]._t = b3w->whirl->haT;
	b3w->flt[0]._f = b3w->whirl->haF;
	b3w->flt[0]._g = b3w->whirl->haG;
	b3w->flt[0]._q = b3w->whirl->haQ;

	b3w->flt[1]._t = b3w->whirl->hbT;
	b3w->flt[1]._f = b3w->whirl->hbF;
	b3w->flt[1]._g = b3w->whirl->hbG;
	b3w->flt[1]._q = b3w->whirl->hbQ;

	b3w->flt[2]._t = b3w->whirl->lpT;
	b3w->flt[2]._f = b3w->whirl->lpF;
	b3w->flt[2]._g = b3w->whirl->lpG;
	b3w->flt[2]._q = b3w->whirl->lpQ;

	b3w->o_horn_radius = b3w->whirl->hornRadiusCm;
	b3w->o_drum_radius = b3w->whirl->drumRadiusCm;
	b3w->o_mic_dist = b3w->whirl->micDistCm;
	b3w->o_horn_xoff = b3w->whirl->hornXOffsetCm;
	b3w->o_horn_zoff = b3w->whirl->hornZOffsetCm;
	b3w->o_horn_leak = b3w->whirl->leakLevel;

	b3w->fade_dir = false;
	b3w->fade = 0;

	b3w->rate = rate;
	b3w->nyq  = rate * 0.4998;
	b3w->lpf1  = 2000.0 / rate;
	b3w->lpf2  = 880.0 / rate;

	b3w->resend_data_to_ui = 0;
	b3w->resend_trigger = 0;

	// fade in levels
	b3w->o_horn_level = 0.0;
	b3w->o_drum_level = 0.0;
	b3w->o_drum_width = 0.0;

	b3w->x_drum_width = 0.0;
	b3w->x_dll = b3w->x_drr = 1.0;
	b3w->x_dlr = b3w->x_drl = 0.0;

	b3w->x_horn_width = 0.0;
	b3w->x_hll = b3w->x_hrr = 1.0;
	b3w->x_hlr = b3w->x_hrl = 0.0;

	return (LV2_Handle)b3w;
}

static void
connect_port (LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
	B3W* b3w = (B3W*)instance;

	switch ((PortIndex)port) {
		case B3W_INPUT:       b3w->input = (float*)data; break;
		case B3W_OUTL:        b3w->outL = (float*)data; break;
		case B3W_OUTR:        b3w->outR = (float*)data; break;

		case B3W_REVSELECT:   b3w->rev_select = (float*)data; break;

		case B3W_FILTATYPE:   b3w->flt[0].type = (float*)data; break;
		case B3W_FILTAFREQ:   b3w->flt[0].freq = (float*)data; break;
		case B3W_FILTAQUAL:   b3w->flt[0].qual = (float*)data; break;
		case B3W_FILTAGAIN:   b3w->flt[0].gain = (float*)data; break;

		case B3W_FILTBTYPE:   b3w->flt[1].type = (float*)data; break;
		case B3W_FILTBFREQ:   b3w->flt[1].freq = (float*)data; break;
		case B3W_FILTBQUAL:   b3w->flt[1].qual = (float*)data; break;
		case B3W_FILTBGAIN:   b3w->flt[1].gain = (float*)data; break;

		case B3W_FILTDTYPE:   b3w->flt[2].type = (float*)data; break;
		case B3W_FILTDFREQ:   b3w->flt[2].freq = (float*)data; break;
		case B3W_FILTDQUAL:   b3w->flt[2].qual = (float*)data; break;
		case B3W_FILTDGAIN:   b3w->flt[2].gain = (float*)data; break;

		case B3W_HORNBRAKE:   b3w->horn_brake = (float*)data; break;
		case B3W_HORNACCEL:   b3w->horn_accel = (float*)data; break;
		case B3W_HORNDECEL:   b3w->horn_decel = (float*)data; break;
		case B3W_HORNRPMSLOW: b3w->horn_slow = (float*)data; break;
		case B3W_HORNRPMFAST: b3w->horn_fast = (float*)data; break;

		case B3W_DRUMBRAKE:   b3w->drum_brake = (float*)data; break;
		case B3W_DRUMACCEL:   b3w->drum_accel = (float*)data; break;
		case B3W_DRUMDECEL:   b3w->drum_decel = (float*)data; break;
		case B3W_DRUMRPMSLOW: b3w->drum_slow = (float*)data; break;
		case B3W_DRUMRPMFAST: b3w->drum_fast = (float*)data; break;

		case B3W_HORNLVL:     b3w->horn_level = (float*)data; break;
		case B3W_DRUMLVL:     b3w->drum_level = (float*)data; break;
		case B3W_DRUMWIDTH:   b3w->drum_width = (float*)data; break;
		case B3W_HORNWIDTH:   b3w->horn_width = (float*)data; break;

		case B3W_HORNLEAK:    b3w->horn_leak = (float*)data; break;
		case B3W_HORNRADIUS:  b3w->horn_radius = (float*)data; break;
		case B3W_DRUMRADIUS:  b3w->drum_radius = (float*)data; break;
		case B3W_HORNOFFX:    b3w->horn_xoff = (float*)data; break;
		case B3W_HORNOFFZ:    b3w->horn_zoff = (float*)data; break;
		case B3W_MICDIST:     b3w->mic_dist = (float*)data; break;

		case B3W_HORNRPM:     b3w->c_horm_rpm = (float*)data; break;
		case B3W_DRUMRPM:     b3w->c_drum_rpm = (float*)data; break;
		case B3W_HORNANG:     b3w->c_horm_ang = (float*)data; break;
		case B3W_DRUMANG:     b3w->c_drum_ang = (float*)data; break;

		case B3W_GUINOTIFY:   b3w->p_resend_trigger = (float*)data; break;
		case B3W_LINKSPEED:   b3w->p_link_speed = (float*)data; break;
		case B3W_MICANGLE:    b3w->mic_angle = (float*)data; break;
		default: break;
	}
}

static inline float db_to_coefficient (const float d) {
	return powf (10.0f, 0.05f * d);
}

#define SILENT 128 // cycles of 64
#define FADED   96 // cycles of 64

static bool faded (B3W *b3w) {
	return b3w->fade >= FADED;
}

static int interpolate_filter (B3W *b3w,  Filter *flt) {
	assert (flt->type && flt->freq && flt->qual && flt->gain);

	int   t = (int) rintf (*flt->type);
	float f = *flt->freq;
	float q = *flt->qual;
	float g = *flt->gain;

	t = t % 9;

	if  (t != flt->_t && !faded (b3w)) {
		return 1;
	}

	if (q < .01)      { q =  .01f; }
	if (q > 6.0)      { q =  6.f; }
	if (f < 20.0)     { f =  20.f; }
	if (f > b3w->nyq) { f =  b3w->nyq;}
	if (g < -80)      { g = -80.f; }
	if (g >  80)      { g =  80.f; }

	if (flt->_f == f && flt->_g == g && flt->_q == q && flt->_t == t) {
		return 0;
	}

	const float _a = b3w->lpf1;
	const float _b = b3w->lpf2;

	if (faded (b3w)) {
		flt->_t = t;
		flt->_g = g;
		flt->_f = f;
		flt->_q = q;
	} else {
		// TODO think about linear rather that exp approach, or require fade.

		// limit large jumps per 64 samples
		const float w0 = flt->_f / b3w->rate;
		const float w1 = f / b3w->rate;
		if (fabsf (w0 - w1) > .20) { return 1; } // unusual, whirl EQ range is 20Hz..8KHz
		if ((w0 - w1) >  .02) { f = b3w->rate * (w0 - .02 * b3w->rate);}
		if ((w0 - w1) < -.02) { f = b3w->rate * (w0 + .02 * b3w->rate);}
		if ((flt->_g - g) >  10)   { g = flt->_g - 10; }
		if ((flt->_g - g) < -10)   { g = flt->_g + 10; }

		flt->_f += _a * (f - flt->_f);
		flt->_g += _a * (g - flt->_g);
		flt->_q += _b * (q - flt->_q);

		if (fabsf (flt->_g - g) < 1e-4) { flt->_g = g; }
		if (fabsf (flt->_f - f) < 1e-2) { flt->_f = f; }
		if (fabsf (flt->_q - q) < 1e-3) { flt->_q = q; }
	}

	double C[6];
	eqCompute (flt->_t, flt->_f, flt->_q, flt->_g, C, b3w->rate);

	flt->W[0][a1] = C[EQC_A1];
	flt->W[0][a2] = C[EQC_A2];
	flt->W[0][b0] = C[EQC_B0];
	flt->W[0][b1] = C[EQC_B1];
	flt->W[0][b2] = C[EQC_B2];
	if (faded (b3w)) {
		flt->W[0][z0] = 0;
		flt->W[0][z1] = 0;
	}

	if (!flt->W[1]) {
		return 0;
	}

	flt->W[1][a1] = C[EQC_A1];
	flt->W[1][a2] = C[EQC_A2];
	flt->W[1][b0] = C[EQC_B0];
	flt->W[1][b1] = C[EQC_B1];
	flt->W[1][b2] = C[EQC_B2];
	if (faded (b3w)) {
		flt->W[1][z0] = 0;
		flt->W[1][z1] = 0;
	}
	return 0;
}

#define SETVAR(PARAM, VAR, MIN, MAX, MOD) { \
	const float val = *b3w->VAR; \
	 b3w->o_ ## VAR = val; \
	if (val >= MIN && val <= MAX) { b3w->whirl->PARAM = MOD val; } \
}

#define CHECKDIFF(VAR) \
	if (b3w->VAR && *b3w->VAR != b3w->o_ ## VAR) { changed = 1; }

static int reconfigure (B3W* b3w) {
	int changed = 0;

	CHECKDIFF(horn_radius);
	CHECKDIFF(drum_radius);
	CHECKDIFF(horn_xoff);
	CHECKDIFF(horn_zoff);
	CHECKDIFF(mic_dist);
	CHECKDIFF(mic_angle);

	if (!changed) {
		return 0;
	}

	if (!faded (b3w)) {
		return 1;
	}

	SETVAR(hornRadiusCm, horn_radius, 9, 50,)
	SETVAR(drumRadiusCm, drum_radius, 9, 50,)
	SETVAR(micDistCm, mic_dist, 9, 300,)
	SETVAR(hornXOffsetCm, horn_xoff, -20 , 20,)
	SETVAR(hornZOffsetCm, horn_zoff, -20 , 20,)
	SETVAR(micAngle, mic_angle, 0 , 180, 1.f - 1.f/180.f *)

	computeOffsets (b3w->whirl);
	return 0;
}

static void process (B3W* b3w, uint32_t n_samples, float const * const in, float *outL, float *outR) {
	uint32_t i;
	assert (n_samples <= 64);
	const float lpf = b3w->lpf1;

	float horn_left[64];
	float horn_right[64];
	float drum_left[64];
	float drum_right[64];

	if (b3w->horn_leak) {
		const float lk = db_to_coefficient (*b3w->horn_leak);
		b3w->o_horn_leak += lpf * (lk - b3w->o_horn_leak) + 1e-15;
		b3w->whirl->leakage = b3w->o_horn_leak * b3w->whirl->hornLevel;
	}

	whirlProc2 (b3w->whirl, in, NULL, NULL,
			horn_left, horn_right, drum_left, drum_right, n_samples);

	// mixdown
	const float hl = db_to_coefficient (*b3w->horn_level);
	const float dl = db_to_coefficient (*b3w->drum_level);

	const float dw = *b3w->drum_width - 1.f;
	const float hw = b3w->horn_width ? *b3w->horn_width - 1.f : 0.0;

	b3w->o_horn_level += lpf * (hl - b3w->o_horn_level) + 1e-15;
	b3w->o_drum_level += lpf * (dl - b3w->o_drum_level) + 1e-15;
	b3w->o_horn_width += lpf * (hw - b3w->o_horn_width) + 1e-15;
	b3w->o_drum_width += lpf * (dw - b3w->o_drum_width) + 1e-15;

	// re-calc coefficients only when changed
	if (fabsf (b3w->x_drum_width - b3w->o_drum_width) > 1e-8) {
		b3w->x_drum_width = b3w->o_drum_width;

		const float dwF = b3w->o_drum_width;
		const float dwP = dwF > 0.f ? (dwF >  1.f ? 1.f :  dwF) : 0.f;
		const float dwN = dwF < 0.f ? (dwF < -1.f ? 1.f : -dwF) : 0.f;
		b3w->x_dll = sqrtf (1.f - dwP);
		b3w->x_dlr = sqrtf (0.f + dwP);
		b3w->x_drl = sqrtf (0.f + dwN);
		b3w->x_drr = sqrtf (1.f - dwN);
	}

	if (fabsf (b3w->x_horn_width - b3w->o_horn_width) > 1e-8) {
		b3w->x_horn_width = b3w->o_horn_width;

		const float hwF = b3w->o_horn_width;
		const float hwP = hwF > 0.f ? (hwF >  1.f ? 1.f :  hwF) : 0.f;
		const float hwN = hwF < 0.f ? (hwF < -1.f ? 1.f : -hwF) : 0.f;
		b3w->x_hll = sqrtf (1.f - hwP);
		b3w->x_hlr = sqrtf (0.f + hwP);
		b3w->x_hrl = sqrtf (0.f + hwN);
		b3w->x_hrr = sqrtf (1.f - hwN);
	}

	// localize variable, small loop
	const float dll = b3w->o_drum_level * b3w->x_dll;
	const float dlr = b3w->o_drum_level * b3w->x_dlr;
	const float drl = b3w->o_drum_level * b3w->x_drl;
	const float drr = b3w->o_drum_level * b3w->x_drr;

	const float hll = b3w->o_horn_level * b3w->x_hll;
	const float hlr = b3w->o_horn_level * b3w->x_hlr;
	const float hrl = b3w->o_horn_level * b3w->x_hrl;
	const float hrr = b3w->o_horn_level * b3w->x_hrr;

	for (i = 0; i < n_samples; ++i) {
		outL [i] = horn_left[i] * hll + horn_right[i] * hlr + drum_left[i] * dll + drum_right[i] * dlr;
		outR [i] = horn_left[i] * hrl + horn_right[i] * hrr + drum_left[i] * drl + drum_right[i] * drr;
	}
}

static void set_speed (B3W* b3w)
{
	if (b3w->o_rev_select == *b3w->rev_select) {
		return;
	}

	if (b3w->flt[0].type && !b3w->horn_radius) {
		// MOD version
		const int v = (int) floorf (*b3w->rev_select);
		useRevOption (b3w->whirl, v * 3 + v);
	} else {
		const float l = b3w->p_link_speed ? (*b3w->p_link_speed) : 0;
		const int v = (int) floorf (*b3w->rev_select);
		int h = v / 3; // 0: stop, 1: slow, 2: fast
		int d = v % 3; // 0: stop, 1: slow, 2: fast
		if (l <= -.5) { h = d; }
		if (l >= 0.5) { d = h; }
		useRevOption (b3w->whirl, h * 3 + d);
	}

	b3w->o_rev_select = *b3w->rev_select;
}

#define SETVALUE(VAR, NAME, PROC, FN) \
  if (b3w->NAME) { \
    if (b3w->o_##NAME != *(b3w->NAME)) { \
      b3w->whirl->VAR = PROC (*(b3w->NAME)); \
      b3w->o_##NAME = *(b3w->NAME); \
      FN; \
    } \
  }

static void run (LV2_Handle instance, uint32_t n_samples) {
	B3W* b3w = (B3W*)instance;

	SETVALUE(hnBrakePos, horn_brake, (double), );
	SETVALUE(hornAcc, horn_accel, , );
	SETVALUE(hornDec, horn_decel, , );

	SETVALUE(hornRPMslow, horn_slow, , computeRotationSpeeds (b3w->whirl); b3w->o_rev_select = -1;);
	SETVALUE(hornRPMfast, horn_fast, , computeRotationSpeeds (b3w->whirl); b3w->o_rev_select = -1;);
	SETVALUE(drumRPMslow, drum_slow, , computeRotationSpeeds (b3w->whirl); b3w->o_rev_select = -1;);
	SETVALUE(drumRPMfast, drum_fast, , computeRotationSpeeds (b3w->whirl); b3w->o_rev_select = -1;);

	SETVALUE(drBrakePos, drum_brake, (double), );
	SETVALUE(drumAcc, drum_accel, , );
	SETVALUE(drumDec, drum_decel, , );

	set_speed (b3w);

	float* input = b3w->input;
	float* outL = b3w->outL;
	float* outR = b3w->outR;

	uint32_t k = n_samples;
	while (k > 0) {
		const uint32_t n = k > 64 ? 64 : k;

		int need_fade = 0;

		if (b3w->flt[0].type) { // extended and MOD variant
			need_fade |= interpolate_filter (b3w, &b3w->flt[0]);
			need_fade |= interpolate_filter (b3w, &b3w->flt[1]);
			need_fade |= interpolate_filter (b3w, &b3w->flt[2]);
		}
		if (b3w->horn_radius) { // extended version only
			need_fade |= reconfigure (b3w);
		}

		process (b3w, n, input, outL, outR);

		if (need_fade) {
			b3w->fade_dir = true;
		}

		float g0, g1; g0 = g1 = 1.0;

		if (!b3w->fade_dir && b3w->fade > 0 && b3w->fade <= FADED) {
			g0 = 1.0 - b3w->fade / (float)FADED;
			--b3w->fade;
			g1 = 1.0 - b3w->fade / (float)FADED;
		} else if (b3w->fade_dir && b3w->fade < FADED) {
			g0 = 1.0 - b3w->fade / (float)FADED;
			++b3w->fade;
			g1 = 1.0 - b3w->fade / (float)FADED;
		} else if (b3w->fade >= FADED) {
			if (!b3w->fade_dir) {
				--b3w->fade;
			} else if (b3w->fade < SILENT) {
				++b3w->fade;
			} else if (!need_fade) {
				b3w->fade_dir = false;
			}
			memset (outL, 0, sizeof (float) * n);
			memset (outR, 0, sizeof (float) * n);
		}

		if (g0 != g1) {
			uint32_t i;
			const float d = (g1 - g0) / (float)n;
			float g = g0;
			for (i = 0; i < n; ++i) {
				g += d;
				outL[i] *= g;
				outR[i] *= g;
			}
		}

		input += n;
		outL += n; outR += n;
		k -= n;
	}

	if (!b3w->c_horm_rpm || !b3w->c_horm_rpm) {
		// simple version
		return;
	}

	const float hspd = b3w->whirl->hornIncr * 60.f * b3w->rate;
	const float dspd = b3w->whirl->drumIncr * 60.f * b3w->rate;

	if (b3w->resend_trigger != (int)(floorf (b3w->p_resend_trigger[0]))) {
		b3w->resend_data_to_ui = ceilf (.5 * b3w->rate / n_samples);
		b3w->resend_trigger = (int)(floorf (b3w->p_resend_trigger[0]));
	}

	if (b3w->resend_data_to_ui > 0) {
		// Force host to send update
		--b3w->resend_data_to_ui;
		*b3w->c_horm_rpm = -1.f - b3w->resend_data_to_ui / 100.f;
		*b3w->c_horm_ang = -1.f - b3w->resend_data_to_ui / 100.f;
		*b3w->c_drum_rpm = -1.f - b3w->resend_data_to_ui / 100.f;
		*b3w->c_drum_ang = -1.f - b3w->resend_data_to_ui / 100.f;
	} else {
		*b3w->c_horm_rpm = hspd;
		*b3w->c_horm_ang = fmod (1.25 - b3w->whirl->hornAngleGRD, 1.0);
		*b3w->c_drum_rpm = dspd;
		*b3w->c_drum_ang = fmod (b3w->whirl->drumAngleGRD + .25, 1.0);
	}
}

static void
cleanup (LV2_Handle instance)
{
	B3W* b3w = (B3W*)instance;
	freeWhirl (b3w->whirl);
	free (instance);
}

const void*
extension_data (const char* uri)
{
	return NULL;
}

static const LV2_Descriptor descriptor = {
	B3W_URI,
	instantiate,
	connect_port,
	NULL,
	run,
	NULL,
	cleanup,
	extension_data
};

static const LV2_Descriptor descriptorExt = {
	B3W_URI_EXT,
	instantiate,
	connect_port,
	NULL,
	run,
	NULL,
	cleanup,
	extension_data
};

static const LV2_Descriptor descriptorMOD = {
	B3W_URI_MOD,
	instantiate,
	connect_port,
	NULL,
	run,
	NULL,
	cleanup,
	extension_data
};

// fix for -fvisibility=hidden
#undef LV2_SYMBOL_EXPORT
#ifdef _WIN32
#    define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
#    define LV2_SYMBOL_EXPORT  __attribute__ ((visibility ("default")))
#endif
LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor (uint32_t index)
{
	switch (index) {
		case 0:
			return &descriptor;
		case 1:
			return &descriptorExt;
		case 2:
			return &descriptorMOD;
		default:
			return NULL;
	}
}

// setBfree API - unused
void useMIDIControlFunction (void *m, const char * cfname, void (* f) (void *d, unsigned char), void *d) { }
int getConfigParameter_dr (const char * par, ConfigContext * cfg, double * dp, double lowInc, double highInc) { return 0; }
int getConfigParameter_d (const char * par, ConfigContext * cfg, double * dp) { return 0; }
int getConfigParameter_ir (const char * par, ConfigContext * cfg, int * ip, int lowInc, int highInc) { return 0; }
int getConfigParameter_i (const char * par, ConfigContext * cfg, int * ip) { return 0; }
void notifyControlChangeByName (void *mcfg, const char * cfname, unsigned char val) { }

/* vi:set ts=2 sts=2 sw=2: */
