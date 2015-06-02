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

#include "eqcomp.c"

#define MTR_URI "http://gareus.org/oss/lv2/b_whirl#"
#define MTR_GUI "ui"

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
	{ 10.0, 200.0, 40.32, 20, S_("%.0f RPM")}, // horn chorale
	{  5.0, 100.0, 36.00, 20, S_("%.0f RPM")}, // baffle chorale
};

static const Parameter rpm_fast[2] = {
	{ 100.0, 1000.0, 423.36, 10, S_("%.0f RPM")}, // horn tremolo
	{  60.0,  600.0, 357.30, 10, S_("%.0f RPM")}, // baffle tremolo
};

static const Parameter acceleration[2] = {
	{ 0.001, 10.0, 0.161, 1000, S_("%.2f s")}, // horn accel [s]
	{  0.01, 20.0, 4.127,  200, S_("%.2f s")}, // baffle accel [s]
};

static const Parameter deceleration[2] = {
	{ 0.001, 10.0, 0.321, 1000, S_("%.2f s")}, // horn
	{  0.01, 20.0, 1.371,  200, S_("%.2f s")}, // baffle
};

static const Parameter filter[3][3] = {
	{ // horn char -- low pass
		{ 250.00, 8000.0, 4500.0, 32, S_("%.0f Hz")}, // freq
		{   0.01,    6.0, 2.7456, 60, S_("%.2f")}, // q
		{ -48.,   48.0, -38.9291,  0, NULL}, // level

	},
	{ // horn split -- low shelf
		{ 250.0, 8000.0,  300.0, 32, S_("%.0f Hz")}, // freq
		{ 0.01,    6.0,     1.0, 60, S_("%.2f")}, // q
		{ -48.,   48.0,   -30.0,  0, NULL}, // level

	},
	{ // drum split -- high-shelf
		{  50.00, 8000.0, 811.9695, 160, S_("%.0f Hz")}, // freq
		{   0.01,    6.0,   1.6016,  60, S_("%.2f")}, // q
		{ -48.,     48.0, -38.9291,   0, NULL}, // level

	}
};

static float dial_to_param (const Parameter *d, float val) {
	return d->min + (d->max - d->min) * (pow((1. + d->warp), val) - 1.) / d->warp;
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

	// horn[0] / drum[1] - level/mix
	RobWidget *box_mix[2];
	RobTkLbl  *lbl_mix[2];
	RobTkDial *s_level[2];

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
	RobWidget   * fil_tf[3];
	cairo_surface_t* fil_sf[3];

	RobWidget   *box_drmmic;
	RobTkDial   *s_drumwidth;
	RobTkLbl    *lbl_drumwidth;

	RobWidget   *box_spdsel;
	RobTkSelect *sel_spd;

	RobTkSep    *sep_h[6];
	RobTkSep    *sep_v[6];

	PangoFontDescription *font[2];
	cairo_surface_t* dial_bg[15];
	cairo_surface_t* gui_bg;
	const char *nfo;
} WhirlUI;

static const float c_ann[4] = {0.5, 0.5, 0.5, 1.0}; // EQ annotation color
static const float c_dlf[4] = {0.8, 0.8, 0.8, 1.0}; // dial faceplate fg

/***  transfer function display ***/

#ifndef SQUARE
#define SQUARE(X) ( (X) * (X) )
#endif

typedef struct {
	float A, B, C, D, A1, B1;
	float rate;
} FilterSection;

static float get_eq_response (FilterSection *flt, const float freq) {
	const float w = 2.f * M_PI * freq / flt->rate;
	const float c1 = cosf(w);
	const float s1 = sinf(w);
	const float A = flt->A * c1 + flt->B1;
	const float B = flt->B * s1;
	const float C = flt->C * c1 + flt->A1;
	const float D = flt->D * s1;
	return 20.f * log10f (sqrtf ((SQUARE(A) + SQUARE(B)) * (SQUARE(C) + SQUARE(D))) / (SQUARE(C) + SQUARE(D)));
}

static float freq_at_x (const int x, const int m0_width) {
	return 20.f * powf (1000.f, x / (float) m0_width);
}

static float x_at_freq (const float f, const int m0_width) {
	return rintf(m0_width * logf (f / 20.0) / logf (1000.0));
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
		const float yy = rint (ym - yr * DB) + .5; \
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

	for (int i = 2; i < 9; ++i) {
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

	cairo_restore (cr);

	for (int i = 0; i < xw; ++i) {
		// TODO interpolate (don't miss peaks)
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


/***  value changed calllbacks ***/

#define SELECT_CALLBACK(name, widget, port, hook) \
static bool cb_sel_ ## name (RobWidget *w, void* handle) { \
	WhirlUI* ui = (WhirlUI*)handle; \
	hook; \
	if (ui->disable_signals) return TRUE; \
	const float val = robtk_select_get_value (ui->sel_ ## widget); \
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
DIAL_CALLBACK(brk0,   brakepos[0], B3W_HORNBRAKE, );
DIAL_CALLBACK(brk1,   brakepos[1], B3W_DRUMBRAKE, );
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

DIAL_CALLBACK(width,  drumwidth,   B3W_DRUMWIDTH, );

SELECT_CALLBACK(spd,  spd,         B3W_REVSELECT, );

SELECT_CALLBACK(fil0, fil[0],      B3W_FILTATYPE, update_eq (ui, 0));
SELECT_CALLBACK(fil1, fil[1],      B3W_FILTBTYPE, update_eq (ui, 1));
SELECT_CALLBACK(fil2, fil[2],      B3W_FILTDTYPE, update_eq (ui, 2));


/*** knob tooltips/annotations ***/

static void render_annotation (WhirlUI* ui, cairo_t *cr, const char *txt) {
	int tw, th;
	PangoLayout * pl = pango_cairo_create_layout (cr);
	pango_layout_set_font_description (pl, ui->font[0]);
	pango_layout_set_text (pl, txt, -1);
	pango_layout_get_pixel_size (pl, &tw, &th);
	cairo_translate (cr, -tw / 2.0 , -th);
	cairo_set_source_rgba (cr, .0, .0, .0, .5);
	rounded_rectangle (cr, -1, -1, tw+3, th+1, 3);
	cairo_fill (cr);
	CairoSetSouerceRGBA (c_wht);
	pango_cairo_layout_path (cr, pl);
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
			snprintf (txt, 24, p->fmt, p, d->cur);
		}
	} else {
		snprintf (txt, 24, "%+5.2f dB", d->cur);
	}
	cairo_save (cr);
	cairo_translate (cr, d->w_width / 2, d->w_height - 3);
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
	cairo_translate (cr, d->w_width / 2, d->w_height - 3);
	render_annotation (ui, cr, txt);
	cairo_restore (cr);
}

static void dial_annotation_brake (RobTkDial * d, cairo_t *cr, void *data) {
	WhirlUI* ui = (WhirlUI*) (data);
	char txt[24];
	if (d->cur == 0) {
		snprintf (txt, 24, "No Brake");
	} else if (d->cur == .25) {
		snprintf (txt, 24, "Left");
	} else if (d->cur ==  .5) {
		snprintf (txt, 24, "Back");
	} else if (d->cur == .75) {
		snprintf (txt, 24, "Right");
	} else if (d->cur == 1.0) {
		snprintf (txt, 24, "Front");
	} else {
		snprintf (txt, 24, "%.0f deg", d->cur * 360.);
	}

	cairo_save (cr);
	cairo_translate (cr, d->w_width / 2, d->w_height - 3);
	render_annotation (ui, cr, txt);
	cairo_restore (cr);
}

/*** knob faceplates ***/
static void prepare_faceplates (WhirlUI* ui) {
	cairo_t *cr;
	float xlp, ylp, ang;

#define INIT_DIAL_SF(VAR, W, H) \
	VAR = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, W, H); \
	cr = cairo_create (VAR); \
	CairoSetSouerceRGBA (c_trs); \
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE); \
	cairo_rectangle (cr, 0, 0, GED_WIDTH, GED_HEIGHT); \
	cairo_fill (cr); \
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER); \

#define DIALDOTS(V, XADD, YADD) \
	ang = (-.75 * M_PI) + (1.5 * M_PI) * (V); \
	xlp = GED_CX + XADD + sinf (ang) * (GED_RADIUS + 3.0); \
	ylp = GED_CY + YADD - cosf (ang) * (GED_RADIUS + 3.0); \
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND); \
	CairoSetSouerceRGBA (c_dlf); \
	cairo_set_line_width (cr, 2.5); \
	cairo_move_to (cr, rint (xlp)-.5, rint (ylp)-.5); \
	cairo_close_path (cr); \
	cairo_stroke (cr);

#define DIALLABLEL(V, TXT, ALIGN) \
	DIALDOTS(V, 6.5, 15.5) \
	xlp = GED_CX + 6.5 + sinf (ang) * (GED_RADIUS + 9.5); \
	ylp = GED_CY + 15.5 - cosf (ang) * (GED_RADIUS + 9.5); \
	write_text_full (cr, TXT, ui->font[0], xlp, ylp,  0, ALIGN, c_dlf); \

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
			const float cent = fmod (val / 100., 10.); \
			if (cent == 0) { \
				snprintf (tmp, 24, "%.0fK", floor (val / 1000.)); \
			} else { \
				snprintf (tmp, 24, "%.0fK%.0f", floor (val / 1000.), cent); \
			} \
		} else { \
			snprintf (tmp, 24, "%.0f", val); \
		} \
		DIALLABLEL(V, tmp, ALIGN); \
	}


	/* gain knob  -20,.+20*/
	INIT_DIAL_SF(ui->dial_bg[0], GED_WIDTH + 12, GED_HEIGHT + 20);
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

	/* generic*/
	INIT_DIAL_SF(ui->dial_bg[14], GED_WIDTH + 12, GED_HEIGHT + 20);
	{
	DIALDOTS(0.00, 6.5, 15.5)
	DIALDOTS(0.25, 6.5, 15.5)
	DIALDOTS(0.50, 6.5, 15.5)
	DIALDOTS(0.75, 6.5, 15.5)
	DIALDOTS(1.00, 6.5, 15.5)
	}
	cairo_destroy (cr);

}

/*** layout background ***/

static void bg_size_allocate (RobWidget* rw, int w, int h)
{
	WhirlUI *ui = (WhirlUI*)((GlMetersLV2UI*)rw->top)->ui;
	rtable_size_allocate (rw, w, h);
	if (ui->gui_bg) {
		cairo_surface_destroy (ui->gui_bg);
		ui->gui_bg = NULL;
	}
}

static float tbl_ym (struct rob_table *rt, int r0, int r1) {
	int y0 = 0;
	int y1 = 0;
	for (int i = 0; i < r1; ++i) {
		if (i < r0) {
			y0 += rt->rows[i].acq_h;
		}
		y1 += rt->rows[i].acq_h;
	}
	return (y1 + y0) * .5;
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
	return (x1 + x0) * .5;
}

static float tbl_y0 (struct rob_table *rt, int r0) {
	int y0 = 0;
	for (int i = 0; i < r0; ++i) {
		y0 += rt->rows[i].acq_h;
	}
	return y0;
}

static float tbl_x0 (struct rob_table *rt, int c0) {
	int x0 = 0;
	for (int i = 0; i < c0; ++i) {
		x0 += rt->cols[i].acq_w;
	}
	return x0;
}

static void draw_bg (WhirlUI *ui, const int w, const int h, struct rob_table *rt) {
	assert (!ui->gui_bg);
	ui->gui_bg = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create (ui->gui_bg);

	float c[4];
	get_color_from_theme (1, c);
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgb (cr, c[0], c[1], c[2]);
	cairo_rectangle (cr, 0, 0, w ,h);
	cairo_fill (cr);

	cairo_set_line_width (cr, 2.0);
	CairoSetSouerceRGBA (c_wht);

	float x0, x1, y0, y1;

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
	x1 = tbl_x0 (rt, 1) - 1; // border
	y1 = tbl_ym (rt, 5, 7);

	cairo_move_to (cr, x0, y0);
	cairo_curve_to (cr, x1, y0, x0, y1, x1, y1);
	cairo_stroke (cr);
	ARROW_LEFT;

	y1 = tbl_ym (rt, 2, 4);
	cairo_move_to (cr, x0, y0);
	cairo_curve_to (cr, x1, y0, x0, y1, x1, y1);
	cairo_stroke (cr);
	ARROW_LEFT;

	// filter to box
	x0 = tbl_x0 (rt, 2);
	x1 = tbl_xm (rt, 2, 3);
	y0 = y1 = tbl_ym (rt, 2, 4);
	cairo_move_to (cr, x0, y0);
	cairo_line_to (cr, x1, y1);
	cairo_stroke (cr);
	ARROW_LEFT;

	y0 = y1 = tbl_ym (rt, 5, 7);
	cairo_move_to (cr, x0, y0);
	cairo_line_to (cr, x1, y1);
	cairo_stroke (cr);
	ARROW_LEFT;

	// output baffle
	x0 = tbl_xm (rt, 6, 7);
	x1 = tbl_xm (rt, 7, 8);
	y0 = tbl_ym (rt, 7, 8);
	y1 = tbl_y0 (rt, 7);

	cairo_move_to (cr, x0, y0);
	cairo_curve_to (cr, x1, y0, x1, y0, x1, y1);
	cairo_stroke (cr);
	ARROW_UP;

	y0 = tbl_ym (rt, 6, 7);
	y1 = tbl_y0 (rt, 6);
	cairo_move_to (cr, x1, y0);
	cairo_line_to (cr, x1, y1);
	cairo_stroke (cr);
	ARROW_UP;

	y0 = tbl_ym (rt, 5, 6);
	y1 = tbl_y0 (rt, 5);
	cairo_move_to (cr, x1, y0);
	cairo_line_to (cr, x1, y1);
	cairo_stroke (cr);
	ARROW_UP;

	// output horn
	y0 = tbl_ym (rt, 2, 3);
	y1 = tbl_y0 (rt, 3);
	cairo_move_to (cr, x0, y0);
	cairo_curve_to (cr, x1, y0, x1, y0, x1, y1);
	cairo_stroke (cr);
	ARROW_DOWN;

	y0 = tbl_ym (rt, 3, 4);
	y1 = tbl_y0 (rt, 4);
	cairo_move_to (cr, x1, y0);
	cairo_line_to (cr, x1, y1);
	cairo_stroke (cr);
	ARROW_DOWN;

	// output
	y0 = tbl_y0 (rt, 4);
	y1 = tbl_y0 (rt, 5);
	cairo_move_to (cr, x1, y0);
	cairo_line_to (cr, x1, y1);
	cairo_stroke (cr);

	y0 = tbl_ym (rt, 4, 5);
	cairo_arc (cr, x1, y0, 8, 0., 2. * M_PI);
	cairo_stroke (cr);

	x0 = x1 - 8; y1 = y0;
	x1 = tbl_xm (rt, 8, 9);
	cairo_move_to (cr, x0, y0);
	cairo_line_to (cr, x1, y1);
	cairo_stroke (cr);
	ARROW_LEFT;

	// leslie box
	x0 = tbl_xm (rt, 2, 3);
	y0 = tbl_ym (rt, 0, 1);
	x1 = tbl_xm (rt, 6, 7);
	y1 = tbl_ym (rt, 8, 9);
	CairoSetSouerceRGBA (c_g20);
	rounded_rectangle (cr, x0, y0, (x1 - x0), (y1 - y0), 9);
	cairo_fill (cr);


	x0 = tbl_xm (rt, 3, 4);
	y0 = tbl_ym (rt, 3, 4);
	write_text_full (cr, "Draw a horn", ui->font[1], x0, y0,  0, 2, c_wht);

	x0 = tbl_xm (rt, 3, 4);
	y0 = tbl_ym (rt, 6, 7);
	write_text_full (cr, "Here be the drum", ui->font[1], x0, y0,  0, 2, c_wht);


	cairo_destroy (cr);
}

static bool bg_expose_event (RobWidget* rw, cairo_t* cr, cairo_rectangle_t *ev)
{
	WhirlUI *ui = (WhirlUI*)((GlMetersLV2UI*)rw->top)->ui;

	if (!ui->gui_bg) {
		draw_bg (ui, rw->area.width, rw->area.height, (struct rob_table*)rw->self);
	}

	cairo_save (cr);
	cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
	cairo_clip (cr);
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_set_source_surface (cr, ui->gui_bg, 0, 0);
	cairo_paint (cr);
	cairo_restore (cr);

	return rcontainer_expose_event_no_clear (rw, cr, ev);
}

static bool tblbox_expose_event (RobWidget* rw, cairo_t* cr, cairo_rectangle_t *ev)
{
	float c[4];
	get_color_from_theme (1, c);
	if (rw->resized) {
		cairo_rectangle_t event;
		event.x = MAX(0, ev->x - rw->area.x);
		event.y = MAX(0, ev->y - rw->area.y);
		event.width  = MIN(rw->area.x + rw->area.width , ev->x + ev->width)   - MAX(ev->x, rw->area.x);
		event.height = MIN(rw->area.y + rw->area.height, ev->y + ev->height) - MAX(ev->y, rw->area.y);
		cairo_save (cr);
		cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgb (cr, c[0], c[1], c[2]);
		cairo_rectangle (cr, event.x-1, event.y-1, event.width+2, event.height+2);
		cairo_fill_preserve (cr);
		CairoSetSouerceRGBA (c_g30);
		cairo_set_line_width (cr, 1.0);
		cairo_stroke (cr);
		cairo_restore (cr);
	} else {
		cairo_save (cr);
		cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
		cairo_clip (cr);
		cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgb (cr, c[0], c[1], c[2]);
		cairo_rectangle (cr, 0, 0, rw->area.width, rw->area.height);
		cairo_fill_preserve (cr);
		CairoSetSouerceRGBA (c_g30);
		cairo_set_line_width (cr, 1.0);
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
	ui->rw = rob_table_new (/*rows*/ 11, /*cols*/ 9, FALSE);
	robwidget_make_toplevel (ui->rw, top);

	ui->font[0] = pango_font_description_from_string ("Mono 9px");
	ui->font[1] = pango_font_description_from_string ("Sans 14px");

	prepare_faceplates (ui);

#define LB_W(PTR) robtk_lbl_widget(PTR)
#define DL_W(PTR) robtk_dial_widget(PTR)
#define SL_W(PTR) robtk_select_widget(PTR)
#define SP_W(PTR) robtk_sep_widget(PTR)

	ui->sel_spd = robtk_select_new ();

	robtk_select_add_item (ui->sel_spd, 0, "horn: stop, drum:stop");
	robtk_select_add_item (ui->sel_spd, 1, "horn: stop, drum:slow");
	robtk_select_add_item (ui->sel_spd, 2, "horn: stop, drum:fast");
	robtk_select_add_item (ui->sel_spd, 3, "horn: slow, drum:stop");
	robtk_select_add_item (ui->sel_spd, 4, "horn: slow, drum:slow");
	robtk_select_add_item (ui->sel_spd, 5, "horn: slow, drum:fast");
	robtk_select_add_item (ui->sel_spd, 6, "horn: fast, drum:stop");
	robtk_select_add_item (ui->sel_spd, 7, "horn: fast, drum:slow");
	robtk_select_add_item (ui->sel_spd, 8, "horn: fast, drum:fast");

	robtk_select_set_default_item (ui->sel_spd, 0);
	robtk_select_set_value (ui->sel_spd, 0);

	ui->box_spdsel = rob_vbox_new (FALSE, 2);
	rob_vbox_child_pack (ui->box_spdsel, SL_W(ui->sel_spd), TRUE, FALSE);

	ui->s_drumwidth = robtk_dial_new_with_size (0, 2, .05,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
	robtk_dial_set_default (ui->s_drumwidth, 1.0);
	robtk_dial_set_detent_default (ui->s_drumwidth, true);
	robtk_dial_set_scroll_mult (ui->s_drumwidth, 5.f);
	robtk_dial_annotation_callback (ui->s_drumwidth, dial_annotation_stereo, ui);
	robtk_dial_set_surface (ui->s_drumwidth, ui->dial_bg[14]);


	ui->lbl_drumwidth  = robtk_lbl_new ("Drum Stereo");

	ui->box_drmmic = rob_vbox_new (FALSE, 2);
	rob_vbox_child_pack (ui->box_drmmic, DL_W(ui->s_drumwidth), TRUE, FALSE);
	rob_vbox_child_pack (ui->box_drmmic, LB_W(ui->lbl_drumwidth), TRUE, FALSE);


	ui->lbl_flt[0][0]  = robtk_lbl_new ("Horn Character");
	ui->lbl_flt[1][0]  = robtk_lbl_new ("Horn Split");
	ui->lbl_flt[2][0]  = robtk_lbl_new ("Drum Split");

	for (int i = 0; i < 3; ++i) {
		ui->tbl_flt[i] = rob_table_new (/*rows*/ 6, /*cols*/ 2, FALSE);
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

		// table background
		robwidget_set_expose_event (ui->tbl_flt[i], tblbox_expose_event);

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

		ui->lbl_flt[i][1]  = robtk_lbl_new ("Type:");
		ui->lbl_flt[i][2]  = robtk_lbl_new ("Freq:");
		ui->lbl_flt[i][3]  = robtk_lbl_new ("Q:");
		ui->lbl_flt[i][4]  = robtk_lbl_new ("Gain:");

		robtk_lbl_set_alignment (ui->lbl_flt[i][1], 1, .5);
		robtk_lbl_set_alignment (ui->lbl_flt[i][2], 1, .5);
		robtk_lbl_set_alignment (ui->lbl_flt[i][3], 1, .5);
		robtk_lbl_set_alignment (ui->lbl_flt[i][4], 1, .5);

		robtk_select_add_item (ui->sel_fil[i], 0, "Low Pass");
		robtk_select_add_item (ui->sel_fil[i], 1, "High Pass");
		robtk_select_add_item (ui->sel_fil[i], 2, "Band Pass Q peak");
		robtk_select_add_item (ui->sel_fil[i], 3, "Band Pass 0dB peak");
		robtk_select_add_item (ui->sel_fil[i], 4, "Notch");
		robtk_select_add_item (ui->sel_fil[i], 5, "All Pass");
		robtk_select_add_item (ui->sel_fil[i], 6, "PEQ");
		robtk_select_add_item (ui->sel_fil[i], 7, "Low Shelf");
		robtk_select_add_item (ui->sel_fil[i], 8, "High Shelf");

		rob_table_attach (ui->tbl_flt[i], LB_W(ui->lbl_flt[i][0]), 0, 2, 0, 1,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_flt[i], LB_W(ui->lbl_flt[i][1]), 0, 1, 1, 2,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_flt[i], LB_W(ui->lbl_flt[i][2]), 0, 1, 2, 3,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_flt[i], LB_W(ui->lbl_flt[i][3]), 0, 1, 3, 4,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_flt[i], LB_W(ui->lbl_flt[i][4]), 0, 1, 4, 5,  5, 0, RTK_EXANDF, RTK_SHRINK);

		rob_table_attach (ui->tbl_flt[i], SL_W(ui->sel_fil[i]), 1, 2, 1, 2,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_flt[i], DL_W(ui->s_ffreq[i]), 1, 2, 2, 3,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_flt[i], DL_W(ui->s_fqual[i]), 1, 2, 3, 4,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_flt[i], DL_W(ui->s_fgain[i]), 1, 2, 4, 5,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_flt[i], ui->fil_tf[i],        0, 2, 5, 6,  5, 0, RTK_EXANDF, RTK_SHRINK);
	}

	robtk_select_set_default_item (ui->sel_fil[0], 0);
	robtk_select_set_default_item (ui->sel_fil[1], 7);
	robtk_select_set_default_item (ui->sel_fil[2], 8);


	ui->lbl_mix[0] = robtk_lbl_new ("Horn Level");
	ui->lbl_mix[1] = robtk_lbl_new ("Drum Level");
	ui->lbl_brk[0] = robtk_lbl_new ("Horn Brake");
	ui->lbl_brk[1] = robtk_lbl_new ("Drum Brake");

	ui->lbl_mtr[0][0]  = robtk_lbl_new ("Horn Motor");
	ui->lbl_mtr[1][0]  = robtk_lbl_new ("Drum Motor");

	for (int i = 0; i < 2; ++i) {
		ui->tbl_mtr[i] = rob_table_new (/*rows*/ 5, /*cols*/ 2, FALSE);
		ui->box_mix[i] = rob_vbox_new (FALSE, 2);
		ui->box_brk[i] = rob_vbox_new (FALSE, 2);

		robwidget_set_expose_event (ui->tbl_mtr[i], tblbox_expose_event);

		ui->s_level[i] = robtk_dial_new_with_size (-20, 20, .02,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->s_brakepos[i] = robtk_dial_new_with_size (0, 1, 1./360,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->s_rpm_slow[i] = robtk_dial_new_with_size (0, 1, 1./96,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->s_rpm_fast[i] = robtk_dial_new_with_size (0, 1, 1./96,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->s_accel[i] = robtk_dial_new_with_size (0, 1, 1./96,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->s_decel[i] = robtk_dial_new_with_size (0, 1, 1./96,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);

		robtk_dial_set_scroll_mult (ui->s_level[i], 5.f);
		robtk_dial_set_scroll_mult (ui->s_brakepos[i], 5.f);

		robtk_dial_set_constained (ui->s_rpm_slow[i], false);
		robtk_dial_set_constained (ui->s_rpm_fast[i], false);
		robtk_dial_set_constained (ui->s_accel[i], false);
		robtk_dial_set_constained (ui->s_decel[i], false);

		robtk_dial_set_default (ui->s_level[i], 0);
		robtk_dial_set_default (ui->s_brakepos[i], 0);
		robtk_dial_set_default (ui->s_rpm_slow[i], param_to_dial (&rpm_slow[i], rpm_slow[i].dflt));
		robtk_dial_set_default (ui->s_rpm_fast[i], param_to_dial (&rpm_fast[i], rpm_fast[i].dflt));
		robtk_dial_set_default (ui->s_accel[i], param_to_dial (&acceleration[i], acceleration[i].dflt));
		robtk_dial_set_default (ui->s_decel[i], param_to_dial (&deceleration[i], deceleration[i].dflt));

		robtk_dial_annotation_callback (ui->s_level[i], dial_annotation, NULL);
		robtk_dial_annotation_callback (ui->s_brakepos[i], dial_annotation_brake, ui);
		robtk_dial_annotation_callback (ui->s_rpm_slow[i], dial_annotation, (void*)&rpm_slow[i]);
		robtk_dial_annotation_callback (ui->s_rpm_fast[i], dial_annotation, (void*)&rpm_fast[i]);
		robtk_dial_annotation_callback (ui->s_accel[i], dial_annotation, (void*)&acceleration[i]);
		robtk_dial_annotation_callback (ui->s_decel[i], dial_annotation, (void*)&deceleration[i]);

		robtk_dial_set_surface (ui->s_level[i], ui->dial_bg[0]);
		robtk_dial_set_surface (ui->s_rpm_slow[i], ui->dial_bg[6 + i]);
		robtk_dial_set_surface (ui->s_rpm_fast[i], ui->dial_bg[8 + i]);
		robtk_dial_set_surface (ui->s_accel[i], ui->dial_bg[10 + i]);
		robtk_dial_set_surface (ui->s_decel[i], ui->dial_bg[12 + i]);
		robtk_dial_set_surface (ui->s_brakepos[i], ui->dial_bg[14]);

		static const float bpos[3] = {.25, .5, .75};
		robtk_dial_set_detents (ui->s_brakepos[i], 3, bpos);

		ui->lbl_mtr[i][1]  = robtk_lbl_new ("RPM slow");
		ui->lbl_mtr[i][2]  = robtk_lbl_new ("RPM fast");
		ui->lbl_mtr[i][3]  = robtk_lbl_new ("Acceleration");
		ui->lbl_mtr[i][4]  = robtk_lbl_new ("Deceleration");

		if (i == 0) {
			rob_vbox_child_pack (ui->box_mix[i], LB_W(ui->lbl_mix[i]), TRUE, FALSE);
			rob_vbox_child_pack (ui->box_mix[i], DL_W(ui->s_level[i]), TRUE, FALSE);
		} else {
			rob_vbox_child_pack (ui->box_mix[i], DL_W(ui->s_level[i]), TRUE, FALSE);
			rob_vbox_child_pack (ui->box_mix[i], LB_W(ui->lbl_mix[i]), TRUE, FALSE);
		}

		rob_vbox_child_pack (ui->box_brk[i], DL_W(ui->s_brakepos[i]), TRUE, FALSE);
		rob_vbox_child_pack (ui->box_brk[i], LB_W(ui->lbl_brk[i]), TRUE, FALSE);

		rob_table_attach (ui->tbl_mtr[i], LB_W(ui->lbl_mtr[i][0]), 0, 2, 0, 1,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], LB_W(ui->lbl_mtr[i][1]), 0, 1, 1, 2,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], LB_W(ui->lbl_mtr[i][2]), 0, 1, 2, 3,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], LB_W(ui->lbl_mtr[i][3]), 0, 1, 3, 4,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], LB_W(ui->lbl_mtr[i][4]), 0, 1, 4, 5,  5, 0, RTK_EXANDF, RTK_SHRINK);

		rob_table_attach (ui->tbl_mtr[i], DL_W(ui->s_rpm_slow[i]), 1, 2, 1, 2,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], DL_W(ui->s_rpm_fast[i]), 1, 2, 2, 3,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], DL_W(ui->s_accel[i]),    1, 2, 3, 4,  5, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->tbl_mtr[i], DL_W(ui->s_decel[i]),    1, 2, 4, 5,  5, 0, RTK_EXANDF, RTK_SHRINK);
	}

	/* set ranges & defaults */
	//robtk_dial_set_default(ui->spn_g_hifreq, freq_to_dial (&lphp[0], lphp[0].dflt));
	// TODO

	/* callbacks */
	robtk_select_set_callback (ui->sel_spd, cb_sel_spd, ui);
	robtk_dial_set_callback (ui->s_drumwidth, cb_dial_width, ui);

	robtk_dial_set_callback (ui->s_level[0], cb_dial_lvl0, ui);
	robtk_dial_set_callback (ui->s_brakepos[0], cb_dial_brk0, ui);
	robtk_dial_set_callback (ui->s_rpm_slow[0], cb_dial_rpms0, ui);
	robtk_dial_set_callback (ui->s_rpm_fast[0], cb_dial_rpmf0, ui);
	robtk_dial_set_callback (ui->s_accel[0], cb_dial_acc0, ui);
	robtk_dial_set_callback (ui->s_decel[0], cb_dial_dec0, ui);

	robtk_dial_set_callback (ui->s_level[1], cb_dial_lvl1, ui);
	robtk_dial_set_callback (ui->s_brakepos[1], cb_dial_brk1, ui);
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


	for (int i = 0; i < 6; ++i) {
		ui->sep_v[i] = robtk_sep_new (FALSE);
		ui->sep_h[i] = robtk_sep_new (TRUE);
		ui->sep_h[i]->m_height = 20;
		ui->sep_v[i]->m_width = 40;
		robwidget_set_expose_event (ui->sep_v[i]->rw, noop_expose_event);
		robwidget_set_expose_event (ui->sep_h[i]->rw, noop_expose_event);
	}

	/* top-level packing */
	rob_table_attach (ui->rw, SP_W(ui->sep_v[0]), 0,  1,  1, 10,  0, 0, RTK_EXANDF, RTK_EXANDF);
	rob_table_attach (ui->rw, SP_W(ui->sep_v[1]), 2,  3,  1,  4,  0, 0, RTK_EXANDF, RTK_EXANDF);
	rob_table_attach (ui->rw, SP_W(ui->sep_v[2]), 6,  7,  1,  4,  0, 0, RTK_EXANDF, RTK_EXANDF);
	rob_table_attach (ui->rw, SP_W(ui->sep_v[3]), 2,  3,  5, 10,  0, 0, RTK_EXANDF, RTK_EXANDF);
	rob_table_attach (ui->rw, SP_W(ui->sep_v[4]), 6,  7,  5, 10,  0, 0, RTK_EXANDF, RTK_EXANDF);
	rob_table_attach (ui->rw, SP_W(ui->sep_v[5]), 8,  9,  1, 10,  0, 0, RTK_EXANDF, RTK_EXANDF);

	rob_table_attach (ui->rw, SP_W(ui->sep_h[0]), 0,  9,  0,  1,  0, 0, RTK_EXANDF, RTK_EXANDF);
	rob_table_attach (ui->rw, SP_W(ui->sep_h[1]), 1,  8,  4,  5,  0, 0, RTK_EXANDF, RTK_EXANDF);
	rob_table_attach (ui->rw, SP_W(ui->sep_h[2]), 0,  9, 10, 11,  0, 0, RTK_EXANDF, RTK_EXANDF);

	rob_table_attach (ui->rw, ui->box_spdsel,     3,  6,  9, 10,  0, 0, RTK_EXANDF, RTK_SHRINK);

	rob_table_attach (ui->rw, ui->box_brk[0],     5,  6,  3,  4,  0, 0, RTK_SHRINK, RTK_SHRINK);
	rob_table_attach (ui->rw, ui->box_brk[1],     5,  6,  5,  6,  0, 0, RTK_SHRINK, RTK_SHRINK);

	rob_table_attach (ui->rw, ui->box_mix[0],     7,  8,  3,  4,  0, 0, RTK_FILL, RTK_SHRINK);
	rob_table_attach (ui->rw, ui->box_mix[1],     7,  8,  5,  6,  0, 0, RTK_FILL, RTK_SHRINK);
	rob_table_attach (ui->rw, ui->box_drmmic,     7,  8,  6,  7,  0, 0, RTK_FILL, RTK_SHRINK);

	rob_table_attach (ui->rw, ui->tbl_mtr[0],     5,  6,  1,  3,  2, 2, RTK_FILL, RTK_FILL);
	rob_table_attach (ui->rw, ui->tbl_mtr[1],     5,  6,  6,  8,  2, 2, RTK_FILL, RTK_SHRINK);

	rob_table_attach (ui->rw, ui->tbl_flt[0],     3,  4,  1,  3,  2, 2, RTK_EXANDF, RTK_FILL);
	rob_table_attach (ui->rw, ui->tbl_flt[1],     1,  2,  2,  4,  2, 2, RTK_EXANDF, RTK_FILL);
	rob_table_attach (ui->rw, ui->tbl_flt[2],     1,  2,  5,  7,  2, 2, RTK_EXANDF, RTK_FILL);

	// override expose and allocate for custom background
	ui->rw->size_allocate = bg_size_allocate;
	ui->rw->expose_event = bg_expose_event;

	for (int i = 0; i < 3; ++i) {
		update_eq (ui, i);
	}

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
	for (int i = 0; i < 2; ++i) {
		for (int j = 0; j < 5; ++j) {
			robtk_lbl_destroy (ui->lbl_mtr[i][j]);
		}
		robtk_lbl_destroy (ui->lbl_mix[i]);
		robtk_lbl_destroy (ui->lbl_brk[i]);
		robtk_dial_destroy (ui->s_level[i]);
		robtk_dial_destroy (ui->s_brakepos[i]);
		robtk_dial_destroy (ui->s_rpm_slow[i]);
		robtk_dial_destroy (ui->s_rpm_fast[i]);
		robtk_dial_destroy (ui->s_accel[i]);
		robtk_dial_destroy (ui->s_decel[i]);

		rob_table_destroy (ui->tbl_mtr[i]);
		rob_box_destroy (ui->box_mix[i]);
		rob_box_destroy (ui->box_brk[i]);
	}

	for (int i = 0; i < 6; ++i) {
		robtk_sep_destroy (ui->sep_v[i]);
		robtk_sep_destroy (ui->sep_h[i]);
	}

	robtk_lbl_destroy (ui->lbl_drumwidth);
	robtk_dial_destroy (ui->s_drumwidth);
	rob_box_destroy (ui->box_drmmic);

	robtk_select_destroy (ui->sel_spd);
	rob_box_destroy (ui->box_spdsel);


	pango_font_description_free (ui->font[0]);
	pango_font_description_free (ui->font[1]);

	for (int i = 0; i < 15; ++i) {
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
}

static void ui_disable (LV2UI_Handle handle) {
}


static LV2UI_Handle
instantiate(
		void* const               ui_toplevel,
		const LV2UI_Descriptor*   descriptor,
		const char*               plugin_uri,
		const char*               bundle_path,
		LV2UI_Write_Function      write_function,
		LV2UI_Controller          controller,
		RobWidget**               widget,
		const LV2_Feature* const* features)
{
	if (strcmp (plugin_uri, MTR_URI "extended")) {
		return NULL;
	}

	WhirlUI* ui = (WhirlUI*) calloc (1, sizeof (WhirlUI));
	if (!ui) { return NULL; }

	ui->nfo = robtk_info (ui_toplevel);
	ui->write      = write_function;
	ui->controller = controller;

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
	if (format != 0 || port_index < B3W_REVSELECT || port_index > B3W_FILTDGAIN) return;

	const float v = *(float *)buffer;
	ui->disable_signals = true;
	switch (port_index) {
		case B3W_REVSELECT:
			robtk_select_set_value (ui->sel_spd, v);
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
			robtk_dial_set_value (ui->s_brakepos[0], v);
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
			robtk_dial_set_value (ui->s_brakepos[1], v);
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
