/*
 * Copyright (C) 2013 Robin Gareus <robin@gareus.org>
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
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef RTK_CAIRO_H
#define RTK_CAIRO_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static float rtk_hue2rgb(const float p, const float q, float t) {
	if(t < 0.f) t += 1.f;
	if(t > 1.f) t -= 1.f;
	if(t < 1.f/6.f) return p + (q - p) * 6.f * t;
	if(t < 1.f/2.f) return q;
	if(t < 2.f/3.f) return p + (q - p) * (2.f/3.f - t) * 6.f;
	return p;
}

static void rgba_to_hsva (float* hsva, float const* rgba) {
	float r = rgba[0];
	float g = rgba[1];
	float b = rgba[2];
	hsva[3] = rgba[3];

	float cmax = fmaxf (r, fmaxf (g, b));
	float cmin = fminf (r, fminf (g, b));

	hsva[2] = cmax; // v

	if (cmax == 0) {
		// r = g = b == 0 ... v is undefined, s = 0
		hsva[0] = 0; // h
		hsva[1] = 0; // s
		return;
	}

	float delta = cmax - cmin;
	if (delta != 0.0) {
		if (cmax == r) {
			hsva[0] = fmodf ((g - b) / delta, 6.0);
		} else if (cmax == g) {
			hsva[0] = ((b - r) / delta) + 2;
		} else {
			hsva[0] = ((r - g) / delta) + 4;
		}

		hsva[0] *= 60.0;
		if (hsva[0] < 0.0) {
			/* negative values are legal but confusing, because
				 they alias positive values.
				 */
			hsva[0] = 360 + hsva[0];
		}
	} else {
		hsva[0] = 0; // h
		hsva[1] = 0; // s
		return;
	}

	if (delta == 0 || cmax == 0) {
		hsva[1] = 0;
	} else {
		hsva[1] = delta / cmax;
	}
}

static void hsva_to_rgba (float* rgba, float* hsva) {
	float h = hsva[0];
	float s = hsva[1];
	float v = hsva[2];
	rgba[3] = hsva[3];

	s = fminf (1.f, fmaxf (0.f, s));
	v = fminf (1.f, fmaxf (0.f, v));
	h = fmodf (h + 360.f, 360.f);

	if (s == 0) {
		rgba[0] = rgba[1] = rgba[2] = v;
		return;
	}
	float c = v * s;
	float x = c * (1.f - fabsf (fmodf (h / 60.f, 2) - 1.f));
	float m = v - c;
#define SETRGBA(R, G, B) rgba[0] = R; rgba[1] = G; rgba[2] = B;
	if (h >= 0.0 && h < 60.0) {
		SETRGBA (c + m, x + m, m);
	} else if (h >= 60.0 && h < 120.0) {
		SETRGBA (x + m, c + m, m);
	} else if (h >= 120.0 && h < 180.0) {
		SETRGBA (m, c + m, x + m);
	} else if (h >= 180.0 && h < 240.0) {
		SETRGBA (m, x + m, c + m);
	} else if (h >= 240.0 && h < 300.0) {
		SETRGBA (x + m, m, c + m);
	} else if (h >= 300.0 && h < 360.0) {
		SETRGBA (c + m, m, x + m);
	} else {
		SETRGBA (m, m, m);
	}
#undef SETRGBA
}

static void interpolate_hue (float* c, const float* c1, const float* c2, float f) {
	assert (f >= 0.f && f <= 1.f);
	float h1[4];
	float h2[4];
	rgba_to_hsva (h1, c1);
	rgba_to_hsva (h2, c2);

	float d = h2[0] - h1[0];
	if (d > 180) {
		d -= 360;
	} else if (d < -180) {
		d += 360;
	}

	assert (fabsf(d) <= 180);

	h1[0] = fmodf (360 + h1[0] + f * d, 360);
	h1[1] += f * (h2[1] - h1[1]);
	h1[2] += f * (h2[2] - h1[2]);
	h1[3] += f * (h2[3] - h1[3]);

	hsva_to_rgba (c, h1);
}

static void interpolate_rgb (float* c, const float* c1, const float* c2, float f) {
	assert (f >= 0.f && f <= 1.f);
	c[0] = c1[0] + f * (c2[0] - c1[0]);
	c[1] = c1[1] + f * (c2[1] - c1[1]);
	c[2] = c1[2] + f * (c2[2] - c1[2]);
	c[3] = fmax (c1[3], c2[3]);
}

static void interpolate_fg_bg (float* c, float fract) {
	float c_bg[4];
	float c_fg[4];
	get_color_from_theme (0, c_fg);
	get_color_from_theme (1, c_bg);
	interpolate_hue (c, c_fg, c_bg, fract);
}

static uint32_t rgba_to_hex (float *c)
{
	float r = fminf (1.f, fmaxf (0.f, c[0]));
	float g = fminf (1.f, fmaxf (0.f, c[1]));
	float b = fminf (1.f, fmaxf (0.f, c[2]));
	float a = fminf (1.f, fmaxf (0.f, c[3]));

	uint32_t rc, gc, bc, ac;
	rc = rint (r * 255.0);
	gc = rint (g * 255.0);
	bc = rint (b * 255.0);
	ac = rint (a * 255.0);
	return (rc << 24) | (gc << 16) | (bc << 8) | ac;
}

static float inv_gamma_srgb (const float v) {
	if (v <= 0.04045) {
		return v / 12.92;
	} else {
		return pow(((v + 0.055) / (1.055)), 2.4);
	}
}

static float gamma_srgb (float v) {
	if (v <= 0.0031308) {
		v *= 12.92;
	} else {
		v = 1.055 * powf (v, 1.0 / 2.4) - 0.055;
	}
	return v;
}

static float luminance_rgb (float const* c) {
	const float rY = 0.212655;
	const float gY = 0.715158;
	const float bY = 0.072187;
	return gamma_srgb (rY * inv_gamma_srgb (c[0]) + gY * inv_gamma_srgb(c[1]) + bY * inv_gamma_srgb (c[2]));
}

static void rounded_rectangle (cairo_t* cr, double x, double y, double w, double h, double r)
{
  double degrees = M_PI / 180.0;

  cairo_new_sub_path (cr);
  cairo_arc (cr, x + w - r, y + r, r, -90 * degrees, 0 * degrees);
  cairo_arc (cr, x + w - r, y + h - r, r, 0 * degrees, 90 * degrees);
  cairo_arc (cr, x + r, y + h - r, r, 90 * degrees, 180 * degrees);
  cairo_arc (cr, x + r, y + r, r, 180 * degrees, 270 * degrees);
  cairo_close_path (cr);
}

static void get_text_geometry( const char *txt, PangoFontDescription *font, int *tw, int *th) {
	cairo_surface_t* tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
	cairo_t *cr = cairo_create (tmp);
	PangoLayout * pl = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(pl, font);
	if (strncmp(txt, "<markup>", 8)) {
		pango_layout_set_text(pl, txt, -1);
	} else {
		pango_layout_set_markup(pl, txt, -1);
	}
	pango_layout_get_pixel_size(pl, tw, th);
	g_object_unref(pl);
	cairo_destroy (cr);
	cairo_surface_destroy(tmp);
}

static void write_text_full(
		cairo_t* cr,
		const char *txt,
		PangoFontDescription *font,
		const float x, const float y,
		const float ang, const int align,
		const float * const col) {
	int tw, th;
	cairo_save(cr);

	PangoLayout * pl = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(pl, font);
	if (strncmp(txt, "<markup>", 8)) {
		pango_layout_set_text(pl, txt, -1);
	} else {
		pango_layout_set_markup(pl, txt, -1);
	}
	pango_layout_get_pixel_size(pl, &tw, &th);
	cairo_translate (cr, rintf(x), rintf(y));
	if (ang != 0) { cairo_rotate (cr, ang); }
	switch(abs(align)) {
		case 1:
			cairo_translate (cr, -tw, ceil(th/-2.0));
			pango_layout_set_alignment (pl, PANGO_ALIGN_RIGHT);
			break;
		case 2:
			cairo_translate (cr, ceil(tw/-2.0), ceil(th/-2.0));
			pango_layout_set_alignment (pl, PANGO_ALIGN_CENTER);
			break;
		case 3:
			cairo_translate (cr, 0, ceil(th/-2.0));
			pango_layout_set_alignment (pl, PANGO_ALIGN_LEFT);
			break;
		case 4:
			cairo_translate (cr, -tw, -th);
			pango_layout_set_alignment (pl, PANGO_ALIGN_RIGHT);
			break;
		case 5:
			cairo_translate (cr, ceil(tw/-2.0), -th);
			pango_layout_set_alignment (pl, PANGO_ALIGN_CENTER);
			break;
		case 6:
			cairo_translate (cr, 0, -th);
			pango_layout_set_alignment (pl, PANGO_ALIGN_LEFT);
			break;
		case 7:
			cairo_translate (cr, -tw, 0);
			pango_layout_set_alignment (pl, PANGO_ALIGN_RIGHT);
			break;
		case 8:
			cairo_translate (cr, ceil(tw/-2.0), 0);
			pango_layout_set_alignment (pl, PANGO_ALIGN_CENTER);
			break;
		case 9:
			cairo_translate (cr, 0, 0);
			pango_layout_set_alignment (pl, PANGO_ALIGN_LEFT);
			break;
		default:
			break;
	}
	if (align < 0) {
		cairo_set_source_rgba (cr, .0, .0, .0, .5);
		cairo_rectangle (cr, 0, 0, tw, th);
		cairo_fill (cr);
	}
#if 1
	cairo_set_source_rgba (cr, col[0], col[1], col[2], col[3]);
	pango_cairo_show_layout(cr, pl);
#else
	cairo_set_source_rgba (cr, col[0], col[1], col[2], col[3]);
	pango_cairo_layout_path(cr, pl);
	cairo_fill(cr);
#endif
	g_object_unref(pl);
	cairo_restore(cr);
	cairo_new_path (cr);
}

static void create_text_surface3s(cairo_surface_t ** sf,
		const float w, const float h,
		float x, float y,
		const char * txt, PangoFontDescription *font,
		const float * const c_col, const float scale) {
	assert(sf);

	if (*sf) {
		cairo_surface_destroy(*sf);
	}
	*sf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ceilf(w * scale), ceilf(h * scale));
	cairo_t *cr = cairo_create (*sf);
	cairo_set_source_rgba (cr, .0, .0, .0, 0);
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
	cairo_rectangle (cr, 0, 0, ceil(w * scale), ceil(h * scale));
	cairo_fill (cr);
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

	cairo_scale (cr, scale, scale);
	x = floor (scale * x) + 1;
	y = floor (scale * y) + 1;
	write_text_full(cr, txt, font, ceil(x / scale), ceil(y / scale), 0, 2, c_col);
	cairo_surface_flush(*sf);
	cairo_destroy (cr);
}

static void create_text_surface3(cairo_surface_t ** sf,
		const float w, const float h,
		const float x, const float y,
		const char * txt, PangoFontDescription *font,
		const float * const c_col, const float scale) {
	assert(sf);

	if (*sf) {
		cairo_surface_destroy(*sf);
	}
	*sf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ceilf(w), ceilf(h));
	cairo_t *cr = cairo_create (*sf);
	cairo_set_source_rgba (cr, .0, .0, .0, 0);
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
	cairo_rectangle (cr, 0, 0, ceil(w), ceil(h));
	cairo_fill (cr);
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

	cairo_scale (cr, scale, scale);
	write_text_full(cr, txt, font, ceil(x / scale), ceil(y / scale), 0, 2, c_col);
	cairo_surface_flush(*sf);
	cairo_destroy (cr);
}

static void create_text_surface(cairo_surface_t ** sf,
		const float w, const float h,
		const float x, const float y,
		const char * txt, PangoFontDescription *font,
		const float * const c_col) {
	return create_text_surface3 (sf, w, h, x, y, txt, font, c_col, 1.0);
}


static void create_text_surface2(cairo_surface_t ** sf,
		const float w, const float h,
		const float x, const float y,
		const char * txt, PangoFontDescription *font,
		float ang, int align,
		const float * const c_col) {
	assert(sf);

	if (*sf) {
		cairo_surface_destroy(*sf);
	}
	*sf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ceilf(w), ceilf(h));
	cairo_t *cr = cairo_create (*sf);
	cairo_set_source_rgba (cr, .0, .0, .0, 0);
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
	cairo_rectangle (cr, 0, 0, ceil(w), ceil(h));
	cairo_fill (cr);
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

	write_text_full(cr, txt, font, ceil(x), ceil(y), ang, align, c_col);
	cairo_surface_flush(*sf);
	cairo_destroy (cr);
}


#ifdef _WIN32
#  include <windows.h>
#  include <shellapi.h>
#elif defined __APPLE__
// defined in pugl/pugl_osx.mm
extern bool rtk_osx_open_url (const char* url);
#else
#  include <stdlib.h>
#endif

static void rtk_open_url (const char *url) {
	// assume URL is escaped and shorter than 1024 chars;
#ifdef _WIN32
	ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif defined __APPLE__
	rtk_osx_open_url (url);
#else
	char tmp[1024];
	sprintf(tmp, "xdg-open %s &", url);
	(void) system (tmp);
#endif
}

#endif
