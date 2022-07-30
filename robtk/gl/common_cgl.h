/* robwidget - gtk2 & GL wrapper
 *
 * Copyright (C) 2013-2016 Robin Gareus <robin@gareus.org>
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

#ifndef COMMON_CAIRO_H
#define COMMON_CAIRO_H

#include <string.h>
#include <cairo/cairo.h>
#include <pango/pango.h>

static PangoFontDescription * get_font_from_theme () {
  PangoFontDescription * rv;
	rv = pango_font_description_from_string("Sans 11px");
	assert(rv);
	return rv;
}

static float host_fg_color[4] = { .9, .9, .9, 1.0 };
static float host_bg_color[4] = { .24, .24, .24, 1.0 };
static bool  rtk_light_theme = false;

static void set_host_color (int which, uint32_t color) {
	switch(which) {
		case 0:
			host_fg_color[0] = ((color >> 24) & 0xff) / 255.0;
			host_fg_color[1] = ((color >> 16) & 0xff) / 255.0;
			host_fg_color[2] = ((color >>  8) & 0xff) / 255.0;
			host_fg_color[3] = ((color >>  0) & 0xff) / 255.0;
			rtk_light_theme = luminance_rgb (host_fg_color) < 0.5;
			break;
		case 1:
			host_bg_color[0] = ((color >> 24) & 0xff) / 255.0;
			host_bg_color[1] = ((color >> 16) & 0xff) / 255.0;
			host_bg_color[2] = ((color >>  8) & 0xff) / 255.0;
			host_bg_color[3] = ((color >>  0) & 0xff) / 255.0;
			break;
		default:
			break;
	}
}

static void get_color_from_theme (int which, float *col) {
	switch(which) {
		default: // fg
			memcpy (col, host_fg_color, 4 * sizeof (float));
			break;
		case 1: // bg
			memcpy (col, host_bg_color, 4 * sizeof (float));
			break;
	}
}

static bool is_light_theme () {
	return rtk_light_theme;
}
#endif
