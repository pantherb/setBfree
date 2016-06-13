/* robtk b_whirl gui
 *
 * Copyright 2015 Robin Gareus <robin@gareus.org>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#include "gui/rtk_lever.h"
#include "eqcomp.c"

#define RTK_URI "http://gareus.org/oss/lv2/b_whirl#"
#define RTK_GUI "ui"

// port map; see b_whirl/lv2.c
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


/***  parameter mapping, default values & format ***/

typedef struct {
	float min;
	float max;
	float dflt;
	float warp;
	const char *fmt;
} Parameter;

#define S_(str) (const char*)str

// defaults from b_whirl-configurable.ttl
static const Parameter rpm_slow[2] = {
	{  5.0, 200.0, 40.32, 40, S_("%.0f RPM")}, // horn chorale
	{  5.0, 100.0, 36.00, 20, S_("%.0f RPM")}, // baffle chorale
};

static const Parameter rpm_fast[2] = {
	{ 100.0, 1000.0, 423.36, 10, S_("%.0f RPM")}, // horn tremolo
	{  60.0,  600.0, 357.30, 10, S_("%.0f RPM")}, // baffle tremolo
};

static const Parameter acceleration[2] = {
	{ 0.006, 10.0, 0.161,  500, S_("%.2f s")}, // horn accel [s]
	{  0.01, 20.0, 4.127,  200, S_("%.2f s")}, // baffle accel [s]
};

static const Parameter deceleration[2] = {
	{ 0.006, 10.0, 0.321,  500, S_("%.2f s")}, // horn
	{  0.01, 20.0, 1.371,  200, S_("%.2f s")}, // baffle
};

static const Parameter radius[2] = {
	{  9, 50.0, 19.2,  0, S_("%.1f cm")}, // horn
	{  9, 50.0, 22.0,  0, S_("%.1f cm")}, // baffle
};

static const Parameter xzmpos[4] = {
	{  -20,  20.0,  0.0,  0, S_("%.1f cm")}, // horn X
	{  -20,  20.0,  0.0,  0, S_("%.1f cm")}, // horn Z
	{    9, 150.0, 42.0,  0, S_("%.1f cm")}, // all mics
	{  0.0, 180.0, 180.0, 0, S_("%.1f deg")}, // horn mic angle
};

static const Parameter filter[3][3] = {
	{ // horn char -- low pass
		{ 250.0,  10000.0,   4500.0, 35.8, S_("%.0f Hz")}, // freq
		{   0.01,     6.0,   2.7456,   60, S_("%.2f")}, // q
		{ -48.,      48.0, -38.9291,    0, NULL}, // level

	},
	{ // horn split -- low shelf
		{ 100.0,   8000.0,    300.0,   78, S_("%.0f Hz")}, // freq
		{   0.01,     6.0,      1.0,   60, S_("%.2f")}, // q
		{ -48.,      48.0,    -30.0,    0, NULL}, // level

	},
	{ // drum split -- high-shelf
		{  50.0,   8000.0, 811.9695,   49, S_("%.0f Hz")}, // freq
		{   0.01,     6.0,   1.6016,   60, S_("%.2f")}, // q
		{ -48.,      48.0, -38.9291,    0, NULL}, // level

	}
};

static float dial_to_param (const Parameter *d, float val) {
	return d->min + (d->max - d->min) * (pow ((1. + d->warp), val) - 1.) / d->warp;
}

static float param_to_dial (const Parameter *d, float val) {
	if (val < d->min) return 0.f;
	if (val > d->max) return 1.f;
	return log (1. + d->warp * (val - d->min) / (d->max - d->min)) / log (1. + d->warp);
}

typedef struct {
	LV2UI_Write_Function write;
	LV2UI_Controller controller;

	bool disable_signals;
	RobWidget *rw; // top-level container

	// horn[0] / drum[1] - motor
	RobWidget *tbl_mtr[2];
	RobTkLbl  *lbl_mtr[2][5];
	RobTkDial *s_rpm_slow[2];
	RobTkDial *s_rpm_fast[2];
	RobTkDial *s_accel[2];
	RobTkDial *s_decel[2];
	RobTkSep  *sep_tbl[2];

	// horn[0] / drum[1] - level/mix
	RobTkSep  *sep_mix[4];
	RobWidget *box_mix[2];
	RobTkLbl  *lbl_mix[2];
	RobTkDial *s_level[2];

	// horn[0] / drum[1] - speed levers
	RobTkLever *lever[2];

	// horn[0] / drum[1] - brake
	RobWidget *box_brk[2];
	RobTkLbl  *lbl_brk[2];
	RobTkDial *s_brakepos[2];

	// filters horn A+B, drum
	RobWidget   *tbl_flt[3];
	RobTkLbl    *lbl_flt[3][5];
	RobTkSelect *sel_fil[3];
	RobTkDial   *s_ffreq[3];
	RobTkDial   *s_fqual[3];
	RobTkDial   *s_fgain[3];

	// filter transfer function display
	RobWidget       *fil_tf[3];
	cairo_surface_t *fil_sf[3];

	// speaker widgets
	RobTkLbl        *lbl_rpm[2];
	float            cur_rpm[2];
	float            cur_ang[2];
	RobWidget       *spk_dpy[2];
	cairo_surface_t *spk_sf[2];

	// advanced controls
	RobTkCBtn *btn_adv;
	RobTkSep  *sep_adv[3];
	RobWidget *tbl_adv[2];
	RobTkLbl  *lbl_adv[7];
	RobTkDial *s_radius[2]; // horn, drum
	RobTkDial *s_xzmpos[4]; // horn x, horn z, all-mics

	// misc other widgets
	RobWidget   *box_drmmic;
	RobTkSep    *sep_drmmic;
	RobTkLbl    *lbl_drumwidth;
	RobTkDial   *s_drumwidth;

	RobWidget   *box_hrnmic;
	RobTkSep    *sep_hrnmic;
	RobTkLbl    *lbl_hornwidth;
	RobTkDial   *s_hornwidth;

	RobWidget   *box_leak;
	RobTkSep    *sep_leak;
	RobTkLbl    *lbl_leak;
	RobTkDial   *s_leak;

	RobTkCBtn   *btn_link;

	RobTkSep    *sep_h[3];
	RobTkSep    *sep_v[3];

	PangoFontDescription *font[2];
	cairo_surface_t* dial_bg[17];
	cairo_surface_t* gui_bg;

	cairo_pattern_t* hornp[4];

	int eq_dragging;
	int eq_hover;
	struct { float x0, y0; } eq_ctrl [3];

	bool last_used_horn_lever;
	bool set_last_used;
	int initialized;
	const char *nfo;
} WhirlUI;

static const float c_ann[4] = {0.5, 0.5, 0.5, 1.0}; // EQ annotation color
static const float c_dlf[4] = {0.8, 0.8, 0.8, 1.0}; // dial faceplate fg

/***  transfer function display ***/

#ifndef SQUARE
#define SQUARE(X) ( (X) * (X) )
#endif

#ifndef MAX
#define MAX(A,B) ((A) > (B)) ? (A) : (B)
#endif

typedef struct {
	float A, B, C, D, A1, B1;
	float rate;
	float x0, y0; // mouse position
} FilterSection;

static float get_eq_response (FilterSection *flt, const float freq) {
	const float w = 2.f * M_PI * freq / flt->rate;
	const float c1 = cosf (w);
	const float s1 = sinf (w);
	const float A = flt->A * c1 + flt->B1;
	const float B = flt->B * s1;
	const float C = flt->C * c1 + flt->A1;
	const float D = flt->D * s1;
	const float rv = 20.f * log10f (sqrtf ((SQUARE(A) + SQUARE(B)) * (SQUARE(C) + SQUARE(D))) / (SQUARE(C) + SQUARE(D)));
	return MAX(-100, rv);
}

static float freq_at_x (const int x, const int m0_width) {
	return 20.f * powf (1000.f, x / (float) m0_width);
}

static float x_at_freq (const float f, const int m0_width) {
	return rintf (m0_width * logf (f / 20.0) / logf (1000.0));
}

static void draw_eq (WhirlUI* ui, const int f, const int w, const int h) {
	assert (!ui->fil_sf[f]);
	ui->fil_sf[f] = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create (ui->fil_sf[f]);

	double C[6];
	eqCompute (
			robtk_select_get_value (ui->sel_fil[f]),
			dial_to_param (&filter[f][0], robtk_dial_get_value (ui->s_ffreq[f])),
			dial_to_param (&filter[f][1], robtk_dial_get_value (ui->s_fqual[f])),
			robtk_dial_get_value (ui->s_fgain[f]),
			C, 48000);

	FilterSection flt;
	flt.rate = 48000;
	flt.A  = C[EQC_B0] + C[EQC_B2];
	flt.B  = C[EQC_B0] - C[EQC_B2];
	flt.C  = 1.0 + C[EQC_A2];
	flt.D  = 1.0 - C[EQC_A2];
	flt.A1 = C[EQC_A1];
	flt.B1 = C[EQC_B1];

	const int xw = w - 4;
	const float ym = floor (h * .5) + .5;
	const float yr = (h - 4) / 100.f;

	cairo_set_line_width (cr, .75);
	double dash = 1;
	cairo_save (cr);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);
	cairo_set_dash (cr, &dash, 1, 0);

	CairoSetSouerceRGBA (c_ann);
	cairo_move_to (cr, 2, ym);
	cairo_line_to (cr, 2 + xw, ym);
	cairo_stroke (cr);

	CairoSetSouerceRGBA (c_g20);
#define GAINLINE(DB) \
	{ \
		const float yy = rintf (ym - yr * DB) + .5; \
		cairo_move_to (cr, 2, yy); cairo_line_to (cr, 2 + xw, yy); cairo_stroke (cr); \
	}

	GAINLINE(-18);
	GAINLINE( 18);
	GAINLINE(-36);
	GAINLINE( 36);

#undef GAINLINE

#define FREQLINE(FQ) \
	{ \
		const float xf = rintf (x_at_freq (FQ, xw)) + 2.5; \
		cairo_move_to (cr, xf, 2); cairo_line_to (cr, xf, h - 2); cairo_stroke (cr); \
	} \

	for (int i = 2; i < 10; ++i) {
		if (i != 2)
			FREQLINE (i * 10);
		FREQLINE (i * 100);
		FREQLINE (i * 1000);
	}
	FREQLINE (20000);

	CairoSetSouerceRGBA (c_ann);
	FREQLINE (100);
	FREQLINE (1000);
	FREQLINE (10000);

#undef FREQLINE

	if (h > 60) {
		float yy = rintf (ym - yr * 36) + .5;
		write_text_full (cr, "+36dB", ui->font[0], 3, yy,  0, -3, c_ann);
		yy = rintf (ym - yr * -36) + .5;
		write_text_full (cr, "-36dB", ui->font[0], 3, yy,  0, -3, c_ann);
		yy = rintf (ym - yr * 0) + .5;
		write_text_full (cr, "  0dB", ui->font[0], 3, yy,  0, -3, c_ann);
	}
	if (h > 120) {
		float yy = rintf (ym - yr * 18) + .5;
		write_text_full (cr, "+18dB", ui->font[0], 3, yy,  0, -3, c_ann);
		yy = rintf (ym - yr * -18) + .5;
		write_text_full (cr, "-18dB", ui->font[0], 3, yy,  0, -3, c_ann);
	}
	if (h > 60 && w > 120) {
		float xf = rintf (x_at_freq (100, xw)) + 2.5;
		write_text_full (cr, "100", ui->font[0], xf, h - 3,  0, -5, c_ann);
		xf = rintf (x_at_freq (1000, xw)) + 2.5;
		write_text_full (cr, "1K", ui->font[0], xf, h - 3,  0, -5, c_ann);
		xf = rintf (x_at_freq (10000, xw)) + 2.5;
		write_text_full (cr, "10K", ui->font[0], xf, h - 3,  0, -5, c_ann);
	}

	cairo_restore (cr);

#define DOTRADIUS 7
#define BOXRADIUS 6
	if (h > 60 && w > 120) {
		// draw dots,  todo special case Notch & gain other out of bounds..
		cairo_set_operator (cr, CAIRO_OPERATOR_ADD);
		cairo_set_line_width(cr, 1.0);
		const float fq = dial_to_param (&filter[f][0], robtk_dial_get_value (ui->s_ffreq[f]));
		const float xf = 2.5 + x_at_freq(fq, xw) - .5f;
		float yg;
		if (ui->eq_dragging == f || (ui->eq_dragging < 0 && ui->eq_hover == f)) {
			cairo_set_source_rgba (cr, 8, .4, .2, .6);
		} else {
			cairo_set_source_rgba (cr, 8, .4, .2, .3);
		}

		switch ((int)robtk_select_get_value (ui->sel_fil[f])) {
			case EQC_LPF:
			case EQC_HPF:
			case EQC_NOTCH:
				yg = ym;
				cairo_move_to (cr, xf            , yg + BOXRADIUS + .5);
				cairo_line_to (cr, xf - BOXRADIUS, yg - BOXRADIUS);
				cairo_line_to (cr, xf + BOXRADIUS, yg - BOXRADIUS);
				cairo_close_path (cr);
				break;
			case EQC_BPF0:
				yg = ym;
				cairo_move_to (cr, xf            , yg - BOXRADIUS - .5);
				cairo_line_to (cr, xf - BOXRADIUS - .5, yg);
				cairo_line_to (cr, xf            , yg + BOXRADIUS + .5);
				cairo_line_to (cr, xf + BOXRADIUS + .5, yg);
				cairo_close_path (cr);
				break;
			case EQC_APF:
			case EQC_BPF1:
				yg = ym - yr * get_eq_response (&flt, fq);
				cairo_rectangle (cr, xf - BOXRADIUS, yg - BOXRADIUS, 2 * BOXRADIUS, 2 * BOXRADIUS);
				break;
			default:
				yg = ym - yr * get_eq_response (&flt, fq);
				cairo_arc (cr, xf, yg, DOTRADIUS, 0, 2 * M_PI);
				break;
		}
		cairo_fill_preserve (cr);
		cairo_set_source_rgba (cr, 8, .4, .2, .3);
		cairo_stroke (cr);
		ui->eq_ctrl[f].x0 = xf;
		ui->eq_ctrl[f].y0 = yg;
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	} else {
		ui->eq_ctrl[f].x0 = -1;
		ui->eq_ctrl[f].y0 = -1;
	}

	for (int i = 0; i < xw; ++i) {
		// TODO handle notch-peak if it's not on a px x-coordinate)
		const float xf = freq_at_x (i, xw);
		const float y = yr * get_eq_response (&flt, xf);
		if (i == 0) {
			cairo_move_to (cr, 2.5 + i, ym - y);
		} else {
			cairo_line_to (cr, 2.5 + i, ym - y);
		}
	}
	CairoSetSouerceRGBA (c_wht);
	cairo_set_line_width (cr, 1.0);
	cairo_stroke (cr);
	cairo_destroy (cr);
}

static int find_filter (WhirlUI* ui, RobWidget *rw) {
	if (rw == ui->fil_tf[0]) {
		return 0;
	} else if (rw == ui->fil_tf[1]) {
		return 1;
	} else if (rw == ui->fil_tf[2]) {
		return 2;
	} else {
		return -1;
	}
}

static bool m0_expose_event (RobWidget* rw, cairo_t* cr, cairo_rectangle_t *ev) {
	WhirlUI* ui = (WhirlUI*)GET_HANDLE (rw);

	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
	cairo_clip_preserve (cr);
	CairoSetSouerceRGBA (c_trs);
	cairo_fill (cr);

	const int w = rw->area.width;
	const int h = rw->area.height;
	rounded_rectangle (cr, 2, 2, w - 4 , h - 4, 9);
	cairo_set_line_width (cr, 1.0);
	CairoSetSouerceRGBA (c_g80);
	cairo_stroke_preserve (cr);
	CairoSetSouerceRGBA (c_blk);
	cairo_fill_preserve (cr);
	cairo_clip (cr);

	const int f = find_filter (ui, rw);
	if (f < 0 || f > 2) return TRUE;

	if (!ui->fil_sf[f]) {
		draw_eq (ui, f, w, h);
	}

	cairo_set_source_surface (cr, ui->fil_sf[f], 0, 0);
	cairo_paint (cr);
	return TRUE;
}

static void
m0_size_request (RobWidget* handle, int *w, int *h) {
	*w = 60;
	*h = 50;
}

static void update_eq (WhirlUI *ui, int i) {
	assert (i >= 0 && i < 3);
	if (ui->fil_sf[i]) {
		cairo_surface_destroy (ui->fil_sf[i]);
		ui->fil_sf[i] = NULL;
	}
	queue_draw (ui->fil_tf[i]);
	robtk_dial_set_sensitive (ui->s_fgain[i], robtk_select_get_value (ui->sel_fil[i]) >= EQC_PEQ);
}

static void
m0_size_allocate (RobWidget* rw, int w, int h) {
	WhirlUI* ui = (WhirlUI*)GET_HANDLE(rw);
	robwidget_set_size (rw, w, h);
	const int f = find_filter (ui, rw);
	if (f < 0) return;
	update_eq (ui, f);
}

static bool check_control_point (WhirlUI* ui, const int f, const int x, const int y) {
	if (ui->eq_ctrl[f].x0 < 0 || ui->eq_ctrl[f].y0 < 0) return false;
	return (fabsf(x - ui->eq_ctrl[f].x0) <= DOTRADIUS && fabsf(y - ui->eq_ctrl[f].y0) <= DOTRADIUS);
}

static RobWidget* m0_mouse_move (RobWidget* rw, RobTkBtnEvent *ev) {
	WhirlUI* ui = (WhirlUI*)GET_HANDLE(rw);
	int hv = -1;
	int f = ui->eq_dragging;
	if (f < 0) {
		f = find_filter (ui, rw);
	}
	if (check_control_point (ui, f, ev->x, ev->y)) {
		hv = f;
	}
	if (hv != ui->eq_hover) {
		ui->eq_hover = hv;
		if (ui->eq_dragging < 0) {
			update_eq (ui, f);
		}
	}
	if (ui->eq_dragging < 0) {
		return NULL;
	}

	RobTkDial *fctl = ui->s_ffreq[f];
	RobTkDial *gctl = ui->s_fgain[f];

	if (!gctl->sensitive) {
		gctl = NULL;
	}

	const float x0 = 2.5;
	const float x1 = rw->area.width - 1.5;
	const float xw = x1 - x0;

	if (fctl && ev->x >= x0 && ev->x <= x1) {
		const float hz = freq_at_x (ev->x - x0, xw);
		robtk_dial_set_value (fctl, param_to_dial (&filter[f][0], hz));
	}
	if (gctl) {
		const int h = rw->area.height;
		const float ym = floor (h * .5);
		float yr = (h - 4) / 100.f;
		if (robtk_select_get_value (ui->sel_fil[f]) >= EQC_LOW) {
			yr /= 2; // shelf filter mid-point
		}
		const float db = (ym - ev->y) / yr;
		robtk_dial_set_value (gctl, db);
	}
	return rw;
}

static RobWidget* m0_mouse_up (RobWidget* rw, RobTkBtnEvent *ev) {
	WhirlUI* ui = (WhirlUI*)GET_HANDLE(rw);
	if (ui->eq_dragging >= 0) {
		update_eq (ui, ui->eq_dragging);
	}
	ui->eq_dragging = -1;
	return NULL;
}

static RobWidget* m0_mouse_scroll (RobWidget* rw, RobTkBtnEvent *ev) {
	WhirlUI* ui = (WhirlUI*)GET_HANDLE(rw);

	const int f = find_filter (ui, rw);
	if (!check_control_point (ui, f, ev->x, ev->y)) return NULL;

	RobTkDial *bwctl = ui->s_fqual[f];
	float v = robtk_dial_get_value (bwctl);
	const float delta = (ev->state & ROBTK_MOD_CTRL) ? bwctl->acc : bwctl->scroll_mult * bwctl->acc;

	switch (ev->direction) {
		case ROBTK_SCROLL_RIGHT:
		case ROBTK_SCROLL_UP:
			v += delta;
			robtk_dial_set_value (bwctl, v);
			break;
		case ROBTK_SCROLL_LEFT:
		case ROBTK_SCROLL_DOWN:
			v -= delta;
			robtk_dial_set_value (bwctl, v);
			break;
		default:
			break;
	}
	return NULL;
}


static RobWidget* m0_mouse_down (RobWidget* rw, RobTkBtnEvent *ev) {
	WhirlUI* ui = (WhirlUI*)GET_HANDLE(rw);
	if (ev->button != 1) return NULL;
	const int f = find_filter (ui, rw);
	if (f < 0 || f > 2) return NULL;
	if (!check_control_point (ui, f, ev->x, ev->y)) return NULL;

	if (ev->state & ROBTK_MOD_SHIFT) {
		robtk_dial_set_value (ui->s_ffreq[f], param_to_dial (&filter[f][0], filter[f][0].dflt));
		robtk_dial_set_value (ui->s_fqual[f], param_to_dial (&filter[f][1], filter[f][1].dflt));
		robtk_dial_set_value (ui->s_fgain[f], filter[f][2].dflt);
		update_eq (ui, f);
		return NULL;
	}

	ui->eq_dragging = f;
	update_eq (ui, f);
	return rw;
}

/*** horn & drum display widgets ***/

static void
horn_size_request (RobWidget* handle, int *w, int *h) {
	*w = 60;
	*h = 40;
}

static void
drum_size_request (RobWidget* handle, int *w, int *h) {
	*w = 60;
	*h = 120;
}

static void
m1_size_allocate (RobWidget* rw, int w, int h) {
	WhirlUI* ui = (WhirlUI*)GET_HANDLE (rw);
	robwidget_set_size (rw, w, h);
	for (int i = 0; i < 4; ++i) {
		if (ui->hornp[i]) { cairo_pattern_destroy (ui->hornp[i]); ui->hornp[i] = NULL; }
	}
}

static void
create_horn_patterns (WhirlUI* ui, float x, float y) {
	assert (!ui->hornp[0] && !ui->hornp[1] && !ui->hornp[2] && !ui->hornp[3]);

	// horn c30
	ui->hornp[0] = cairo_pattern_create_linear (x * .05, -y, 0, y);
	cairo_pattern_add_color_stop_rgb (ui->hornp[0], 0.00, .30, .30, .30);
	cairo_pattern_add_color_stop_rgb (ui->hornp[0], 0.32, .31, .31, .31);
	cairo_pattern_add_color_stop_rgb (ui->hornp[0], 0.50, .40, .40, .40);
	cairo_pattern_add_color_stop_rgb (ui->hornp[0], 0.68, .31, .31, .31);
	cairo_pattern_add_color_stop_rgb (ui->hornp[0], 1.00, .30, .30, .30);

	// horn c60
	ui->hornp[1] = cairo_pattern_create_linear (x * .05, -y, 0, y);
	cairo_pattern_add_color_stop_rgb (ui->hornp[1], 0.00, .57, .57, .57);
	cairo_pattern_add_color_stop_rgb (ui->hornp[1], 0.32, .55, .55, .55);
	cairo_pattern_add_color_stop_rgb (ui->hornp[1], 0.50, .64, .64, .64);
	cairo_pattern_add_color_stop_rgb (ui->hornp[1], 0.68, .55, .55, .55);
	cairo_pattern_add_color_stop_rgb (ui->hornp[1], 1.00, .57, .57, .57);

	// base .75 + pat + .2 * .5
	ui->hornp[2] = cairo_pattern_create_linear (-x * .5, 0, x * .5, 0);
	cairo_pattern_add_color_stop_rgb (ui->hornp[2], 0.00, .4750, .4750, .4750);  // a=0.
	cairo_pattern_add_color_stop_rgb (ui->hornp[2], 0.10, .3925, .3925, .3925);  // a=.3  c=.2
	cairo_pattern_add_color_stop_rgb (ui->hornp[2], 0.66, .5125, .5125, .5125);  // a=.5  c=.9
	cairo_pattern_add_color_stop_rgb (ui->hornp[2], 0.90, .3925, .3925, .3925);  // a=.3  c=.2
	cairo_pattern_add_color_stop_rgb (ui->hornp[2], 1.00, .4750, .4750, .4750);  // a=0.

	// base g30
	ui->hornp[3] = cairo_pattern_create_linear (-x * .5, 0, x * .5, 0);
	cairo_pattern_add_color_stop_rgb (ui->hornp[3], 0.00, .30, .30, .30);  // a=0.
	cairo_pattern_add_color_stop_rgb (ui->hornp[3], 0.10, .27, .27, .27);  // a=.3  c=.2
	cairo_pattern_add_color_stop_rgb (ui->hornp[3], 0.66, .60, .60, .60);  // a=.5  c=.9
	cairo_pattern_add_color_stop_rgb (ui->hornp[3], 0.90, .27, .27, .27);  // a=.3  c=.2
	cairo_pattern_add_color_stop_rgb (ui->hornp[3], 1.00, .30, .30, .30);  // a=0.
}

static bool horn_expose_event (RobWidget* rw, cairo_t* cr, cairo_rectangle_t *ev) {
	WhirlUI* ui = (WhirlUI*)GET_HANDLE (rw);

	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
	cairo_clip (cr);

	cairo_rectangle (cr, 0, 0, rw->area.width, rw->area.height);
	CairoSetSouerceRGBA (c_trs);
	cairo_fill (cr);

	const float spd = ui->cur_rpm[0];
	const float ang = ui->cur_ang[0] * 2. * M_PI;
	if (spd < 0 || ang < 0) { return TRUE; }

	const int w = rw->area.width;
	const int h = rw->area.height;
#if 0
	cairo_rectangle (cr, 0, 0, w, h);
	CairoSetSouerceRGBA (c_ora);
	cairo_set_line_width (cr, 3.0);
	cairo_stroke (cr);
#endif

	const float xoff = robtk_dial_get_value (ui->s_xzmpos[0]) / 20.0;
	const float zoff = robtk_dial_get_value (ui->s_xzmpos[1]) / 20.0;

	const float cx = rintf (w * .5f + w * .05 * xoff);
	const float cy = rintf (h * .5f + h * .05 * zoff);

	const float sc = (w < 1.5 * h ? w : 1.5 * h) * (1 + .05 * zoff);

	const float ww = sc * .38f;
	const float hh = sc * .13f;
	const float rd = sc * .10f;

	const float sa = sinf (ang);
	const float ca = cosf (ang);
	const float x  = -ww * sa;
	const float y  =  hh * ca;
	const float r0 = rd * .30f;
	const float y0 = hh * -.5f;
	const float bw = r0 * 3.f;
	const float bx = -bw * sa * .5f;
	const float xs = 1.f - .5f * fabsf (sa);

	// sep line
	CairoSetSouerceRGBA (c_blk);
	cairo_set_line_width (cr, 1);
	cairo_move_to (cr, w * .1, rint (h * .5f - y0 * 3.35) + .5);
	cairo_line_to (cr, w * .9, rint (h * .5f - y0 * 3.35) + .5);
	cairo_stroke (cr);

	cairo_matrix_t matrix;

	if (!ui->hornp[0]) {
		create_horn_patterns (ui, bw, hh-y0);
	}

#define DRAW_BASE \
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);                 \
	cairo_set_line_width (cr, bw);                                \
	cairo_move_to (cr, 0, -y0 * .5);                              \
	cairo_line_to (cr, 0, -y0 * 2.);                              \
	cairo_set_source (cr, ui->hornp[3]);                          \
	cairo_stroke (cr);                                            \
	                                                              \
	cairo_save (cr);                                              \
	cairo_translate (cr, 0, y0 * -1.9);                           \
	cairo_scale (cr, 3.0, 0.6);                                   \
	cairo_arc (cr, 0, 0, bw * .5, 0, 2 * M_PI);                   \
	cairo_set_source (cr, ui->hornp[2]);                          \
	cairo_fill (cr);                                              \
	                                                              \
	cairo_arc (cr, 0, 0, bw * .5, 0, M_PI);                       \
	cairo_arc_negative (cr, 0, -y0 * 0.8, bw * .5, M_PI, 0);      \
	cairo_close_path (cr);                                        \
	cairo_set_source (cr, ui->hornp[3]);                          \
	cairo_fill (cr);                                              \
	cairo_restore (cr); \

#define DRAW_HORN(XX, YY, CMP, CLR, BASE)                             \
	cairo_move_to (cr,  r0 * xs, -y0);                            \
	cairo_line_to (cr, -r0 * xs, -y0);                            \
	cairo_line_to (cr, XX x - rd * xs, y0 + YY y);                \
	cairo_line_to (cr, XX x + rd * xs, y0 + YY y);                \
	cairo_close_path (cr);                                        \
	CairoSetSouerceRGBA (CLR ? c_g60 : c_g30);                    \
	cairo_fill (cr);                                              \
	                                                              \
	BASE;                                                         \
	                                                              \
	cairo_matrix_init_identity (&matrix);                         \
	cairo_pattern_set_matrix (ui->hornp[CLR], &matrix);           \
	cairo_move_to (cr, XX bx,  r0 - y0);                          \
	cairo_line_to (cr, XX bx, -r0 - y0);                          \
	cairo_line_to (cr, XX x, y0 + YY y - rd);                     \
	cairo_line_to (cr, XX x, y0 + YY y + rd);                     \
	cairo_close_path (cr);                                        \
	cairo_set_source (cr, ui->hornp[CLR]);                        \
	cairo_fill (cr);                                              \
	                                                              \
	cairo_save (cr);                                              \
	cairo_translate (cr, XX x, y0 + YY y);                        \
	cairo_scale (cr, xs, 1.0);                                    \
	cairo_arc (cr, 0, 0, rd, 0, 2. * M_PI);                       \
	                                                              \
	cairo_matrix_init_translate (&matrix, 0,                      \
		CMP(ang < M_PI) ? -y0 - (YY y) : y0 + (YY y) );       \
	cairo_pattern_set_matrix (ui->hornp[CLR], &matrix);           \
	cairo_set_source (cr, ui->hornp[CLR]);                        \
	if (CMP(ang > .25 * M_PI && ang < 1.25 * M_PI)) {             \
		cairo_fill_preserve (cr);                             \
		cairo_set_source (cr, hornpr);                        \
	}                                                             \
	cairo_fill (cr);                                              \
	cairo_restore (cr);


	const float aa = cosf (ang + .25 * M_PI);
	cairo_pattern_t* hornpr;
	hornpr = cairo_pattern_create_radial (0, 0, 0, 0, 0, rd);
	cairo_pattern_add_color_stop_rgba (hornpr, 0.00, .05, .05, .05, .5 * fabsf (aa));
	cairo_pattern_add_color_stop_rgba (hornpr, 0.70, .25, .25, .25, .4 * fabsf (aa));
	cairo_pattern_add_color_stop_rgba (hornpr, 1.00, .00, .00, .00, .0);

	cairo_translate (cr, cx, cy);

	if (ang < .5 * M_PI || ang > 1.5 * M_PI) {
		DRAW_HORN(-, -,  , 0, );
		DRAW_HORN(+, +, !, 1, DRAW_BASE);
	} else {
		DRAW_HORN(+, +, !, 1, );
		DRAW_HORN(-, -,  , 0, DRAW_BASE);
	}

	cairo_pattern_destroy (hornpr);

	/* speed blur */
	if (spd > 150) {
		cairo_scale (cr, 1.0, hh/ww);
		cairo_set_source_rgba (cr, .4, .4, .4, 0.3);
		cairo_arc (cr, 0, y0, ww, ang - 1.5 * M_PI, ang - .5 * M_PI);
		cairo_fill (cr);

		cairo_set_source_rgba (cr, .7, .7, .7, 0.3);
		cairo_arc (cr, 0, y0, ww, ang - 0.5 * M_PI, ang + .5 * M_PI);
		cairo_fill (cr);
	}

	return TRUE;
}

static bool drum_expose_event (RobWidget* rw, cairo_t* cr, cairo_rectangle_t *ev) {
	WhirlUI* ui = (WhirlUI*)GET_HANDLE (rw);

	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
	cairo_clip (cr);
	cairo_rectangle (cr, 0, 0, rw->area.width, rw->area.height);
	CairoSetSouerceRGBA (c_trs);
	cairo_fill (cr);

	const float spd = ui->cur_rpm[1];
	const float ang = ui->cur_ang[1] * 2. * M_PI;
	if (spd < 0 || ang < 0) { return TRUE; }


	const int w = rw->area.width;
	const int h = rw->area.height;
	const float sc = w < h ? w : h;

#if 0
	cairo_rectangle (cr, 0, 0, w, h);
	CairoSetSouerceRGBA (c_grn);
	cairo_set_line_width (cr, 3.0);
	cairo_stroke (cr);
#endif

	const float cx = rintf (w * .5);
	const float cy = rintf (h - .5 * sc);
	const float yt = cy - sc * .05;
	const float yb = cy + sc * .4;
	const float rd = sc * .4;
	const float yh = (yb - yt) / .2; // ellipse scaled height

	/* bottom and outline */
	cairo_save (cr);
	cairo_translate (cr, cx, yb);
	cairo_scale (cr, 1.0, 0.2);

	cairo_arc (cr, 0, 0, rd, 0, 2 * M_PI);
	cairo_set_source_rgba (cr, .3, .3, .3, 1.0);
	cairo_fill_preserve (cr);
	cairo_set_source_rgba (cr, .0, .0, .0, 1.0);
	cairo_set_line_width (cr, 3.0);
	cairo_stroke (cr);

	cairo_arc (cr, 0, -yh, rd, 0, 2 * M_PI);
	cairo_set_source_rgba (cr, .1, .1, .1, 1.0);
	cairo_set_line_width (cr, 3.0);
	cairo_stroke (cr);

	cairo_restore (cr);


#if 0  // arrow
	const float sa = sinf (ang);
	const float ca = cosf (ang);
	const float x  = -rd * sa;
	const float y  =  rd * ca * .2;
#endif

	const float cng = ang + M_PI;
#define RAD(X) ((X) * 2 * M_PI / 360.)

	/* backside */
	cairo_save (cr);
	cairo_translate (cr, cx, yt);
	cairo_scale (cr, 1.0, 0.2);
	cairo_set_source_rgba (cr, .0, .0, .0, .8);

	if (ang > RAD(45) && ang < RAD(225)) {
		cairo_arc (cr, 0, yh, rd, cng - .25 * M_PI, 2 * M_PI);
		cairo_arc_negative (cr, 0, 0, rd, 2 * M_PI, cng - .25 * M_PI);
		cairo_close_path (cr);
		cairo_fill (cr);
	}
	if (ang > RAD(135) && ang < RAD(315)) {
		cairo_arc (cr, 0, yh, rd, M_PI, cng - 0.75 * M_PI);
		cairo_arc_negative (cr, 0, 0, rd, cng - .75 * M_PI, M_PI);
		cairo_close_path (cr);
		cairo_fill (cr);
	}
	if (ang < RAD(45) || ang > RAD(315)) {
		cairo_arc (cr, 0, yh, rd, M_PI, 2 * M_PI);
		cairo_arc_negative (cr, 0, 0, rd, 2 * M_PI, M_PI);
		cairo_close_path (cr);
		cairo_fill (cr);
	}
	cairo_restore (cr);


	/* bass-reflector */
	const float sa1 = sinf (ang + .25 * M_PI);
	const float sa2 = sinf (ang - .25 * M_PI);
	const float ca1 = cosf (ang + .25 * M_PI);
	const float ca2 = cosf (ang - .25 * M_PI);

#define RXY(SIN, COS) (cx - rd * SIN), (yb + rd * COS * .2)
#define MXY(SIN, COS) (cx - rd * SIN), (cy + rd * COS * .2)

	cairo_set_line_width (cr, 2.0);
	cairo_set_source_rgba (cr, .1, .1, .1, .9);

	// lid
	cairo_save (cr);
	cairo_translate (cr, cx, yb);
	cairo_scale (cr, 1.0, 0.2);
	cairo_arc (cr, 0, -rd / .2, rd, ang - .75 * M_PI, ang - .25 * M_PI);
	cairo_close_path (cr);
	cairo_fill (cr);
	cairo_restore (cr);

	// left tri
	cairo_move_to (cr, MXY( ca1, -sa1));
	cairo_line_to (cr, RXY( sa1,  ca1));
	cairo_line_to (cr, RXY( ca1, -sa1));
	cairo_close_path (cr);
	cairo_fill (cr);

	// right tri
	cairo_move_to (cr, MXY(-ca2, sa2));
	cairo_line_to (cr, RXY( sa2, ca2));
	cairo_line_to (cr, RXY(-ca2, sa2));
	cairo_close_path (cr);
	cairo_fill (cr);

	// ramp
	cairo_move_to (cr, MXY( ca1, -sa1));
	cairo_line_to (cr, RXY( sa1,  ca1));
	cairo_line_to (cr, RXY( sa2,  ca2));
	cairo_line_to (cr, MXY(-ca2,  sa2));
	cairo_close_path (cr);
	cairo_fill (cr);

	// rounded backside
	if (ang > .25 * M_PI && ang <= 1.75 * M_PI) {
		cairo_save (cr);
		cairo_translate (cr, cx, yb);
		cairo_scale (cr, 1.0, 0.2);
		if (ang > .25 * M_PI && ang < .75 * M_PI) {
			cairo_arc (cr, 0, 0, rd, 0, ang - .25 * M_PI);
			cairo_arc_negative (cr, 0, -rd / .2, rd, ang - .25 * M_PI, 0);
			cairo_close_path (cr);
			cairo_fill (cr);
		}
		if (ang > 1.25 * M_PI && ang < 1.75 * M_PI) {
			cairo_arc (cr, 0, 0, rd, M_PI, ang - .25 * M_PI);
			cairo_arc_negative (cr, 0, -rd / .2, rd, ang - .25 * M_PI, M_PI);
			cairo_close_path (cr);
			cairo_fill (cr);
		}
		if (ang >= .75 * M_PI && ang <= 1.75 * M_PI) {
			cairo_arc (cr, 0, 0, rd,  ang - .75 * M_PI, ang - .25 * M_PI);
			cairo_arc_negative (cr, 0, -rd / .2, rd, ang - .25 * M_PI, ang - .75 * M_PI);
			cairo_close_path (cr);
			cairo_fill (cr);
		}
		cairo_restore (cr);
	}

	/* front */
	cairo_save (cr);
	cairo_translate (cr, cx, yt);
	cairo_scale (cr, 1.0, 0.2);
	cairo_set_source_rgba (cr, .4, .4, .4, .45);

	if (ang < RAD(45) || ang > RAD(225)) {
		cairo_arc (cr, 0, yh, rd, cng - 0.25 * M_PI, M_PI);
		cairo_arc_negative (cr, 0, 0, rd, M_PI, cng - .25 * M_PI);
		cairo_close_path (cr);
		cairo_fill (cr);
	}
	if (ang < RAD(135)) {
		cairo_arc (cr, 0, yh, rd, 0, cng - 0.75 * M_PI);
		cairo_arc_negative (cr, 0, 0, rd, cng - .75 * M_PI, 0);
		cairo_close_path (cr);
		cairo_fill (cr);
	}
	if (ang > RAD(315)) {
		cairo_arc (cr, 0, yh, rd, 2 * M_PI, cng - 0.75 * M_PI);
		cairo_arc_negative (cr, 0, 0, rd, cng - .75 * M_PI, 2 * M_PI);
		cairo_close_path (cr);
		cairo_fill (cr);
	}
	if (ang > RAD(135) && ang < RAD(225)) {
		cairo_arc (cr, 0, yh, rd, 0, M_PI);
		cairo_arc_negative (cr, 0, 0, rd, M_PI, 0);
		cairo_close_path (cr);
		cairo_fill (cr);
	}
	cairo_restore (cr);

	/* speed blur */
	if (spd > 180) {
		cairo_save (cr);
		cairo_translate (cr, cx, yt);
		cairo_scale (cr, 1.0, 0.2);
		cairo_arc (cr, 0, 0, rd, 0, M_PI);
		cairo_arc_negative (cr, 0, yh, rd, M_PI, 0);
		cairo_close_path (cr);
		cairo_set_source_rgba (cr, .4, .4, .4, .3);
		cairo_fill (cr);
		cairo_restore (cr);
	}

	return TRUE;
}


/*** GUI value updates ***/

static void update_levers (WhirlUI *ui, float val) {
	const int v = rintf (val);
	int h = v / 3; // 0: stop, 1: slow, 2: fast
	int d = v % 3; // 0: stop, 1: slow, 2: fast
	// lever 0:slow 1:stop 2:fast
	if (robtk_cbtn_get_active (ui->btn_link)) {
		if (ui->last_used_horn_lever) {
			d = h;
		} else {
			h = d;
		}
	}
	ui->set_last_used = false;
	robtk_lever_set_value (ui->lever[0], h > 1 ? h : h ^ 1);
	robtk_lever_set_value (ui->lever[1], d > 1 ? d : d ^ 1);
	ui->set_last_used = true;
}

static void update_rpm_display (WhirlUI *ui, const int which) {
	char txt[32];
	//snprintf (txt, 32, "%6.1f RPM\n%6.1f deg", ui->cur_rpm[which], 360 * ui->cur_ang[which]);
	snprintf (txt, 32, "%6.1f RPM", ui->cur_rpm[which]);
	robtk_lbl_set_text (ui->lbl_rpm[which], txt);
}

static void update_rpm (WhirlUI *ui, const int which, const float val) {
	ui->initialized |= (which + 1) << 2;
	if (val < 0) { return; }
	if (rintf (20 * ui->cur_rpm[which]) == rintf (20 * val)) {
		return;
	}
	ui->cur_rpm[which] = val;
	update_rpm_display (ui, which);
}

static void update_ang (WhirlUI *ui, const int which, const float val) {
	ui->initialized |= (which + 1);
	if (val < 0) { return; }
	if (rintf (240 * ui->cur_ang[which]) == rintf (240 * val)) {
		return;
	}
	ui->cur_ang[which] = val;
	queue_draw (ui->spk_dpy[which]);
}

static void reexpose_bg (WhirlUI *ui) {
	if (ui->gui_bg) {
		cairo_surface_destroy (ui->gui_bg);
		ui->gui_bg = NULL;
	}
	queue_draw (ui->rw);
}

/***  value changed calllbacks ***/

#define SELECT_CALLBACK(name, widget, port, hook) \
static bool cb_sel_ ## name (RobWidget *w, void* handle) { \
	WhirlUI* ui = (WhirlUI*)handle; \
	const float val = robtk_select_get_value (ui->sel_ ## widget); \
	hook; \
	if (ui->disable_signals) return TRUE; \
	ui->write (ui->controller, port, sizeof (float), 0, (const void*) &val); \
	return TRUE; \
}

#define DIAL_CALLBACK(name, widget, port, hook) \
static bool cb_dial_ ## name (RobWidget *w, void* handle) { \
	WhirlUI* ui = (WhirlUI*)handle; \
	hook; \
	if (ui->disable_signals) return TRUE; \
	const float val = robtk_dial_get_value (ui->s_ ## widget); \
	ui->write (ui->controller, port, sizeof (float), 0, (const void*) &val); \
	return TRUE; \
}

#define DIAL_CB_SCALE(name, widget, port, param, hook) \
static bool cb_dial_ ## name (RobWidget *w, void* handle) { \
	WhirlUI* ui = (WhirlUI*)handle; \
	hook; \
	if (ui->disable_signals) return TRUE; \
	const float val = dial_to_param (&(param), robtk_dial_get_value (ui->s_ ## widget)); \
	ui->write (ui->controller, port, sizeof (float), 0, (const void*) &val); \
	return TRUE; \
}

DIAL_CALLBACK(lvl0,   level[0],    B3W_HORNLVL, );
DIAL_CALLBACK(lvl1,   level[1],    B3W_DRUMLVL, );
DIAL_CB_SCALE(rpms0,  rpm_slow[0], B3W_HORNRPMSLOW, rpm_slow[0], );
DIAL_CB_SCALE(rpms1,  rpm_slow[1], B3W_DRUMRPMSLOW, rpm_slow[1], );
DIAL_CB_SCALE(rpmf0,  rpm_fast[0], B3W_HORNRPMFAST, rpm_fast[0], );
DIAL_CB_SCALE(rpmf1,  rpm_fast[1], B3W_DRUMRPMFAST, rpm_fast[1], );
DIAL_CB_SCALE(acc0,   accel[0],    B3W_HORNACCEL, acceleration[0], );
DIAL_CB_SCALE(acc1,   accel[1],    B3W_DRUMACCEL, acceleration[1], );
DIAL_CB_SCALE(dec0,   decel[0],    B3W_HORNDECEL, deceleration[0], );
DIAL_CB_SCALE(dec1,   decel[1],    B3W_DRUMDECEL, deceleration[1], );

DIAL_CB_SCALE(freq0,  ffreq[0],    B3W_FILTAFREQ, filter[0][0], update_eq (ui, 0));
DIAL_CB_SCALE(qual0,  fqual[0],    B3W_FILTAQUAL, filter[0][1], update_eq (ui, 0));
DIAL_CALLBACK(gain0,  fgain[0],    B3W_FILTAGAIN, update_eq (ui, 0));
DIAL_CB_SCALE(freq1,  ffreq[1],    B3W_FILTBFREQ, filter[1][0], update_eq (ui, 1));
DIAL_CB_SCALE(qual1,  fqual[1],    B3W_FILTBQUAL, filter[1][1], update_eq (ui, 1));
DIAL_CALLBACK(gain1,  fgain[1],    B3W_FILTBGAIN, update_eq (ui, 1));
DIAL_CB_SCALE(freq2,  ffreq[2],    B3W_FILTDFREQ, filter[2][0], update_eq (ui, 2));
DIAL_CB_SCALE(qual2,  fqual[2],    B3W_FILTDQUAL, filter[2][1], update_eq (ui, 2));
DIAL_CALLBACK(gain2,  fgain[2],    B3W_FILTDGAIN, update_eq (ui, 2));

DIAL_CALLBACK(hrad,   radius[0],   B3W_HORNRADIUS, );
DIAL_CALLBACK(drad,   radius[1],   B3W_DRUMRADIUS, );

DIAL_CALLBACK(hornx,  xzmpos[0],   B3W_HORNOFFX, queue_draw (ui->spk_dpy[0]););
DIAL_CALLBACK(hornz,  xzmpos[1],   B3W_HORNOFFZ, queue_draw (ui->spk_dpy[0]););
DIAL_CALLBACK(micd,   xzmpos[2],   B3W_MICDIST, );
DIAL_CALLBACK(mica,   xzmpos[3],   B3W_MICANGLE, reexpose_bg (ui););

DIAL_CALLBACK(dwidth, drumwidth,   B3W_DRUMWIDTH, );
DIAL_CALLBACK(hwidth, hornwidth,   B3W_HORNWIDTH, );
DIAL_CALLBACK(leak,   leak,        B3W_HORNLEAK, );

SELECT_CALLBACK(fil0, fil[0],      B3W_FILTATYPE, update_eq (ui, 0));
SELECT_CALLBACK(fil1, fil[1],      B3W_FILTBTYPE, update_eq (ui, 1));
SELECT_CALLBACK(fil2, fil[2],      B3W_FILTDTYPE, update_eq (ui, 2));


static bool cb_dial_brk0 (RobWidget *w, void* handle) {
	WhirlUI* ui = (WhirlUI*)handle;
	if (ui->disable_signals) return TRUE;
	float val = robtk_dial_get_value (ui->s_brakepos[0]);
	if (!robtk_dial_get_state (ui->s_brakepos[0])) { val = 0; }
	ui->write (ui->controller, B3W_HORNBRAKE, sizeof (float), 0, (const void*) &val);
	return TRUE;
}

static bool cb_dial_brk1 (RobWidget *w, void* handle) {
	WhirlUI* ui = (WhirlUI*)handle;
	if (ui->disable_signals) return TRUE;
	float val = robtk_dial_get_value (ui->s_brakepos[1]);
	if (!robtk_dial_get_state (ui->s_brakepos[1])) { val = 0; }
	ui->write (ui->controller, B3W_DRUMBRAKE, sizeof (float), 0, (const void*) &val);
	return TRUE;
}


static void handle_lever (WhirlUI* ui, int vh, int vd) {
	if (ui->disable_signals) return;
	const float val = 3 * (vh > 1 ? vh : vh ^ 1) + (vd > 1 ? vd : vd ^ 1);
	ui->write (ui->controller, B3W_REVSELECT, sizeof (float), 0, (const void*) &val);
}

static bool cb_leverH (RobWidget *w, void* handle) {
	WhirlUI* ui = (WhirlUI*)handle;
	int vh = robtk_lever_get_value (ui->lever[0]);
	int vd = robtk_lever_get_value (ui->lever[1]);
	if (robtk_cbtn_get_active (ui->btn_link)) {
		vd = vh;
		robtk_lever_set_value (ui->lever[1], vh);
		if (ui->set_last_used && !ui->last_used_horn_lever) {
			assert (!ui->disable_signals);
			const float val = 1.f;
			ui->write (ui->controller, B3W_LINKSPEED, sizeof (float), 0, (const void*) &val);
		}
	}
	if (ui->set_last_used) {
		ui->last_used_horn_lever = true;
	}
	handle_lever (ui, vh, vd);
	return TRUE;
}

static bool cb_leverD (RobWidget *w, void* handle) {
	WhirlUI* ui = (WhirlUI*)handle;
	int vh = robtk_lever_get_value (ui->lever[0]);
	int vd = robtk_lever_get_value (ui->lever[1]);
	if (robtk_cbtn_get_active (ui->btn_link)) {
		vh = vd;
		robtk_lever_set_value (ui->lever[0], vd);
		if (ui->set_last_used && ui->last_used_horn_lever) {
			assert (!ui->disable_signals);
			const float val = -1.f;
			ui->write (ui->controller, B3W_LINKSPEED, sizeof (float), 0, (const void*) &val);
		}
	}
	if (ui->set_last_used) {
		ui->last_used_horn_lever = false;
	}
	handle_lever (ui, vh, vd);
	return TRUE;
}

static bool cb_linked (RobWidget *w, void* handle) {
	WhirlUI* ui = (WhirlUI*)handle;
	const float val = robtk_cbtn_get_active (ui->btn_link) ? (ui->last_used_horn_lever ? 1.f : -1.f) : 0.f;

	if (!ui->disable_signals) {
		ui->write (ui->controller, B3W_LINKSPEED, sizeof (float), 0, (const void*) &val);
	}
	if (val == 0.f) {
		return TRUE;
	}

	int vh = robtk_lever_get_value (ui->lever[0]);
	int vd = robtk_lever_get_value (ui->lever[1]);
	if (vh == vd) {
		return TRUE;
	}
	ui->set_last_used = false;
	if (ui->last_used_horn_lever) {
		vd = vh;
		robtk_lever_set_value (ui->lever[1], vh);
	} else {
		vh = vd;
		robtk_lever_set_value (ui->lever[0], vd);
	}
	// cb_leverX() -> handle_lever() -> HOST -> update_levers()
	ui->set_last_used = true;
	return TRUE;
}

static bool cb_adv_en (RobWidget *w, void* handle) {
	WhirlUI* ui = (WhirlUI*)handle;
	bool en = robtk_cbtn_get_active (ui->btn_adv);

	robtk_dial_set_sensitive (ui->s_radius[0], en);
	robtk_dial_set_sensitive (ui->s_radius[1], en);
	robtk_dial_set_sensitive (ui->s_xzmpos[0], en);
	robtk_dial_set_sensitive (ui->s_xzmpos[1], en);
	robtk_dial_set_sensitive (ui->s_xzmpos[2], en);
	robtk_dial_set_sensitive (ui->s_xzmpos[3], en);
	for (int i = 1; i < 7; ++i) {
		robtk_lbl_set_sensitive (ui->lbl_adv[i], en);
	}
	return TRUE;
}

/*** knob tooltips/annotations ***/

static void render_annotation (WhirlUI* ui, cairo_t *cr, const char *txt) {
	int tw, th;
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	PangoLayout * pl = pango_cairo_create_layout (cr);
	pango_layout_set_font_description (pl, ui->font[0]);
	pango_layout_set_text (pl, txt, -1);
	pango_layout_set_alignment (pl, PANGO_ALIGN_CENTER);
	pango_layout_get_pixel_size (pl, &tw, &th);
	cairo_translate (cr, rint (-tw / 2.0) , rint (-th));
	rounded_rectangle (cr, -2, -2, tw + 3, th + 3, 3);
	cairo_set_line_width (cr, 1); \
	cairo_set_source_rgba (cr, .5, .5, .5, .66);
	cairo_stroke_preserve (cr);
	cairo_set_source_rgba (cr, .0, .0, .0, .7);
	cairo_fill (cr);
	CairoSetSouerceRGBA (c_wht);
	pango_cairo_show_layout (cr, pl);
	g_object_unref (pl);
}

static void dial_annotation (RobTkDial * d, cairo_t *cr, void *data) {
	WhirlUI* ui = (WhirlUI*) (d->handle); // re-use callback handle
	const Parameter * p = (const Parameter*) (data);
	char txt[24];
	if (p) {
		if (p->warp != 0) {
			snprintf (txt, 24, p->fmt, dial_to_param (p, d->cur));
		} else {
			snprintf (txt, 24, p->fmt, d->cur);
		}
	} else {
		snprintf (txt, 24, "%+5.2f dB", d->cur);
	}
	cairo_save (cr);
	cairo_translate (cr, rint (d->w_width / 2), rint (d->w_height - 3));
	render_annotation (ui, cr, txt);
	cairo_restore (cr);
}

static void dial_annotation_stereo (RobTkDial * d, cairo_t *cr, void *data) {
	WhirlUI* ui = (WhirlUI*) (data);
	char txt[24];
	if (d->cur == 0) {
		snprintf (txt, 24, "Mono/Left");
	} else if (d->cur == 1.0) {
		snprintf (txt, 24, "Stereo");
	} else if (d->cur == 2.0) {
		snprintf (txt, 24, "Mono/Right");
	} else if (d->cur < 1.0) {
		snprintf (txt, 24, "Left %.0f%%", 100. - d->cur * 100.);
	} else {
		snprintf (txt, 24, "Right %.0f%%", d->cur * 100. - 100.);
	}

	cairo_save (cr);
	cairo_translate (cr, rint (d->w_width / 2), rint (d->w_height - 3));
	render_annotation (ui, cr, txt);
	cairo_restore (cr);
}

static void dial_annotation_brake (RobTkDial * d, cairo_t *cr, void *data) {
	WhirlUI* ui = (WhirlUI*) (data);
	char txt[32];
	if (d->click_state == 0) {
		snprintf (txt, 32, "No Brake\nClick to enable");
	} else if (d->cur == .25) {
		snprintf (txt, 32, "Left");
	} else if (d->cur ==  .5) {
		snprintf (txt, 32, "Back");
	} else if (d->cur == .75) {
		snprintf (txt, 32, "Right");
	} else if (d->cur == 1.0) {
		snprintf (txt, 32, "Front");
	} else {
		snprintf (txt, 32, "%.0f deg", d->cur * 360.);
	}

	cairo_save (cr);
	cairo_translate (cr, rint (d->w_width / 2), rint (d->w_height - 3));
	render_annotation (ui, cr, txt);
	cairo_restore (cr);
}

/*** knob faceplates ***/
static void prepare_faceplates (WhirlUI* ui) {
	cairo_t *cr;
	float xlp, ylp, ang;

#define INIT_DIAL_SF_CLR(VAR, W, H, CLR) \
	VAR = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, W, H); \
	cr = cairo_create (VAR); \
	CairoSetSouerceRGBA (CLR); \
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE); \
	cairo_rectangle (cr, 0, 0, W, H); \
	cairo_fill (cr); \
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER); \

#define INIT_DIAL_SF(VAR, W, H) \
	INIT_DIAL_SF_CLR(VAR, W, H, c_trs);

#define DRAWDIALDOT(XADD, YADD) \
	xlp = GED_CX + XADD + sinf (ang) * (GED_RADIUS + 3.0); \
	ylp = GED_CY + YADD - cosf (ang) * (GED_RADIUS + 3.0); \
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND); \
	CairoSetSouerceRGBA (c_dlf); \
	cairo_set_line_width (cr, 2.5); \
	cairo_move_to (cr, rint (xlp)-.5, rint (ylp)-.5); \
	cairo_close_path (cr); \
	cairo_stroke (cr);

#define DIALDOTS(V, XADD, YADD) \
	ang = (-.75 * M_PI) + (1.5 * M_PI) * (V); \
	DRAWDIALDOT(XADD, YADD)

#define DIALLABLEL(V, TXT, ALIGN) \
	DIALDOTS(V, 6.5, 15.5) \
	xlp = GED_CX + 6.5 + sinf (ang) * (GED_RADIUS + 9.5); \
	ylp = GED_CY + 15.5 - cosf (ang) * (GED_RADIUS + 9.5); \
	write_text_full (cr, TXT, ui->font[0], xlp, ylp,  0, ALIGN, c_dlf);

#define DIALSCALE(V, PARAM, FMT, ALIGN) \
	{ \
		char tmp[24]; \
		snprintf (tmp, 24, FMT, dial_to_param (&(PARAM), (V))); \
		DIALLABLEL (V, tmp, ALIGN); \
	}

#define DIALKSCALE(V, PARAM, ALIGN) \
	{ \
		char tmp[24]; \
		const float val = dial_to_param (&(PARAM), (V)); \
		if (val > 999) { \
			const float cent = rintf (fmod (val / 100., 10.)); \
			if (cent == 0) { \
				snprintf (tmp, 24, "%.0fK", floor (val / 1000.)); \
			} else { \
				snprintf (tmp, 24, "%.0fK%.0f", floor (val / 1000.), cent); \
			} \
		} else if (val < .1) { \
			snprintf (tmp, 24, "%.2f", val); \
			memcpy (tmp, tmp + 1, strlen (tmp)); \
		} else if (val < 1) { \
			snprintf (tmp, 24, "%.1f", val); \
			memcpy (tmp, tmp + 1, strlen (tmp)); \
		} else if (val < 3) { \
			snprintf (tmp, 24, "%.0f", val); \
		} else { \
			snprintf (tmp, 24, "%.0f", 5. * rintf (val / 5)); \
		} \
		DIALDOTS(V, 6.5, 15.5) \
		xlp = GED_CX + 6.5 + sinf (ang) * (GED_RADIUS + 8); \
		ylp = GED_CY + 15.5 - cosf (ang) * (GED_RADIUS + 8); \
		write_text_full (cr, tmp, ui->font[0], xlp, ylp,  0, ALIGN, c_dlf); \
	}


	/* gain knob  -20,.+20*/
	INIT_DIAL_SF_CLR(ui->dial_bg[0], GED_WIDTH + 12, GED_HEIGHT + 20, c_blk);
	DIALLABLEL(0.00, "-20", 1);
	DIALLABLEL(0.25, "-10", 1);
	DIALLABLEL(0.5, "0", 2);
	DIALLABLEL(0.75, "+10", 3);
	DIALLABLEL(1.0, "+20", 3);
	cairo_destroy (cr);

	/* gain knob  -48,.+48*/
	INIT_DIAL_SF(ui->dial_bg[1], GED_WIDTH + 12, GED_HEIGHT + 20);
	DIALLABLEL(0.00, "-48", 1);
	DIALLABLEL(0.25, "-24", 1);
	DIALLABLEL(0.5, "0", 2);
	DIALLABLEL(0.75, "+24", 3);
	DIALLABLEL(1.0, "+48", 3);
	cairo_destroy (cr);

	/* filter quality 0.01, 6.0*/
	INIT_DIAL_SF(ui->dial_bg[2], GED_WIDTH + 12, GED_HEIGHT + 20);
	DIALLABLEL(0.00, ".01", 1);
	DIALSCALE(0.25, filter[0][1], "%.1f", 1);
	DIALSCALE(0.50, filter[0][1], "%.1f", 2);
	DIALSCALE(0.75, filter[0][1], "%.1f", 3);
	DIALSCALE(1.00, filter[0][1], "%.1f", 3);
	cairo_destroy (cr);

#define DIALCOMPLETE(ID, SCALE) \
	INIT_DIAL_SF(ui->dial_bg[ID], GED_WIDTH + 12, GED_HEIGHT + 20); \
	DIALKSCALE(0.00, SCALE, 1); \
	DIALKSCALE(0.25, SCALE, 1); \
	DIALKSCALE(0.50, SCALE, 2); \
	DIALKSCALE(0.75, SCALE, 3); \
	DIALKSCALE(1.00, SCALE, 3); \
	cairo_destroy (cr); \

	// filter freq
	DIALCOMPLETE(3, filter[0][0])
	DIALCOMPLETE(4, filter[1][0])
	DIALCOMPLETE(5, filter[2][0])

	// RPM
	DIALCOMPLETE(6, rpm_slow[0])
	DIALCOMPLETE(7, rpm_slow[1])
	DIALCOMPLETE(8, rpm_fast[0])
	DIALCOMPLETE(9, rpm_fast[1])

	// Accel
	DIALCOMPLETE(10, acceleration[0])
	DIALCOMPLETE(11, acceleration[1])
	DIALCOMPLETE(12, deceleration[0])
	DIALCOMPLETE(13, deceleration[1])

	/* stereo */
	INIT_DIAL_SF_CLR(ui->dial_bg[14], GED_WIDTH + 12, GED_HEIGHT + 20, c_blk);
	{
	DIALLABLEL(0.00, "L", 1);
	DIALDOTS(0.00, 6.5, 15.5)
	DIALDOTS(0.25, 6.5, 15.5)
	DIALLABLEL(0.5, "S", 2);
	DIALDOTS(0.75, 6.5, 15.5)
	DIALLABLEL(1.0, "R", 3);
	}


#define DIALDOTS360(V, XADD, YADD) \
	ang = (2.0 * M_PI) * (V); \
	DRAWDIALDOT(XADD, YADD)

#define DIALLABLEL360(V, TXT, ALIGN) \
	DIALDOTS360(V, 6.5, 15.5) \
	xlp = GED_CX + 6.5 + sinf (ang) * (GED_RADIUS + 9.5); \
	ylp = GED_CY + 15.5 - cosf (ang) * (GED_RADIUS + 9.5); \
	write_text_full (cr, TXT, ui->font[0], xlp, ylp,  0, ALIGN, c_dlf);

	/* 360 deg brake */
	INIT_DIAL_SF(ui->dial_bg[15], GED_WIDTH + 12, GED_HEIGHT + 20);
	{
	DIALDOTS360(0./8, 6.5, 15.5)
	DIALDOTS360(1./8, 6.5, 15.5)
	DIALLABLEL360(2./8, "R" , 3)
	DIALDOTS360(3./8, 6.5, 15.5)
	DIALDOTS360(4./8, 6.5, 15.5)
	DIALDOTS360(5./8, 6.5, 15.5)
	DIALLABLEL360(6./8, "L" , 1)
	DIALDOTS360(7./8, 6.5, 15.5)
	}
	cairo_destroy (cr);


	/* leakage -60..-3*/
	INIT_DIAL_SF_CLR(ui->dial_bg[16], GED_WIDTH + 12, GED_HEIGHT + 20, c_blk);
	DIALLABLEL(0.00, "-63", 1);
	DIALLABLEL(0.25, "-48", 1);
	DIALLABLEL(0.5,  "-33", 2);
	DIALLABLEL(0.75, "-18", 3);
	DIALLABLEL(1.0,  " -3", 3);
	cairo_destroy (cr);

}

/*** layout background ***/

static void bg_size_allocate (RobWidget* rw, int w, int h) {
	WhirlUI *ui = (WhirlUI*)((GLrobtkLV2UI*)rw->top)->ui;
	rtable_size_allocate (rw, w, h);
	reexpose_bg (ui);
#if 0
	printf ("GUI SIZE: %d %d\n", w, h); // XXX
#endif
}

// directly access table layout, query position of elements to draw background
static float tbl_ym (struct rob_table *rt, int r0, int r1) {
	int y0 = 0;
	int y1 = 0;
	for (int i = 0; i < r1; ++i) {
		if (i < r0) {
			y0 += rt->rows[i].acq_h;
		}
		y1 += rt->rows[i].acq_h;
	}
	return floorf ((y1 + y0) * .5) + .5;
}

static float tbl_xm (struct rob_table *rt, int c0, int c1) {
	int x0 = 0;
	int x1 = 0;
	for (int i = 0; i < c1; ++i) {
		if (i < c0) {
			x0 += rt->cols[i].acq_w;
		}
		x1 += rt->cols[i].acq_w;
	}
	return floorf ((x1 + x0) * .5) - .5;
}

static float tbl_y0 (struct rob_table *rt, int r0) {
	int y0 = 0;
	for (int i = 0; i < r0; ++i) {
		y0 += rt->rows[i].acq_h;
	}
	return y0 + .5;
}

static float tbl_x0 (struct rob_table *rt, int c0) {
	int x0 = 0;
	for (int i = 0; i < c0; ++i) {
		x0 += rt->cols[i].acq_w;
	}
	return x0 - .5;
}

static RobWidget* tbl_cld (struct rob_table *rt, const int c, const int r) {
	for (unsigned int i=0; i < rt->nchilds; ++i) {
		struct rob_table_child *tc = &rt->chld[i];
		if (tc->top == r && tc->left == c) {
			return (RobWidget *) tc->rw;
		}
	}
	return NULL;
}

static float tbl_yt (struct rob_table *rt, const int c, const int r, const unsigned int e) {
	RobWidget *rw = tbl_cld (rt, c, r);
	assert (rw && rw->childcount > e);
	RobWidget *cld = rw->children[e];
	return rw->area.y + cld->area.y;
}

static float tbl_yb (struct rob_table *rt, const int c, const int r, const unsigned int e) {
	RobWidget *rw = tbl_cld (rt, c, r);
	assert (rw && rw->childcount > e);
	RobWidget *cld = rw->children[rw->childcount - (e + 1)];
	return rw->area.y + cld->area.y + cld->area.height;
}

// draw fixed window background
static void draw_bg (WhirlUI *ui, const int w, const int h, struct rob_table *rt) {
	assert (!ui->gui_bg);
	ui->gui_bg = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create (ui->gui_bg);

	float c[4];
	get_color_from_theme (1, c);
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
	cairo_rectangle (cr, 0, 0, w ,h);
	CairoSetSouerceRGBA (c_blk);
	cairo_fill (cr);
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

	float x0, x1, y0, y1;

	// leslie box
	x0 = tbl_xm (rt, 2, 3) + 2;
	y0 = tbl_ym (rt, 0, 1);
	x1 = tbl_xm (rt, 7, 8) - 2;
	y1 = tbl_ym (rt, 8, 9);
	rounded_rectangle (cr, x0, y0, (x1 - x0), (y1 - y0), 9);
	cairo_set_line_width (cr, 2.0);
	cairo_set_source_rgb (cr, c[0], c[1], c[2]);
	cairo_fill_preserve (cr);
	CairoSetSouerceRGBA (c_gry);
	cairo_stroke (cr);

	// advanced box
	x0 = tbl_x0 (rt, 6) + 4;
	x1 = tbl_x0 (rt, 7) - 2;
	y0 = tbl_y0 (rt, 2) - 2;
	y1 = tbl_y0 (rt, 7) + 2;
	rounded_rectangle (cr, x0, y0, (x1 - x0), (y1 - y0), 9);
	cairo_set_line_width (cr, 1.0);
	cairo_pattern_t* apat = cairo_pattern_create_linear (0, y0, 0, y1);
	cairo_pattern_add_color_stop_rgba (apat, 0.0, .33, .25, .25, 1.0);
	cairo_pattern_add_color_stop_rgba (apat, 0.5, .27, .21, .21, 1.0);
	cairo_pattern_add_color_stop_rgba (apat, 1.0, .27, .21, .21, 1.0);
	cairo_set_source (cr, apat);
	cairo_fill_preserve (cr);
	cairo_pattern_destroy (apat);
	CairoSetSouerceRGBA (c_gry);
	cairo_stroke (cr);

	cairo_set_line_width (cr, 1.0);
	CairoSetSouerceRGBA (c_g80);

#define ARROW(DX1, DY1, DX2, DY2) \
	cairo_move_to (cr, x1, y1); \
	cairo_rel_line_to (cr, DX1, DY1);  \
	cairo_rel_line_to (cr, DX2, DY2); \
	cairo_close_path (cr); \
	cairo_fill (cr);

#define ARROW_LEFT  ARROW(-5, -5,   0, 10)
#define ARROW_DOWN  ARROW(-5, -5,  10,  0)
#define ARROW_UP    ARROW( 5,  5, -10,  0)

	// input lines
	x0 = tbl_x0 (rt, 0);
	y0 = tbl_ym (rt, 4, 5);
	x1 = tbl_x0 (rt, 1) + 1;
	y1 = tbl_ym (rt, 5, 7);

	cairo_move_to (cr, x0, y0);
	cairo_curve_to (cr, x1, y0, x0, y1, x1, y1);
	cairo_stroke (cr);
	x1 += 2;
	y1 -= 1;
	ARROW_LEFT;
	x1 -= 2;

	y1 = tbl_ym (rt, 2, 4);
	cairo_move_to (cr, x0, y0);
	cairo_curve_to (cr, x1, y0, x0, y1, x1, y1);
	cairo_stroke (cr);
	x1 += 2;
	y1 += 1;
	ARROW_LEFT;

	// filter to box
	x0 = tbl_x0 (rt, 2) - 1;
	x1 = tbl_xm (rt, 2, 3);
	y0 = y1 = tbl_ym (rt, 1, 4);
	cairo_move_to (cr, x0, y0);
	cairo_line_to (cr, x1, y1);
	cairo_stroke (cr);
	ARROW_LEFT;

	y0 = y1 = tbl_ym (rt, 5, 8);
	cairo_move_to (cr, x0, y0);
	cairo_line_to (cr, x1, y1);
	cairo_stroke (cr);
	ARROW_LEFT;

	// output baffle
	x0 = tbl_xm (rt, 7, 8);
	x1 = tbl_xm (rt, 8, 9) + 1;
	y0 = tbl_ym (rt, 7, 8);
	y1 = ceil (tbl_yb (rt, 8, 6, 1)) + .5;

	cairo_move_to  (cr, x0, y0 - 1);
	cairo_curve_to (cr, x1 - 1, y0 - 1, x1 - 1, y0 - 1, x1 - 1, y1 + 2);
	cairo_stroke (cr);
	cairo_move_to (cr, x0, y0 + 1);
	cairo_curve_to (cr, x1 + 1, y0 + 1, x1 + 1, y0 + 1, x1 + 1, y1 + 2);
	cairo_stroke (cr);
	ARROW_UP;
	write_text_full (cr, "Drum",  ui->font[0], x0 + 4, y0 + 4,  0, 9, c_wht);

	// drum stereo width to level
	y0 = tbl_ym (rt, 6, 7);
	y1 = ceil (tbl_yb (rt, 8, 5, 1)) + .5;
	cairo_move_to (cr, x1 - 1, y0);
	cairo_line_to (cr, x1 - 1, y1 + 2);
	cairo_stroke (cr);
	cairo_move_to (cr, x1 + 1, y0);
	cairo_line_to (cr, x1 + 1, y1 + 2);
	cairo_stroke (cr);
	ARROW_UP;

	// drum level to out
	y0 = tbl_ym (rt, 5, 6);
	y1 = tbl_ym (rt, 4, 5) + 8;
	cairo_move_to (cr, x1 - 1, y0);
	cairo_line_to (cr, x1 - 1, y1 + 2);
	cairo_stroke (cr);
	cairo_move_to (cr, x1 + 1, y0);
	cairo_line_to (cr, x1 + 1, y1 + 2);
	cairo_stroke (cr);
	ARROW_UP;

	// leak
	x1 = tbl_x0 (rt, 8) + 4;
	y0 = tbl_ym (rt, 1, 2);
	y1 = y0;
	cairo_move_to (cr, x0, y0);
	cairo_line_to (cr, x1, y1);
	cairo_stroke (cr);
	ARROW_LEFT;

	float ymix = .5 + rint(.5 * (tbl_yb (rt, 8, 1, 1) + tbl_yt (rt, 8, 2, 1)));

	// leak to mixer
	x0 = tbl_xm (rt, 8, 9) + 1;
	x1 = x0;
	y0 = tbl_ym (rt, 1, 2);
	y1 = ymix - 8;
	cairo_move_to (cr, x1, y0);
	cairo_line_to (cr, x1, y1);
	cairo_stroke (cr);
	ARROW_DOWN;

	// output horn to mixer w/leak
	x0 = tbl_xm (rt, 7, 8);
	x1 = tbl_xm (rt, 8, 9) - 8;
	y0 = y1 = ymix;
	cairo_move_to (cr, x0, y1 - 1);
	cairo_line_to (cr, x1 - 1, y1 - 1);
	cairo_stroke (cr);
	cairo_move_to (cr, x0, y1 + 1);
	cairo_line_to (cr, x1 - 1, y1 + 1);
	cairo_stroke (cr);
	ARROW_LEFT;
	write_text_full (cr, "Horn", ui->font[0], x0 + 4, y1 - 4,  0, 6, c_wht);

	// leak + horn mixer
	x1 = tbl_xm (rt, 8, 9) + 1;
	cairo_arc (cr, x1, y0, 8, 0., 2. * M_PI);
	cairo_stroke (cr);

	cairo_move_to (cr, x1, y0 - 5);
	cairo_line_to (cr, x1, y0 + 5);
	cairo_stroke (cr);
	cairo_move_to (cr, x1 - 5, y0);
	cairo_line_to (cr, x1 + 5, y0);
	cairo_stroke (cr);

	// mixer to horn stereo
	y0 = ymix + 8;
	y1 = floor (tbl_yt (rt, 8, 2, 1)) + .5;
	cairo_move_to (cr, x1 - 1, y0);
	cairo_line_to (cr, x1 - 1, y1 - 2);
	cairo_stroke (cr);
	cairo_move_to (cr, x1 + 1, y0);
	cairo_line_to (cr, x1 + 1, y1 - 2);
	cairo_stroke (cr);
	ARROW_DOWN;

	// horn-stereo to level
	y0 = ceil (tbl_yb (rt, 8, 2, 0)) + .5;
	y1 = floor (tbl_yt (rt, 8, 3, 1)) + .5;
	cairo_move_to (cr, x1 - 1, y0);
	cairo_line_to (cr, x1 - 1, y1 - 2);
	cairo_stroke (cr);
	cairo_move_to (cr, x1 + 1, y0);
	cairo_line_to (cr, x1 + 1, y1 - 2);
	cairo_stroke (cr);
	ARROW_DOWN;

	// horn-level to output mix
	y0 = tbl_ym (rt, 3, 4);
	y1 = tbl_ym (rt, 4, 5) - 8;
	cairo_move_to (cr, x1 - 1, y0);
	cairo_line_to (cr, x1 - 1, y1 - 2);
	cairo_stroke (cr);
	cairo_move_to (cr, x1 + 1, y0);
	cairo_line_to (cr, x1 + 1, y1 - 2);
	cairo_stroke (cr);
	ARROW_DOWN;

	// output
	y0 = tbl_ym (rt, 4, 5);
	y1 = tbl_y0 (rt, 5);
	cairo_arc (cr, x1, y0, 8, 0., 2. * M_PI);
	cairo_stroke (cr);

	cairo_move_to (cr, x1, y0 - 5);
	cairo_line_to (cr, x1, y0 + 5);
	cairo_stroke (cr);
	cairo_move_to (cr, x1 - 5, y0);
	cairo_line_to (cr, x1 + 5, y0);
	cairo_stroke (cr);

	// output arrow
	x0 = x1 + 8; y1 = y0;
	x1 = tbl_x0 (rt, 9) - 1;
	cairo_move_to (cr, x0 - 1, y0 - 1);
	cairo_line_to (cr, x1 - 1, y1 - 1);
	cairo_stroke (cr);
	cairo_move_to (cr, x0 - 1, y0 + 1);
	cairo_line_to (cr, x1 - 1, y1 + 1);
	cairo_stroke (cr);
	ARROW_LEFT;

	// get bounds of drum-table-cell
	x0 = tbl_x0 (rt, 3);
	x1 = tbl_x0 (rt, 4);

	/* *** drum speaker ***/
	y0 = tbl_y0 (rt, 6);
	y1 = tbl_y0 (rt, 8);

	// calc size alike drum itself
	const int dw = (x1 - x0);
	const int dh = (y1 - y0);
	const float sc = dw < dh ? dw : dh;

	const float cx = rintf (x0 + dw * .5);
	const float rd = sc * .4;
	const float rs = rd * .45;
	const float yb = y1 - sc * .77;

	CairoSetSouerceRGBA (c_blk);

	cairo_matrix_t matrix;
	cairo_pattern_t* spat[2];

	spat[0] = cairo_pattern_create_linear (-rs, 0, rs, 0);
	cairo_pattern_add_color_stop_rgba (spat[0], 0.00, .30, .30, .30, .0);
	cairo_pattern_add_color_stop_rgba (spat[0], 0.15, .20, .20, .20, .3);
	cairo_pattern_add_color_stop_rgba (spat[0], 0.70, .90, .90, .90, .5);
	cairo_pattern_add_color_stop_rgba (spat[0], 0.90, .20, .20, .20, .3);
	cairo_pattern_add_color_stop_rgba (spat[0], 1.00, .30, .30, .30, .0);

	spat[1] = cairo_pattern_create_linear (2 * rs, -rs, -2 * rs, rs);
	cairo_pattern_add_color_stop_rgba (spat[1], 0.00, .30, .30, .30, .0);
	cairo_pattern_add_color_stop_rgba (spat[1], 0.30, .30, .30, .30, .1);
	cairo_pattern_add_color_stop_rgba (spat[1], 0.50, .90, .90, .90, .3);
	cairo_pattern_add_color_stop_rgba (spat[1], 0.70, .30, .30, .30, .1);
	cairo_pattern_add_color_stop_rgba (spat[1], 1.00, .30, .30, .90, .0);

	cairo_save (cr);
	cairo_translate (cr, cx, yb);
	cairo_scale (cr, 1.0, 0.2);

	// membrane

	// outline behind membrane
	cairo_save (cr);
	cairo_arc (cr, 0, 0, rd, 0, M_PI);
	cairo_arc_negative (cr, 0, -rd/.50, rs, M_PI, 0);
	cairo_close_path (cr);
	cairo_clip (cr);
	cairo_arc (cr, 0, 0, rd, 0, 2 * M_PI);
	CairoSetSouerceRGBA (c_blk);
	cairo_stroke (cr);
	cairo_restore (cr);

	// membrane, again
	cairo_arc (cr, 0, 0, rd, 0, M_PI);
	cairo_arc_negative (cr, 0, -rd/.50, rs, M_PI, 0);
	cairo_close_path (cr);
	cairo_set_source_rgba (cr, .6, .6, .6, .5);
	cairo_fill_preserve (cr);
	CairoSetSouerceRGBA (c_blk);
	cairo_stroke (cr);

	// middle brace
	cairo_arc (cr, 0, 0, rd,                .45 * M_PI, .55 * M_PI);
	cairo_arc_negative (cr, 0, -rd/.50, rs, .55 * M_PI, .45 * M_PI);
	cairo_close_path (cr);
	cairo_set_source (cr, spat[0]);
	cairo_fill_preserve (cr);
	cairo_set_source_rgba (cr, .0, .0, .0, .6);
	cairo_fill (cr);

	// right brace
	cairo_arc (cr, 0, 0, rd,                .12 * M_PI, .22 * M_PI);
	cairo_arc_negative (cr, 0, -rd/.50, rs, .22 * M_PI, .12 * M_PI);
	cairo_close_path (cr);
	cairo_set_source (cr, spat[0]);
	cairo_fill_preserve (cr);
	cairo_set_source_rgba (cr, .0, .0, .0, .6);
	cairo_fill (cr);

	// left brace
	cairo_arc (cr, 0, 0, rd,                .78 * M_PI, .88 * M_PI);
	cairo_arc_negative (cr, 0, -rd/.50, rs, .88 * M_PI, .78 * M_PI);
	cairo_close_path (cr);
	cairo_set_source (cr, spat[0]);
	cairo_fill_preserve (cr);
	cairo_set_source_rgba (cr, .0, .0, .0, .7);
	cairo_fill (cr);

	// magnet
	cairo_arc (cr, 0, -rd/.50, rs, 0,  M_PI);
	cairo_line_to (cr, -rs, -rd/.30);
	cairo_arc_negative (cr, 0, -rd/.30, rs, M_PI, 0);
	cairo_line_to (cr, rs,  -rd/.5);
	cairo_close_path (cr);

	cairo_set_source_rgba (cr, .1, .1, .1, .4);
	cairo_fill_preserve (cr);
	cairo_set_source (cr, spat[0]);
	cairo_fill_preserve (cr);
	CairoSetSouerceRGBA (c_blk);
	cairo_stroke (cr);

	// top
	cairo_arc (cr, 0, -rd/.30, rs, 0,  2 * M_PI);
	cairo_set_source_rgba (cr, .1, .1, .1, .5);
	cairo_fill_preserve (cr);
	cairo_matrix_init_translate (&matrix, 0, yb * .2 + 2 * rd);
	cairo_pattern_set_matrix (spat[1], &matrix);
	cairo_set_source (cr, spat[1]);
	cairo_fill_preserve (cr);
	CairoSetSouerceRGBA (c_blk);
	cairo_stroke (cr);

	// middle ring
	cairo_arc (cr, 0, -rd/.50, rs, 0,  M_PI);
	cairo_stroke (cr);

	// bottom mount
	CairoSetSouerceRGBA (c_blk);
	cairo_arc (cr, 0, -1, rd + 1, 0, M_PI);
	cairo_arc_negative (cr, 0, 30, rd + 1, M_PI, 0);
	cairo_close_path (cr);
	CairoSetSouerceRGBA (c_blk);
	cairo_fill (cr);

	cairo_restore (cr);

	cairo_pattern_destroy (spat[0]);
	cairo_pattern_destroy (spat[1]);

	y0 = rintf (tbl_ym (rt, 5, 8));
	x0 = rintf (tbl_x0 (rt, 4));
	write_text_full (cr, "not to scale", ui->font[1], x0, y0, 1.5 * M_PI, 5, c_gry);


	/* *** microphone positions ***/
	// TODO separate these from the main static background
	// drum mics
	y0 = rintf (tbl_ym (rt, 7, 8));
	x0 = rintf (cx - sc * .5); // re-use pos from speaker
	write_text_full (cr, "L", ui->font[1], x0, y0,  0, 3, c_wht);
	x0 = rintf (cx + sc * .5);
	write_text_full (cr, "R", ui->font[1], x0, y0,  0, 1, c_wht);

	// horn mics
	const float mang = robtk_dial_get_value (ui->s_xzmpos[3]) * M_PI / 360.f;
	const float hcy = rintf (tbl_ym (rt, 4, 6));
	const float hcx = rintf (tbl_xm (rt, 3, 4));
	const float hsc = ui->spk_dpy[0]->area.width < 1.5 * ui->spk_dpy[0]->area.height ? ui->spk_dpy[0]->area.width : 1.5 *  ui->spk_dpy[0]->area.height;
	const float hww = hsc * .45f;
	const float hhh = hsc * .23f;
	y0 = hcy - hhh * cos (mang);
	x0 = hcx - hww * sin (mang);
	write_text_full (cr, "L", ui->font[1], x0, y0,  0, 1, c_wht);
	x0 = hcx + hww * sin (mang);
	write_text_full (cr, "R", ui->font[1], x0, y0,  0, 3, c_wht);

	write_text_full (cr, ui->nfo, ui->font[0], 22, h - 21, 1.5 * M_PI, 6, c_gry);

	cairo_destroy (cr);
}

// main expose function
static bool bg_expose_event (RobWidget* rw, cairo_t* cr, cairo_rectangle_t *ev)
{
	WhirlUI *ui = (WhirlUI*)((GLrobtkLV2UI*)rw->top)->ui;

	if (!ui->gui_bg) {
		draw_bg (ui, rw->area.width, rw->area.height, (struct rob_table*)rw->self);
	}

	cairo_save (cr);
	cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
	cairo_clip (cr);
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface (cr, ui->gui_bg, 0, 0);
	cairo_paint (cr);
	cairo_restore (cr);

#if 0
	struct rob_table *rt = (struct rob_table*)rw->self;
	float xx = 0;
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	for (int c = 0; c < rt->ncols; ++c) {
		float yy = 0;
		for (int r = 0; r < rt->nrows; ++r) {
			cairo_rectangle (cr, xx, yy, rt->cols[c].acq_w, rt->rows[r].acq_h);
			float c[3];
			c[0] = rand() / (float)RAND_MAX;
			c[1] = rand() / (float)RAND_MAX;
			c[2] = rand() / (float)RAND_MAX;
			cairo_set_source_rgba (cr, c[0], c[1], c[2], 0.02);
			cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
			cairo_fill (cr);
			yy += rt->rows[r].acq_h;
		}
		xx += rt->cols[c].acq_w;
	}
#endif

	return rcontainer_expose_event_no_clear (rw, cr, ev);
}

// small table expose function (add borders), code copy from robtk/layout.h
static bool tblbox_expose_event (RobWidget* rw, cairo_t* cr, cairo_rectangle_t *ev)
{
	float c[4];
	if (!strcmp (ROBWIDGET_NAME(rw), "motor")) {
		c[0] = 0.21; c[1] = 0.21; c[2] = 0.27; c[3] = 1.0;
	} else {
		get_color_from_theme (1, c);
	}

	if (rw->resized) {
		cairo_rectangle_t event;
		event.x = MAX(0, ev->x - rw->area.x);
		event.y = MAX(0, ev->y - rw->area.y);
		event.width  = MIN(rw->area.x + rw->area.width , ev->x + ev->width)  - MAX(ev->x, rw->area.x);
		event.height = MIN(rw->area.y + rw->area.height, ev->y + ev->height) - MAX(ev->y, rw->area.y);
		cairo_save (cr);
		cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgb (cr, c[0], c[1], c[2]);
		rounded_rectangle (cr, event.x, event.y, event.width, event.height, 9);
		cairo_fill_preserve (cr);
		cairo_clip_preserve (cr);
		CairoSetSouerceRGBA (c_gry);
		cairo_set_line_width (cr, 2.0);
		cairo_stroke (cr);
		cairo_restore (cr);
	} else {
		cairo_save (cr);
		cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
		cairo_clip (cr);
		cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgb (cr, c[0], c[1], c[2]);
		rounded_rectangle (cr, 0, 0, rw->area.width, rw->area.height, 9);
		cairo_fill_preserve (cr);
		cairo_clip_preserve (cr);
		CairoSetSouerceRGBA (c_gry);
		cairo_set_line_width (cr, 2.0);
		cairo_stroke (cr);
		cairo_restore (cr);
	}
	return rcontainer_expose_event_no_clear (rw, cr, ev);
}

static bool noop_expose_event (RobWidget* rw, cairo_t* cr, cairo_rectangle_t *ev) {
	return TRUE;
}


/*** top-level layout & widget initialiaztion ***/

static RobWidget * toplevel (WhirlUI* ui, void * const top) {
	ui->rw = rob_table_new (/*rows*/ 9, /*cols*/ 9, FALSE);
	robwidget_make_toplevel (ui->rw, top);

	ui->font[0] = pango_font_description_from_string ("Mono 9px");
	ui->font[1] = pango_font_description_from_string ("Sans 12px");

	prepare_faceplates (ui);

#define LB_W(PTR) robtk_lbl_widget(PTR)
#define DL_W(PTR) robtk_dial_widget(PTR)
#define SL_W(PTR) robtk_select_widget(PTR)
#define SP_W(PTR) robtk_sep_widget(PTR)
#define LV_W(PTR) robtk_lever_widget(PTR)
#define CB_W(PTR) robtk_cbtn_widget(PTR)
#define LABEL_BACKGROUND(LBL, R, G, B, A) \
	ui->LBL->bg[0] = R; ui->LBL->bg[1] = G; ui->LBL->bg[2] = B; ui->LBL->bg[3] = A;

	ui->btn_link = robtk_cbtn_new ("Link", GBT_LED_LEFT, false);
	robtk_cbtn_set_color_on (ui->btn_link,  .3, .9, .3);
	robtk_cbtn_set_color_off (ui->btn_link, .1, .3, .1);

	ui->s_drumwidth = robtk_dial_new_with_size (0, 2, .05,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
	robtk_dial_set_default (ui->s_drumwidth, 1.0);
	robtk_dial_set_detent_default (ui->s_drumwidth, true);
	robtk_dial_set_scroll_mult (ui->s_drumwidth, 5.f);
	robtk_dial_annotation_callback (ui->s_drumwidth, dial_annotation_stereo, ui);
	robtk_dial_set_surface (ui->s_drumwidth, ui->dial_bg[14]);

	ui->s_hornwidth = robtk_dial_new_with_size (0, 2, .05,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
	robtk_dial_set_default (ui->s_hornwidth, 1.0);
	robtk_dial_set_detent_default (ui->s_hornwidth, true);
	robtk_dial_set_scroll_mult (ui->s_hornwidth, 5.f);
	robtk_dial_annotation_callback (ui->s_hornwidth, dial_annotation_stereo, ui);
	robtk_dial_set_surface (ui->s_hornwidth, ui->dial_bg[14]);

	ui->s_leak = robtk_dial_new_with_size (-63, -3, .02,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
	robtk_dial_set_default (ui->s_leak, -16.47);
	robtk_dial_set_scroll_mult (ui->s_leak, 5.f);
	robtk_dial_annotation_callback (ui->s_leak, dial_annotation, NULL);
	robtk_dial_set_surface (ui->s_leak, ui->dial_bg[16]);

	ui->lbl_drumwidth  = robtk_lbl_new ("Stereo");
	ui->lbl_hornwidth  = robtk_lbl_new ("Stereo");
	ui->lbl_leak  = robtk_lbl_new ("Leakage");
	LABEL_BACKGROUND(lbl_leak, 0, 0, 0, 1);
	LABEL_BACKGROUND(lbl_drumwidth, 0, 0, 0, 1);
	LABEL_BACKGROUND(lbl_hornwidth, 0, 0, 0, 1);

	ui->box_drmmic = rob_vbox_new (FALSE, 0);
	ui->sep_drmmic = robtk_sep_new (FALSE);
	rob_vbox_child_pack (ui->box_drmmic, DL_W(ui->s_drumwidth), FALSE, FALSE);
	rob_vbox_child_pack (ui->box_drmmic, LB_W(ui->lbl_drumwidth), FALSE, FALSE);
	rob_vbox_child_pack (ui->box_drmmic, SP_W(ui->sep_drmmic), TRUE, TRUE);

	ui->box_hrnmic = rob_vbox_new (FALSE, 0);
	ui->sep_hrnmic = robtk_sep_new (FALSE);
	rob_vbox_child_pack (ui->box_hrnmic, SP_W(ui->sep_hrnmic), TRUE, TRUE);
	rob_vbox_child_pack (ui->box_hrnmic, DL_W(ui->s_hornwidth), FALSE, FALSE);
	rob_vbox_child_pack (ui->box_hrnmic, LB_W(ui->lbl_hornwidth), FALSE, FALSE);

	ui->box_leak = rob_vbox_new (FALSE, 0);
	ui->sep_leak = robtk_sep_new (FALSE);
	rob_vbox_child_pack (ui->box_leak, DL_W(ui->s_leak), FALSE, FALSE);
	rob_vbox_child_pack (ui->box_leak, LB_W(ui->lbl_leak), FALSE, FALSE);
	rob_vbox_child_pack (ui->box_leak, SP_W(ui->sep_leak), TRUE, TRUE);

	// distances, mic pos & angle
	for (int i = 0; i < 4; ++i) {
		ui->s_xzmpos[i] = robtk_dial_new (xzmpos[i].min, xzmpos[i].max, 1./2);
		robtk_dial_set_default (ui->s_xzmpos[i], xzmpos[i].dflt);
		robtk_dial_annotation_callback (ui->s_xzmpos[i], dial_annotation, (void*)&xzmpos[i]);
		//robtk_dial_set_surface (ui->s_xzmpos[i], ui->dial_bg[15]); // 1,2 + 3
	}

	ui->lbl_flt[0][0]  = robtk_lbl_new ("<markup><b>Horn Character</b></markup>");
	ui->lbl_flt[1][0]  = robtk_lbl_new ("<markup><b>Horn Split</b></markup>");
	ui->lbl_flt[2][0]  = robtk_lbl_new ("<markup><b>Drum Split</b></markup>");

	for (int i = 0; i < 3; ++i) {
		ui->tbl_flt[i] = rob_table_new (/*rows*/ 5, /*cols*/ 3, FALSE);
		ui->sel_fil[i] = robtk_select_new ();
		ui->s_ffreq[i] = robtk_dial_new_with_size (0, 1, 1./160,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->s_fqual[i] = robtk_dial_new_with_size (0, 1, 1./90,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->s_fgain[i] = robtk_dial_new_with_size (filter[i][2].min, filter[i][2].max, 0.2,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);

		// transfer function display
		ui->fil_tf[i] = robwidget_new (ui);
		robwidget_set_alignment (ui->fil_tf[i], .5, .5);
		robwidget_set_expose_event (ui->fil_tf[i], m0_expose_event);
		robwidget_set_size_request (ui->fil_tf[i], m0_size_request);
		robwidget_set_size_allocate (ui->fil_tf[i], m0_size_allocate);
		robwidget_set_mousemove (ui->fil_tf[i], m0_mouse_move);
		robwidget_set_mouseup (ui->fil_tf[i], m0_mouse_up);
		robwidget_set_mousedown (ui->fil_tf[i], m0_mouse_down);
		robwidget_set_mousescroll (ui->fil_tf[i], m0_mouse_scroll);

		// table background
		if (i > 0) {
			robwidget_set_expose_event (ui->tbl_flt[i], tblbox_expose_event);
			ROBWIDGET_SETNAME(ui->tbl_flt[i], "filter");
		}

		robtk_dial_set_constained (ui->s_ffreq[i], false);
		robtk_dial_set_constained (ui->s_fqual[i], false);

		robtk_dial_set_scroll_mult (ui->s_fgain[i], 5.f);
		robtk_dial_set_scroll_mult (ui->s_ffreq[i], 4.f);
		robtk_dial_set_scroll_mult (ui->s_fqual[i], 5.f);

		robtk_dial_set_default (ui->s_ffreq[i], param_to_dial (&filter[i][0], filter[i][0].dflt));
		robtk_dial_set_default (ui->s_fqual[i], param_to_dial (&filter[i][1], filter[i][1].dflt));
		robtk_dial_set_default (ui->s_fgain[i], filter[i][2].dflt);

		robtk_dial_annotation_callback (ui->s_ffreq[i], dial_annotation, (void*)&filter[i][0]);
		robtk_dial_annotation_callback (ui->s_fqual[i], dial_annotation, (void*)&filter[i][1]);
		robtk_dial_annotation_callback (ui->s_fgain[i], dial_annotation, NULL);

		robtk_dial_set_surface (ui->s_ffreq[i], ui->dial_bg[3 + i]);
		robtk_dial_set_surface (ui->s_fqual[i], ui->dial_bg[2]);
		robtk_dial_set_surface (ui->s_fgain[i], ui->dial_bg[1]);

		ui->lbl_flt[i][1]  = robtk_lbl_new ("Type:"); // unused
		ui->lbl_flt[i][2]  = robtk_lbl_new ("Freq");
		ui->lbl_flt[i][3]  = robtk_lbl_new ("Q");
		ui->lbl_flt[i][4]  = robtk_lbl_new ("Gain");

		robtk_select_add_item (ui->sel_fil[i], 0, "Low Pass");
		robtk_select_add_item (ui->sel_fil[i], 1, "High Pass");
		robtk_select_add_item (ui->sel_fil[i], 2, "Band Pass Q peak");
		robtk_select_add_item (ui->sel_fil[i], 3, "Band Pass 0dB peak");
		robtk_select_add_item (ui->sel_fil[i], 4, "Notch");
		robtk_select_add_item (ui->sel_fil[i], 5, "All Pass");
		robtk_select_add_item (ui->sel_fil[i], 6, "PEQ");
		robtk_select_add_item (ui->sel_fil[i], 7, "Low Shelf");
		robtk_select_add_item (ui->sel_fil[i], 8, "High Shelf");

		rob_table_attach (ui->tbl_flt[i], LB_W(ui->lbl_flt[i][0]), 0, 3, 0, 1,  3, 4, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_flt[i], SL_W(ui->sel_fil[i]),    0, 3, 1, 2,  4, 0, RTK_EXANDF, RTK_SHRINK);

		rob_table_attach (ui->tbl_flt[i], LB_W(ui->lbl_flt[i][2]), 0, 1, 2, 3,  3, 2, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_flt[i], LB_W(ui->lbl_flt[i][3]), 1, 2, 2, 3,  3, 2, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_flt[i], LB_W(ui->lbl_flt[i][4]), 2, 3, 2, 3,  3, 2, RTK_EXANDF, RTK_SHRINK);

		rob_table_attach (ui->tbl_flt[i], DL_W(ui->s_ffreq[i]),    0, 1, 3, 4,  4, 2, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_flt[i], DL_W(ui->s_fqual[i]),    1, 2, 3, 4,  4, 2, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_flt[i], DL_W(ui->s_fgain[i]),    2, 3, 3, 4,  4, 2, RTK_EXANDF, RTK_SHRINK);

		rob_table_attach (ui->tbl_flt[i], ui->fil_tf[i],           0, 3, 4, 5,  4, 4, RTK_EXANDF, RTK_EXANDF);
	}

	robtk_select_set_default_item (ui->sel_fil[0], 0);
	robtk_select_set_default_item (ui->sel_fil[1], 7);
	robtk_select_set_default_item (ui->sel_fil[2], 8);


	ui->lbl_mix[0] = robtk_lbl_new ("Horn Level");
	ui->lbl_mix[1] = robtk_lbl_new ("Drum Level");
	ui->lbl_brk[0] = robtk_lbl_new ("Brake:");
	ui->lbl_brk[1] = robtk_lbl_new ("Brake:");

	LABEL_BACKGROUND(lbl_mix[0], 0, 0, 0, 1);
	LABEL_BACKGROUND(lbl_mix[1], 0, 0, 0, 1);

	ui->lbl_mtr[0][0]  = robtk_lbl_new ("<markup><b>Horn Rotor</b></markup>");
	ui->lbl_mtr[1][0]  = robtk_lbl_new ("<markup><b>Drum Rotor</b></markup>");

	for (int i = 0; i < 2; ++i) {
		ui->tbl_mtr[i] = rob_table_new (/*rows*/ 7, /*cols*/ 2, FALSE);
		ui->box_mix[i] = rob_vbox_new (FALSE, 0);
		ui->box_brk[i] = rob_hbox_new (FALSE, 0);

		ui->sep_mix[2*i]   = robtk_sep_new (FALSE);
		ui->sep_mix[2*i+1] = robtk_sep_new (FALSE);
		ui->sep_mix[2*i]->m_height = 10 + i;

		robwidget_set_expose_event (ui->box_mix[i], rcontainer_expose_event_no_clear);
		robwidget_set_expose_event (ui->box_mix[i], rcontainer_expose_event_no_clear);
		robwidget_set_expose_event (ui->sep_mix[2*i]->rw, noop_expose_event);
		robwidget_set_expose_event (ui->sep_mix[2*i+1]->rw, noop_expose_event);

		robwidget_set_expose_event (ui->tbl_mtr[i], tblbox_expose_event);
		ROBWIDGET_SETNAME(ui->tbl_mtr[i], "motor");

		ui->s_level[i] = robtk_dial_new_with_size (-20, 20, .02,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->s_brakepos[i] = robtk_dial_new_with_size (1./360, 1, 1./360,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->s_rpm_slow[i] = robtk_dial_new_with_size (0, 1, 1./96,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->s_rpm_fast[i] = robtk_dial_new_with_size (0, 1, 1./96,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->s_accel[i] = robtk_dial_new_with_size (0, 1, 1./96,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->s_decel[i] = robtk_dial_new_with_size (0, 1, 1./96,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->s_radius[i] = robtk_dial_new (9, 50, 1./2);

		ui->s_level[i]->displaymode = 16;
		ui->s_rpm_slow[i]->displaymode = 16;
		ui->s_rpm_fast[i]->displaymode = 16;
		ui->s_accel[i]->displaymode = 16;
		ui->s_decel[i]->displaymode = 16;

		ui->lever[i] = robtk_lever_new (0, 2, 1, 20, 50, false);
		robtk_lever_add_mark (ui->lever[i], 2, "Tremolo");
		robtk_lever_add_mark (ui->lever[i], 1, "Off");
		robtk_lever_add_mark (ui->lever[i], 0, "Chorale");

		robtk_dial_set_scroll_mult (ui->s_level[i], 5.f);
		robtk_dial_set_scroll_mult (ui->s_brakepos[i], 5.f);

		robtk_dial_enable_states (ui->s_brakepos[i], 1);
		ui->s_brakepos[i]->threesixty = true;
		ui->s_brakepos[i]->displaymode = 3;

		robtk_dial_set_constained (ui->s_rpm_slow[i], false);
		robtk_dial_set_constained (ui->s_rpm_fast[i], false);
		robtk_dial_set_constained (ui->s_accel[i], false);
		robtk_dial_set_constained (ui->s_decel[i], false);

		robtk_dial_set_default (ui->s_level[i], 0);
		robtk_dial_set_default (ui->s_radius[i], radius[i].dflt);
		robtk_dial_set_value (ui->s_brakepos[i], .25);
		robtk_dial_set_default (ui->s_brakepos[i], .25);
		robtk_dial_set_default_state (ui->s_brakepos[i], 0.0);
		robtk_dial_set_default (ui->s_rpm_slow[i], param_to_dial (&rpm_slow[i], rpm_slow[i].dflt));
		robtk_dial_set_default (ui->s_rpm_fast[i], param_to_dial (&rpm_fast[i], rpm_fast[i].dflt));
		robtk_dial_set_default (ui->s_accel[i], param_to_dial (&acceleration[i], acceleration[i].dflt));
		robtk_dial_set_default (ui->s_decel[i], param_to_dial (&deceleration[i], deceleration[i].dflt));

		robtk_dial_annotation_callback (ui->s_level[i], dial_annotation, NULL);
		robtk_dial_annotation_callback (ui->s_brakepos[i], dial_annotation_brake, ui);
		robtk_dial_annotation_callback (ui->s_radius[i], dial_annotation, (void*)&radius[i]);
		robtk_dial_annotation_callback (ui->s_rpm_slow[i], dial_annotation, (void*)&rpm_slow[i]);
		robtk_dial_annotation_callback (ui->s_rpm_fast[i], dial_annotation, (void*)&rpm_fast[i]);
		robtk_dial_annotation_callback (ui->s_accel[i], dial_annotation, (void*)&acceleration[i]);
		robtk_dial_annotation_callback (ui->s_decel[i], dial_annotation, (void*)&deceleration[i]);

		robtk_dial_set_surface (ui->s_level[i], ui->dial_bg[0]);
		robtk_dial_set_surface (ui->s_rpm_slow[i], ui->dial_bg[6 + i]);
		robtk_dial_set_surface (ui->s_rpm_fast[i], ui->dial_bg[8 + i]);
		robtk_dial_set_surface (ui->s_accel[i], ui->dial_bg[10 + i]);
		robtk_dial_set_surface (ui->s_decel[i], ui->dial_bg[12 + i]);
		robtk_dial_set_surface (ui->s_brakepos[i], ui->dial_bg[15]);
		// TODO set radius surface + add label

		static const float bpos[4] = {.25, .5, .75, 1.0};
		robtk_dial_set_detents (ui->s_brakepos[i], 4, bpos);

		ui->lbl_rpm[i]  = robtk_lbl_new ("????.? RPM");

		ui->lbl_mtr[i][1]  = robtk_lbl_new ("RPM slow:");
		ui->lbl_mtr[i][2]  = robtk_lbl_new ("RPM fast:");
		ui->lbl_mtr[i][3]  = robtk_lbl_new ("Acceleration:");
		ui->lbl_mtr[i][4]  = robtk_lbl_new ("Deceleration:");

		LABEL_BACKGROUND(lbl_mtr[i][0], 0, 0, 0, 0);
		LABEL_BACKGROUND(lbl_rpm[i], .21, .21, .27, 1.0); // match tblbox_expose_event

		for (int j = 1; j < 5; ++j) {
			robtk_lbl_set_alignment (ui->lbl_mtr[i][j], 1.0, 0.5);
			LABEL_BACKGROUND(lbl_mtr[i][j], 0, 0, 0, 0);
		}

		if (i == 0) {
			rob_vbox_child_pack (ui->box_mix[i], SP_W(ui->sep_mix[2*i+1]), TRUE, TRUE);
		} else {
			rob_vbox_child_pack (ui->box_mix[i], SP_W(ui->sep_mix[2*i]), FALSE, FALSE);
		}
		rob_vbox_child_pack (ui->box_mix[i], DL_W(ui->s_level[i]), FALSE, FALSE);
		rob_vbox_child_pack (ui->box_mix[i], LB_W(ui->lbl_mix[i]), FALSE, FALSE);
		if (i == 0) {
			rob_vbox_child_pack (ui->box_mix[i], SP_W(ui->sep_mix[2*i]), FALSE, FALSE);
		} else {
			rob_vbox_child_pack (ui->box_mix[i], SP_W(ui->sep_mix[2*i+1]), TRUE, TRUE);
		}

		ui->sep_tbl[i] = robtk_sep_new (TRUE);
		robwidget_set_expose_event (ui->sep_tbl[i]->rw, noop_expose_event);

		robtk_lbl_set_alignment (ui->lbl_brk[i], 1.0, .55);
		rob_hbox_child_pack (ui->box_brk[i], LB_W(ui->lbl_brk[i]), FALSE, FALSE);
		rob_hbox_child_pack (ui->box_brk[i], DL_W(ui->s_brakepos[i]), FALSE, FALSE);

		rob_table_attach (ui->tbl_mtr[i], LB_W(ui->lbl_mtr[i][0]), 0, 2, 0, 1,  2, 8, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], LB_W(ui->lbl_rpm[i]),    0, 2, 1, 2,  2, 2, RTK_EXANDF, RTK_EXANDF);
		rob_table_attach (ui->tbl_mtr[i], LB_W(ui->lbl_mtr[i][1]), 0, 1, 2, 3,  2, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], LB_W(ui->lbl_mtr[i][2]), 0, 1, 3, 4,  2, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], LB_W(ui->lbl_mtr[i][3]), 0, 1, 4, 5,  2, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], LB_W(ui->lbl_mtr[i][4]), 0, 1, 5, 6,  2, 0, RTK_EXANDF, RTK_SHRINK);

		rob_table_attach (ui->tbl_mtr[i], DL_W(ui->s_rpm_slow[i]), 1, 2, 2, 3,  4, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], DL_W(ui->s_rpm_fast[i]), 1, 2, 3, 4,  4, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], DL_W(ui->s_accel[i]),    1, 2, 4, 5,  4, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], DL_W(ui->s_decel[i]),    1, 2, 5, 6,  4, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], SP_W(ui->sep_tbl[i]),    0, 2, 6, 7,  0, 0, RTK_EXANDF, RTK_SHRINK);
	}

	ui->tbl_adv[0] = rob_table_new (/*rows*/ 4, /*cols*/ 2, FALSE);
	ui->tbl_adv[1] = rob_table_new (/*rows*/ 6, /*cols*/ 2, FALSE);
	robwidget_set_expose_event (ui->tbl_adv[0], rcontainer_expose_event_no_clear);
	robwidget_set_expose_event (ui->tbl_adv[1], rcontainer_expose_event_no_clear);

	ui->btn_adv = robtk_cbtn_new ("Unlock", GBT_LED_LEFT, false);

	ui->lbl_adv[0] = robtk_lbl_new ("<markup><b>Advanced</b>\nChanging these\nparameters\nre-reinitializes\nthe DSP.\nAudio will fade\nduring reset.</markup>");
	ui->lbl_adv[1] = robtk_lbl_new ("Mic \nDistance:");
	ui->lbl_adv[2] = robtk_lbl_new ("Horn\nMic Angle:");
	ui->lbl_adv[3] = robtk_lbl_new ("Horn\nRadius:");
	ui->lbl_adv[4] = robtk_lbl_new ("Drum\nRadius:");
	ui->lbl_adv[5] = robtk_lbl_new ("Horn\nPos L/R:");
	ui->lbl_adv[6] = robtk_lbl_new ("Horn\nPos F/B:");

	ui->s_xzmpos[0]->displaymode = 31;
	ui->s_xzmpos[1]->displaymode = 31;
	ui->s_xzmpos[2]->displaymode = 20;
	ui->s_xzmpos[3]->displaymode = 20;
	ui->s_radius[0]->displaymode = 20;
	ui->s_radius[1]->displaymode = 20;

	robtk_dial_set_alignment (ui->s_xzmpos[0], 1.0, .5);
	robtk_dial_set_alignment (ui->s_xzmpos[1], 1.0, .5);
	robtk_dial_set_alignment (ui->s_xzmpos[2], 1.0, .5);
	robtk_dial_set_alignment (ui->s_xzmpos[3], 1.0, .5);
	robtk_dial_set_alignment (ui->s_radius[0], 1.0, .5);
	robtk_dial_set_alignment (ui->s_radius[1], 1.0, .5);

	static const float mang[3] = {45, 90, 135};
	robtk_dial_set_detents (ui->s_xzmpos[3], 3, mang);
	robtk_dial_set_detent_default (ui->s_xzmpos[0], true);
	robtk_dial_set_detent_default (ui->s_xzmpos[1], true);
	robtk_dial_set_detent_default (ui->s_xzmpos[2], true);
	robtk_dial_set_detent_default (ui->s_radius[0], true);
	robtk_dial_set_detent_default (ui->s_radius[1], true);

	for (int j = 0; j < 7; ++j) {
		robtk_lbl_set_alignment (ui->lbl_adv[j], 1, .5);
		LABEL_BACKGROUND(lbl_adv[j], 0, 0, 0, 0);
	}
	robtk_lbl_set_alignment (ui->lbl_adv[0], .5, 0);

	for (int j = 0; j < 3; ++j) {
		ui->sep_adv[j] = robtk_sep_new (FALSE);
		robwidget_set_expose_event (ui->sep_adv[j]->rw, noop_expose_event);
	}

	rob_table_attach (ui->tbl_adv[0], LB_W(ui->lbl_adv[0]),  0, 2, 0, 1,  2, 6, RTK_EXANDF, RTK_EXANDF);
	rob_table_attach (ui->tbl_adv[0], LB_W(ui->lbl_adv[1]),  0, 1, 1, 2,  2, 2, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->tbl_adv[0], LB_W(ui->lbl_adv[2]),  0, 1, 2, 3,  2, 2, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->tbl_adv[0], SP_W(ui->sep_adv[0]),  0, 1, 3, 4,  2, 2, RTK_EXANDF, RTK_SHRINK);

	rob_table_attach (ui->tbl_adv[0], DL_W(ui->s_xzmpos[2]), 1, 2, 1, 2,  0, 2, RTK_SHRINK, RTK_SHRINK);
	rob_table_attach (ui->tbl_adv[0], DL_W(ui->s_xzmpos[3]), 1, 2, 2, 3,  0, 2, RTK_SHRINK, RTK_SHRINK);

	rob_table_attach (ui->tbl_adv[1], SP_W(ui->sep_adv[1]),  0, 1, 0, 1,  2, 2, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->tbl_adv[1], LB_W(ui->lbl_adv[3]),  0, 1, 1, 2,  2, 2, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->tbl_adv[1], LB_W(ui->lbl_adv[4]),  0, 1, 2, 3,  2, 2, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->tbl_adv[1], LB_W(ui->lbl_adv[5]),  0, 1, 3, 4,  2, 2, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->tbl_adv[1], LB_W(ui->lbl_adv[6]),  0, 1, 4, 5,  2, 2, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->tbl_adv[1], SP_W(ui->sep_adv[2]),  0, 1, 5, 6,  2, 2, RTK_EXANDF, RTK_EXANDF);

	rob_table_attach (ui->tbl_adv[1], DL_W(ui->s_radius[0]), 1, 2, 1, 2,  0, 2, RTK_SHRINK, RTK_SHRINK);
	rob_table_attach (ui->tbl_adv[1], DL_W(ui->s_radius[1]), 1, 2, 2, 3,  0, 2, RTK_SHRINK, RTK_SHRINK);
	rob_table_attach (ui->tbl_adv[1], DL_W(ui->s_xzmpos[0]), 1, 2, 3, 4,  0, 2, RTK_SHRINK, RTK_SHRINK);
	rob_table_attach (ui->tbl_adv[1], DL_W(ui->s_xzmpos[1]), 1, 2, 4, 5,  0, 2, RTK_SHRINK, RTK_SHRINK);

	ui->spk_dpy[0] = robwidget_new (ui);
	robwidget_set_alignment (ui->spk_dpy[0], .5, .5);
	robwidget_set_size_allocate (ui->spk_dpy[0], m1_size_allocate);
	robwidget_set_size_request (ui->spk_dpy[0], horn_size_request);
	robwidget_set_expose_event (ui->spk_dpy[0], horn_expose_event);

	ui->spk_dpy[1] = robwidget_new (ui);
	robwidget_set_alignment (ui->spk_dpy[1], .5, .5);
	robwidget_set_size_allocate (ui->spk_dpy[1], m1_size_allocate);
	robwidget_set_size_request (ui->spk_dpy[1], drum_size_request);
	robwidget_set_expose_event (ui->spk_dpy[1], drum_expose_event);

	/* callbacks */
	robtk_dial_set_callback (ui->s_drumwidth, cb_dial_dwidth, ui);
	robtk_dial_set_callback (ui->s_hornwidth, cb_dial_hwidth, ui);
	robtk_dial_set_callback (ui->s_leak, cb_dial_leak, ui);
	robtk_cbtn_set_callback (ui->btn_link, cb_linked, ui);

	robtk_cbtn_set_callback (ui->btn_adv, cb_adv_en, ui);

	robtk_dial_set_callback (ui->s_level[0], cb_dial_lvl0, ui);
	robtk_dial_set_callback (ui->s_brakepos[0], cb_dial_brk0, ui);
	robtk_dial_set_callback (ui->s_radius[0], cb_dial_hrad, ui);
	robtk_dial_set_callback (ui->s_rpm_slow[0], cb_dial_rpms0, ui);
	robtk_dial_set_callback (ui->s_rpm_fast[0], cb_dial_rpmf0, ui);
	robtk_dial_set_callback (ui->s_accel[0], cb_dial_acc0, ui);
	robtk_dial_set_callback (ui->s_decel[0], cb_dial_dec0, ui);

	robtk_dial_set_callback (ui->s_level[1], cb_dial_lvl1, ui);
	robtk_dial_set_callback (ui->s_brakepos[1], cb_dial_brk1, ui);
	robtk_dial_set_callback (ui->s_radius[1], cb_dial_drad, ui);
	robtk_dial_set_callback (ui->s_rpm_slow[1], cb_dial_rpms1, ui);
	robtk_dial_set_callback (ui->s_rpm_fast[1], cb_dial_rpmf1, ui);
	robtk_dial_set_callback (ui->s_accel[1], cb_dial_acc1, ui);
	robtk_dial_set_callback (ui->s_decel[1], cb_dial_dec1, ui);

	robtk_select_set_callback (ui->sel_fil[0], cb_sel_fil0, ui);
	robtk_dial_set_callback (ui->s_ffreq[0], cb_dial_freq0, ui);
	robtk_dial_set_callback (ui->s_fqual[0], cb_dial_qual0, ui);
	robtk_dial_set_callback (ui->s_fgain[0], cb_dial_gain0, ui);

	robtk_select_set_callback (ui->sel_fil[1], cb_sel_fil1, ui);
	robtk_dial_set_callback (ui->s_ffreq[1], cb_dial_freq1, ui);
	robtk_dial_set_callback (ui->s_fqual[1], cb_dial_qual1, ui);
	robtk_dial_set_callback (ui->s_fgain[1], cb_dial_gain1, ui);

	robtk_select_set_callback (ui->sel_fil[2], cb_sel_fil2, ui);
	robtk_dial_set_callback (ui->s_ffreq[2], cb_dial_freq2, ui);
	robtk_dial_set_callback (ui->s_fqual[2], cb_dial_qual2, ui);
	robtk_dial_set_callback (ui->s_fgain[2], cb_dial_gain2, ui);

	robtk_lever_set_callback (ui->lever[0], cb_leverH, ui);
	robtk_lever_set_callback (ui->lever[1], cb_leverD, ui);

	robtk_dial_set_callback (ui->s_xzmpos[0], cb_dial_hornx, ui);
	robtk_dial_set_callback (ui->s_xzmpos[1], cb_dial_hornz, ui);
	robtk_dial_set_callback (ui->s_xzmpos[2], cb_dial_micd, ui);
	robtk_dial_set_callback (ui->s_xzmpos[3], cb_dial_mica, ui);

	for (int i = 0; i < 3; ++i) {
		ui->sep_v[i] = robtk_sep_new (FALSE);
		ui->sep_h[i] = robtk_sep_new (TRUE);
		ui->sep_h[i]->m_height = 14;
		robwidget_set_expose_event (ui->sep_v[i]->rw, noop_expose_event);
		robwidget_set_expose_event (ui->sep_h[i]->rw, noop_expose_event);
	}
	ui->sep_v[0]->m_width = 19; // left side, input arrows
	ui->sep_v[1]->m_width = 24; // arrows from filters to speaker
	ui->sep_v[2]->m_width = 14; // nada, right-side rounded rect only

	robwidget_set_expose_event (ui->sep_drmmic->rw, noop_expose_event);
	robwidget_set_expose_event (ui->box_drmmic, rcontainer_expose_event_no_clear);
	robwidget_set_expose_event (ui->sep_hrnmic->rw, noop_expose_event);
	robwidget_set_expose_event (ui->box_hrnmic, rcontainer_expose_event_no_clear);

	robwidget_set_expose_event (ui->sep_leak->rw, noop_expose_event);
	robwidget_set_expose_event (ui->box_leak, rcontainer_expose_event_no_clear);

	/* top-level packing */
	rob_table_attach (ui->rw, SP_W(ui->sep_v[0]),   0,  1,  1,  8,  0, 0, RTK_SHRINK, RTK_EXANDF);
	rob_table_attach (ui->rw, SP_W(ui->sep_v[1]),   2,  3,  1,  8,  0, 0, RTK_SHRINK, RTK_EXANDF);
	rob_table_attach (ui->rw, SP_W(ui->sep_v[2]),   7,  8,  1,  8,  0, 0, RTK_SHRINK, RTK_EXANDF);

	rob_table_attach (ui->rw, SP_W(ui->sep_h[0]),   3,  6,  0,  1,  0, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->rw, SP_W(ui->sep_h[1]),   8,  9,  4,  5,  0, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->rw, SP_W(ui->sep_h[2]),   7,  8,  8,  9,  0, 0, RTK_EXANDF, RTK_SHRINK);

	rob_table_attach (ui->rw, ui->tbl_flt[1],       1,  2,  1,  4,  4, 2, RTK_EXANDF, RTK_EXANDF);
	rob_table_attach (ui->rw, ui->tbl_flt[2],       1,  2,  5,  8,  4, 2, RTK_EXANDF, RTK_EXANDF);

	rob_table_attach (ui->rw, ui->tbl_flt[0],       3,  4,  1,  4,  4, 2, RTK_EXANDF, RTK_EXANDF);

	rob_table_attach (ui->rw, ui->spk_dpy[0],       3,  4,  4,  6,  0, 0, RTK_EXANDF, RTK_EXANDF);
	rob_table_attach (ui->rw, ui->spk_dpy[1],       3,  4,  6,  8,  0, 0, RTK_EXANDF, RTK_EXANDF);

	rob_table_attach (ui->rw, ui->tbl_mtr[0],       4,  5,  1,  4,  2, 2, RTK_FILL,   RTK_EXANDF);
	rob_table_attach (ui->rw, ui->tbl_mtr[1],       4,  5,  5,  8,  2, 2, RTK_FILL,   RTK_EXANDF);

	rob_table_attach (ui->rw, ui->box_brk[0],       5,  7,  1,  2,  0, 3, RTK_SHRINK, RTK_SHRINK);
	rob_table_attach (ui->rw, LV_W(ui->lever[0]),   5,  6,  2,  4,  0, 16,RTK_SHRINK, RTK_EXANDF);
	rob_table_attach (ui->rw, CB_W(ui->btn_link),   5,  6,  4,  5,  0, 0, RTK_FILL,   RTK_SHRINK);
	rob_table_attach (ui->rw, LV_W(ui->lever[1]),   5,  6,  5,  7,  0, 16,RTK_SHRINK, RTK_EXANDF);
	rob_table_attach (ui->rw, ui->box_brk[1],       5,  7,  7,  8,  0, 3, RTK_SHRINK, RTK_SHRINK);

	rob_table_attach (ui->rw, ui->tbl_adv[0],       6,  7,  2,  4,  6, 0, RTK_EXANDF, RTK_EXANDF);
	rob_table_attach (ui->rw, CB_W(ui->btn_adv),    6,  7,  4,  5,  6, 0, RTK_FILL,   RTK_SHRINK);
	rob_table_attach (ui->rw, ui->tbl_adv[1],       6,  7,  5,  7,  6, 0, RTK_EXANDF, RTK_EXANDF);

	rob_table_attach (ui->rw, ui->box_leak,         8,  9,  1,  2,  4, 0, RTK_FILL,   RTK_EXANDF);
	rob_table_attach (ui->rw, ui->box_hrnmic,       8,  9,  2,  3,  4, 0, RTK_FILL,   RTK_EXANDF);
	rob_table_attach (ui->rw, ui->box_mix[0],       8,  9,  3,  4,  4, 0, RTK_FILL,   RTK_EXANDF);
	rob_table_attach (ui->rw, ui->box_mix[1],       8,  9,  5,  6,  4, 0, RTK_FILL,   RTK_EXANDF);
	rob_table_attach (ui->rw, ui->box_drmmic,       8,  9,  6,  7,  4, 0, RTK_FILL,   RTK_EXANDF);

	// override expose and allocate for custom background
	ui->rw->size_allocate = bg_size_allocate;
	ui->rw->expose_event = bg_expose_event;

	for (int i = 0; i < 3; ++i) {
		update_eq (ui, i);
	}

	cb_adv_en (NULL, ui);

	return ui->rw;
}

static void gui_cleanup (WhirlUI* ui) {
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 5; ++j) {
			robtk_lbl_destroy (ui->lbl_flt[i][j]);
		}
		robtk_select_destroy (ui->sel_fil[i]);
		robtk_dial_destroy (ui->s_ffreq[i]);
		robtk_dial_destroy (ui->s_fqual[i]);
		robtk_dial_destroy (ui->s_fgain[i]);
		rob_table_destroy (ui->tbl_flt[i]);
		if (ui->fil_sf[i]) {
			cairo_surface_destroy (ui->fil_sf[i]);
		}
	}
	for (int i = 0; i < 4; ++i) {
		robtk_dial_destroy (ui->s_xzmpos[i]);
		robtk_sep_destroy (ui->sep_mix[i]);
	}
	for (int i = 0; i < 2; ++i) {
		for (int j = 0; j < 5; ++j) {
			robtk_lbl_destroy (ui->lbl_mtr[i][j]);
		}
		robtk_lbl_destroy (ui->lbl_rpm[i]);
		robtk_lbl_destroy (ui->lbl_mix[i]);
		robtk_lbl_destroy (ui->lbl_brk[i]);
		robtk_dial_destroy (ui->s_level[i]);
		robtk_dial_destroy (ui->s_brakepos[i]);
		robtk_dial_destroy (ui->s_radius[i]);
		robtk_dial_destroy (ui->s_rpm_slow[i]);
		robtk_dial_destroy (ui->s_rpm_fast[i]);
		robtk_dial_destroy (ui->s_accel[i]);
		robtk_dial_destroy (ui->s_decel[i]);
		robtk_lever_destroy (ui->lever[i]);
		robtk_sep_destroy (ui->sep_tbl[i]);

		rob_table_destroy (ui->tbl_mtr[i]);
		rob_box_destroy (ui->box_mix[i]);
		rob_box_destroy (ui->box_brk[i]);
	}

	for (int i = 0; i < 3; ++i) {
		robtk_sep_destroy (ui->sep_v[i]);
		robtk_sep_destroy (ui->sep_h[i]);
		robtk_sep_destroy (ui->sep_adv[i]);
	}

	for (int i = 0; i < 6; ++i) {
		robtk_lbl_destroy (ui->lbl_adv[i]);
	}

	robtk_cbtn_destroy (ui->btn_adv);
	rob_table_destroy (ui->tbl_adv[0]);
	rob_table_destroy (ui->tbl_adv[1]);

	robtk_lbl_destroy (ui->lbl_leak);
	robtk_dial_destroy (ui->s_leak);
	robtk_sep_destroy (ui->sep_leak);
	rob_box_destroy (ui->box_leak);

	robtk_sep_destroy (ui->sep_hrnmic);
	robtk_sep_destroy (ui->sep_drmmic);
	robtk_lbl_destroy (ui->lbl_drumwidth);
	robtk_lbl_destroy (ui->lbl_hornwidth);
	robtk_dial_destroy (ui->s_drumwidth);
	robtk_dial_destroy (ui->s_hornwidth);
	rob_box_destroy (ui->box_hrnmic);
	rob_box_destroy (ui->box_drmmic);

	robtk_cbtn_destroy (ui->btn_link);

	for (int i = 0; i < 4; ++i) {
		if (ui->hornp[i]) { cairo_pattern_destroy (ui->hornp[i]); ui->hornp[i] = NULL; }
	}

	pango_font_description_free (ui->font[0]);
	pango_font_description_free (ui->font[1]);

	for (int i = 0; i < 17; ++i) {
		cairo_surface_destroy (ui->dial_bg[i]);
	}
	cairo_surface_destroy (ui->gui_bg);

	rob_table_destroy (ui->rw);
}


/******************************************************************************
 * RobTk + LV2
 */

#define LVGL_RESIZEABLE

static void ui_enable (LV2UI_Handle handle) {
	WhirlUI* ui = (WhirlUI*)handle;
	float val = time (NULL) % rand ();
	ui->write (ui->controller, B3W_GUINOTIFY, sizeof (float), 0, (const void*) &val);
}

static void ui_disable (LV2UI_Handle handle) {
}


static LV2UI_Handle
instantiate (
		void* const               ui_toplevel,
		const LV2UI_Descriptor*   descriptor,
		const char*               plugin_uri,
		const char*               bundle_path,
		LV2UI_Write_Function      write_function,
		LV2UI_Controller          controller,
		RobWidget**               widget,
		const LV2_Feature* const* features)
{
	if (strcmp (plugin_uri, RTK_URI "extended")) {
		return NULL;
	}

	WhirlUI* ui = (WhirlUI*) calloc (1, sizeof (WhirlUI));
	if (!ui) { return NULL; }

	ui->nfo = robtk_info (ui_toplevel);
	ui->write      = write_function;
	ui->controller = controller;
	ui->initialized = 0;
	ui->last_used_horn_lever = true;
	ui->set_last_used = true;
	ui->eq_dragging = -1;
	ui->eq_hover = -1;

	for (int i = 0; i < 2; ++i) {
		ui->cur_rpm[i] = -1;
		ui->cur_ang[i] = -1;
	}

	*widget = toplevel (ui, ui_toplevel);

	return ui;
}

static enum LVGLResize
plugin_scale_mode (LV2UI_Handle handle)
{
	return LVGL_LAYOUT_TO_FIT;
}

static void
cleanup (LV2UI_Handle handle)
{
	WhirlUI* ui = (WhirlUI*)handle;
	gui_cleanup (ui);
	free (ui);
}

/* receive information from DSP */
static void
port_event (LV2UI_Handle handle,
		uint32_t     port_index,
		uint32_t     buffer_size,
		uint32_t     format,
		const void*  buffer)
{
	WhirlUI* ui = (WhirlUI*)handle;
	if (format != 0 || port_index < B3W_REVSELECT) return;

	if (ui->initialized != 15 && port_index != B3W_GUINOTIFY) {
		ui_enable (ui);
	}

	const float v = *(float *)buffer;
	ui->disable_signals = true;
	switch (port_index) {
		case B3W_REVSELECT:
			update_levers (ui, v);
			break;
		case B3W_HORNLVL:
			robtk_dial_set_value (ui->s_level[0], v);
			break;
		case B3W_DRUMLVL:
			robtk_dial_set_value (ui->s_level[1], v);
			break;
		case B3W_DRUMWIDTH:
			robtk_dial_set_value (ui->s_drumwidth, v);
			break;
		case B3W_HORNWIDTH:
			robtk_dial_set_value (ui->s_hornwidth, v);
			break;
		case B3W_HORNRPMSLOW:
			robtk_dial_set_value (ui->s_rpm_slow[0], param_to_dial (&rpm_slow[0], v));
			break;
		case B3W_HORNRPMFAST:
			robtk_dial_set_value (ui->s_rpm_fast[0], param_to_dial (&rpm_fast[0], v));
			break;
		case B3W_HORNACCEL:
			robtk_dial_set_value (ui->s_accel[0], param_to_dial (&acceleration[0], v));
			break;
		case B3W_HORNDECEL:
			robtk_dial_set_value (ui->s_decel[0], param_to_dial (&deceleration[0], v));
			break;
		case B3W_HORNBRAKE:
			if (v > 0) {
				robtk_dial_set_value (ui->s_brakepos[0], v);
				robtk_dial_set_state (ui->s_brakepos[0], 1);
			} else {
				robtk_dial_set_state (ui->s_brakepos[0], 0);
			}
			break;
		case B3W_DRUMRPMSLOW:
			robtk_dial_set_value (ui->s_rpm_slow[1], param_to_dial (&rpm_slow[1], v));
			break;
		case B3W_DRUMRPMFAST:
			robtk_dial_set_value (ui->s_rpm_fast[1], param_to_dial (&rpm_fast[1], v));
			break;
		case B3W_DRUMACCEL:
			robtk_dial_set_value (ui->s_accel[1], param_to_dial (&acceleration[1], v));
			break;
		case B3W_DRUMDECEL:
			robtk_dial_set_value (ui->s_decel[1], param_to_dial (&deceleration[1], v));
			break;
		case B3W_DRUMBRAKE:
			if (v > 0) {
				robtk_dial_set_value (ui->s_brakepos[1], v);
				robtk_dial_set_state (ui->s_brakepos[1], 1);
			} else {
				robtk_dial_set_state (ui->s_brakepos[1], 0);
			}
			break;
		case B3W_FILTATYPE:
			robtk_select_set_value (ui->sel_fil[0], v);
			break;
		case B3W_FILTBTYPE:
			robtk_select_set_value (ui->sel_fil[1], v);
			break;
		case B3W_FILTDTYPE:
			robtk_select_set_value (ui->sel_fil[2], v);
			break;
		case B3W_FILTAFREQ:
			robtk_dial_set_value (ui->s_ffreq[0], param_to_dial (&filter[0][0], v));
			break;
		case B3W_FILTBFREQ:
			robtk_dial_set_value (ui->s_ffreq[1], param_to_dial (&filter[1][0], v));
			break;
		case B3W_FILTDFREQ:
			robtk_dial_set_value (ui->s_ffreq[2], param_to_dial (&filter[2][0], v));
			break;
		case B3W_FILTAQUAL:
			robtk_dial_set_value (ui->s_fqual[0], param_to_dial (&filter[0][1], v));
			break;
		case B3W_FILTBQUAL:
			robtk_dial_set_value (ui->s_fqual[1], param_to_dial (&filter[1][1], v));
			break;
		case B3W_FILTDQUAL:
			robtk_dial_set_value (ui->s_fqual[2], param_to_dial (&filter[2][1], v));
			break;
		case B3W_FILTAGAIN:
			robtk_dial_set_value (ui->s_fgain[0], v);
			break;
		case B3W_FILTBGAIN:
			robtk_dial_set_value (ui->s_fgain[1], v);
			break;
		case B3W_FILTDGAIN:
			robtk_dial_set_value (ui->s_fgain[2], v);
			break;
		case B3W_HORNLEAK:
			robtk_dial_set_value (ui->s_leak, v);
			break;
		case B3W_HORNRADIUS:
			robtk_dial_set_value (ui->s_radius[0], v);
			break;
		case B3W_DRUMRADIUS:
			robtk_dial_set_value (ui->s_radius[1], v);
			break;
		case B3W_HORNOFFX:
			robtk_dial_set_value (ui->s_xzmpos[0], v);
			break;
		case B3W_HORNOFFZ:
			robtk_dial_set_value (ui->s_xzmpos[1], v);
			break;
		case B3W_MICDIST:
			robtk_dial_set_value (ui->s_xzmpos[2], v);
			break;
		case B3W_HORNRPM:
			update_rpm (ui, 0, v);
			break;
		case B3W_DRUMRPM:
			update_rpm (ui, 1, v);
			break;
		case B3W_HORNANG:
			update_ang (ui, 0, v);
			break;
		case B3W_DRUMANG:
			update_ang (ui, 1, v);
			break;
		case B3W_LINKSPEED:
			if (v <= -.5) { ui->last_used_horn_lever = false; }
			if (v >= 0.5) { ui->last_used_horn_lever = true; }
			robtk_cbtn_set_active (ui->btn_link, fabsf(v) >= .5);
			break;
		case B3W_MICANGLE:
			robtk_dial_set_value (ui->s_xzmpos[3], v);
			break;
		default:
			break;
	}
	ui->disable_signals = false;
}


static const void*
extension_data (const char* uri)
{
	return NULL;
}
