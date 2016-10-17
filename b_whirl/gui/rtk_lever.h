/* robtk lever widget (leslie controls) -- needs cleanup before merge to robtk
 *
 * Copyright (C) 2013,2015 Robin Gareus <robin@gareus.org>
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

#ifndef _ROB_TK_LEVER_H_
#define _ROB_TK_LEVER_H_

#define LEVERSPC 4 // rounded up C_RAD

#ifndef SQUARE
#define SQUARE(X) ( (X) * (X) )
#endif

typedef struct {
	RobWidget *rw;

	float min;
	float max;
	float acc;
	float cur;
	float dfl;

	float drag_x, drag_y, drag_c;
	bool sensitive;
	bool prelight;

	bool (*cb) (RobWidget* w, void* handle);
	void* handle;

	cairo_surface_t* bg;

	bool recreate_patterns;
	cairo_pattern_t* spat[2];
	cairo_pattern_t* kpat[2];

	float m_width, m_height; // minium size
	float w_width, w_height;
	float l_offset;
	float g_offset;
	bool horiz;

	int    label_len;
	int    label_cnt;
	char **label_txt;
	float *label_val;
} RobTkLever;


static int robtk_lever_round_length (RobTkLever * d, float v) {
	const float val = (v - d->min) / (d->max - d->min);
	const float mid = rint (d->horiz ? d->w_width * .5 : d->w_height * .5);
	const float girth = d->horiz ? d->m_height : d->m_width;
	const float slt = 2 * (mid - (LEVERSPC + d->l_offset)) - girth;
	const float pslt = (slt - girth * 1.3);
	const float ang = d->horiz ? (val - .5) : (.5 - val); // -.5 .. +.5;
	return rint (mid + pslt * ang) + .5;
}

static void robtk_lever_update_value (RobTkLever * d, float val) {
	if (val < d->min) val = d->min;
	if (val > d->max) val = d->max;
	if (val != d->cur) {
		float oldval = d->cur;
		d->cur = val;
		if (d->cb) d->cb (d->rw, d->handle);
		if (robtk_lever_round_length (d, oldval) != robtk_lever_round_length (d, val)) {
			queue_draw (d->rw);
		}
	}
}

static RobWidget* robtk_lever_mousedown (RobWidget *handle, RobTkBtnEvent *event) {
	RobTkLever * d = (RobTkLever *)GET_HANDLE(handle);
	if (!d->sensitive) { return NULL; }
	if (event->state & ROBTK_MOD_SHIFT) {
		robtk_lever_update_value (d, d->dfl);
	} else {
		d->drag_x = event->x;
		d->drag_y = event->y;
		d->drag_c = d->cur;
	}
	queue_draw (d->rw);
	return handle;
}

static RobWidget* robtk_lever_mouseup (RobWidget *handle, RobTkBtnEvent *event) {
	RobTkLever * d = (RobTkLever *)GET_HANDLE(handle);
	if (!d->sensitive) { return NULL; }
	d->drag_x = d->drag_y = -1;
	queue_draw (d->rw);
	return NULL;
}

static RobWidget* robtk_lever_mousemove (RobWidget *handle, RobTkBtnEvent *event) {
	RobTkLever * d = (RobTkLever *)GET_HANDLE(handle);
	if (d->drag_x < 0 || d->drag_y < 0) return NULL;

	if (!d->sensitive) {
		d->drag_x = d->drag_y = -1;
		queue_draw (d->rw);
		return NULL;
	}
	float len;
	float diff;
	if (d->horiz) {
		len = d->w_width - 2 * (LEVERSPC + d->l_offset) - d->m_height;
		diff = (event->x - d->drag_x) / len;
	} else {
		len = d->w_height - 2 * (LEVERSPC + d->l_offset) - d->m_width;
		diff = (d->drag_y - event->y) / len;
	}
	diff = rint (diff * (d->max - d->min) / d->acc ) * d->acc;
	float val = d->drag_c + diff;

	robtk_lever_update_value (d, val);
	return handle;
}

static void robtk_lever_enter_notify (RobWidget *handle) {
	RobTkLever * d = (RobTkLever *)GET_HANDLE(handle);
	if (!d->prelight) {
		d->prelight = TRUE;
		queue_draw (d->rw);
	}
}

static void robtk_lever_leave_notify (RobWidget *handle) {
	RobTkLever * d = (RobTkLever *)GET_HANDLE(handle);
	if (d->prelight) {
		d->prelight = FALSE;
		queue_draw (d->rw);
	}
}

static void
robtk_lever_size_request (RobWidget* handle, int *w, int *h) {
	RobTkLever * d = (RobTkLever *)GET_HANDLE(handle);
	*w = d->m_width;
	*h = d->m_height;
	if (d->horiz) {
		*w += 2 * d->l_offset;
		*h += 2 * d->g_offset;
		if (*w < d->label_len) {
			*w = d->label_len + 2 * (LEVERSPC + d->m_height);
		}
	} else {
		*w += 2 * d->g_offset;
		*h += 2 * d->l_offset;
		if (*h < d->label_len) {
			*h = d->label_len + 2 * (LEVERSPC + d->m_width);
		}
	}
}

static void
robtk_lever_size_allocate(RobWidget* handle, int w, int h) {
	RobTkLever * d = (RobTkLever *)GET_HANDLE(handle);
	d->w_width = w;
	d->w_height = h;
	robwidget_set_size (handle, d->w_width, d->w_height);
	d->recreate_patterns = true;
}

static RobWidget* robtk_lever_scroll (RobWidget *handle, RobTkBtnEvent *ev) {
	RobTkLever * d = (RobTkLever *)GET_HANDLE(handle);
	if (!d->sensitive) { return NULL; }

	if (!(d->drag_x < 0 || d->drag_y < 0)) {
		d->drag_x = d->drag_y = -1;
	}

	float val = d->cur;
	switch (ev->direction) {
		case ROBTK_SCROLL_RIGHT:
		case ROBTK_SCROLL_UP:
			val += d->acc;
			break;
		case ROBTK_SCROLL_LEFT:
		case ROBTK_SCROLL_DOWN:
			val -= d->acc;
			break;
		default:
			break;
	}
	robtk_lever_update_value (d, val);
	return NULL;
}

static void _robtk_lever_create_patterns (RobTkLever *d) {
	d->recreate_patterns = false;

	if (d->spat[0]) { cairo_pattern_destroy(d->spat[0]); }
	if (d->spat[1]) { cairo_pattern_destroy(d->spat[1]); }

	if (d->horiz) {
		d->spat[0] = cairo_pattern_create_linear (0.0, 0.0, 0.0, d->w_height);
		d->spat[1] = cairo_pattern_create_linear (0.0, 0.0, d->w_width, 0);
	} else {
		d->spat[0] = cairo_pattern_create_linear (0.0, 0.0, d->w_width, 0);
		d->spat[1] = cairo_pattern_create_linear (0.0, 0.0, 0.0, d->w_height);
	}

	cairo_pattern_add_color_stop_rgb (d->spat[0], 0.0, .30, .30, .30);
	cairo_pattern_add_color_stop_rgb (d->spat[0], 0.5, .31, .31, .31);
	cairo_pattern_add_color_stop_rgb (d->spat[0], 1.0, .20, .20, .20);

	cairo_pattern_add_color_stop_rgba (d->spat[1], 0.00, .2, .2, .2, .0);
	cairo_pattern_add_color_stop_rgba (d->spat[1], 0.10, .2, .2, .2, .1);
	cairo_pattern_add_color_stop_rgba (d->spat[1], 0.40, .7, .7, .7, .3);
	cairo_pattern_add_color_stop_rgba (d->spat[1], 0.60, .7, .7, .7, .3);
	cairo_pattern_add_color_stop_rgba (d->spat[1], 0.90, .2, .2, .2, .1);
	cairo_pattern_add_color_stop_rgba (d->spat[1], 1.00, .0, .0, .0, .0);

	// XXX
	if (d->bg) {
		cairo_surface_destroy (d->bg);
		d->bg = NULL;
	}

#if 0 // attempt at 'leslie ctrl box'
	if (d->l_offset > 0 && d->g_offset > 0) {
		d->bg = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, d->w_width, d->w_height);
		cairo_t *cr = cairo_create (d->bg);
		cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint (cr);
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		CairoSetSouerceRGBA (c_blk);

		if (d->horiz) {
			const float len = d->w_width - 2 * (d->l_offset) - d->m_height;
			float rad = sqrt (SQUARE(len * .5) + SQUARE(d->m_height));
			cairo_rectangle (cr, d->w_width * .5 - rad + LEVERSPC, 0, 2 * (rad - LEVERSPC), d->w_height);
			cairo_clip(cr);
			cairo_scale (cr, 1.0, .8);
			cairo_arc (cr, d->w_width * .5, 0, rad, 0, 2 * M_PI);
			cairo_fill(cr);
		} else {
			const float len = d->w_height - 2 * (d->l_offset) - d->m_width;
			float rad = sqrt (SQUARE(len * .5) + SQUARE(d->m_width));
			cairo_rectangle (cr, 0, d->w_height * .5 - rad + LEVERSPC, d->w_width, 2 * (rad - LEVERSPC));
			cairo_clip(cr);
			cairo_scale (cr, .8, 1.0);
			cairo_arc (cr, 0, d->w_height * .5, rad, 0, 2 * M_PI);
			cairo_fill(cr);
		}

	}
#endif
	if (d->label_cnt > 0) {
		d->bg = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, d->w_width, d->w_height);
		cairo_t *cr = cairo_create (d->bg);
		cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint (cr);
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		CairoSetSouerceRGBA (c_blk);

		PangoFontDescription *font = get_font_from_theme();
		float c[4];
		get_color_from_theme (0, c);

		for (int i = 0; i < d->label_cnt; ++i) {
#if 0  // TODO use if (d->l_offset == 0) ?? label at mark
			const float v = (d->label_val[i] - d->min) / (d->max - d->min);
			const float p = robtk_lever_round_length (d, d->label_val[i]);
			int align;
			if (v == .5) { align = 8; }
			else if (v > .5) { align = 7; }
			else { align = 9; }
#else // use if number of labels == number of steps .. and/or < 4 steps..
			int align = 8;
			const float p = (d->horiz ? d->w_width : d->w_height) * (i + 1) / (2 + d->max - d->min);
#endif

			if (d->horiz) {
#if 0 // TODO optinal tickmarks
				cairo_move_to (cr, p, .5 + LEVERSPC * .5);
				cairo_line_to (cr, p, .5 + d->m_height - LEVERSPC * .5);
				CairoSetSouerceRGBA (c_wht);
				cairo_set_line_width (cr, 1);
				cairo_stroke (cr);
#endif

				write_text_full(cr, d->label_txt[i], font, p, d->m_height + LEVERSPC, 0, align, c);
			} else {

#if 0
				cairo_move_to (cr, .5 + LEVERSPC * .5, p);
				cairo_line_to (cr, .5 + d->m_width - LEVERSPC * .5, p);
				CairoSetSouerceRGBA (c_wht);
				cairo_set_line_width (cr, 1);
				cairo_stroke (cr);
#endif
				write_text_full(cr, d->label_txt[i], font, d->m_width + LEVERSPC, p, -M_PI / 2, align, c);
			}
		}
		pango_font_description_free (font);
		cairo_destroy (cr);
	}
}

static bool robtk_lever_expose_event (RobWidget* handle, cairo_t* cr, cairo_rectangle_t* ev) {
	RobTkLever * d = (RobTkLever *)GET_HANDLE(handle);
	cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
	cairo_clip (cr);

	float c[4];
	get_color_from_theme (1, c);
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgb (cr, c[0], c[1], c[2]);
	cairo_rectangle (cr, 0, 0, d->w_width, d->w_height);
	cairo_fill (cr);

	if (d->recreate_patterns) {
		_robtk_lever_create_patterns (d);
	}

	if (d->bg) {
		if (!d->sensitive) {
			//cairo_set_operator (cr, CAIRO_OPERATOR_OVERLAY);
			cairo_set_operator (cr, CAIRO_OPERATOR_SOFT_LIGHT);
		} else {
			cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		}
		cairo_set_source_surface (cr, d->bg, 0, 0);
		cairo_paint (cr);
	}

	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

	const float w0 = LEVERSPC;
	const float girth = d->horiz ? d->m_height : d->m_width;
	const float wid = girth - 2 * LEVERSPC;
	const float ctr = .5 + w0 + .5 * wid;
	const float mid = rint(d->horiz ? d->w_width * .5 : d->w_height * .5);
	const float slt = 2 * (mid - (LEVERSPC + d->l_offset)) - girth;

	// draw fixed slot
	if (d->horiz) {
		rounded_rectangle (cr, mid - slt * .5, .5 + w0, slt, wid, C_RAD);
	} else {
		rounded_rectangle (cr, .5 + w0, mid - slt * .5, wid, slt, C_RAD);
	}
	if (d->sensitive) {
		cairo_set_source(cr, d->spat[0]);
	}  else {
		cairo_set_source_rgba (cr, .5, .5, .5, 1.0);
	}
	cairo_fill_preserve (cr);

	if (d->sensitive && (d->prelight || d->drag_x > 0)) {
		cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, .1);
		cairo_fill_preserve (cr);
	}

	if (d->sensitive) {
		cairo_set_source_rgba (cr, .0, .0, .0, 1.0);
	} else {
		cairo_set_source_rgba (cr, .5, .5, .5, 1.0);
	}
	cairo_set_line_width (cr, .75);
	cairo_stroke_preserve(cr);

	// TODO consolidate with robtk_lever_round_length()
	const float pslt = (slt - girth * 1.3);
	const float val = (d->cur - d->min) / (d->max - d->min);
	const float ang = d->horiz ? (val - .5) : (.5 - val); // -.5 .. +.5;
	const float kc = mid + slt * ang;
	const float kt = mid + pslt * ang;

	cairo_matrix_t matrix;
	if (d->horiz) {
		cairo_matrix_init_translate(&matrix, -ang * pslt, 0.0);
	} else {
		cairo_matrix_init_translate(&matrix, 0.0, -ang * pslt);
	}
	cairo_pattern_set_matrix (d->spat[1], &matrix);

	/* value in slot */
	cairo_set_source(cr, d->spat[1]);
	cairo_fill (cr);

	/* draw knob, angle dependent patterns */
	float colr = .3;
	if (d->sensitive && (d->prelight || d->drag_x > 0)) {
		colr = .4;
	} else if (!d->sensitive) {
		colr = .1;
	}

	if (d->kpat[0]) { cairo_pattern_destroy(d->kpat[0]); }
	if (d->kpat[1]) { cairo_pattern_destroy(d->kpat[1]); }

	const float roff = girth * .2;
	if (d->horiz) {
		d->kpat[0] = cairo_pattern_create_linear (kc - girth, 0.0, kc + girth, 0.0);
		d->kpat[1] = cairo_pattern_create_radial (
				kc - 1 * ang   , ctr + 2, 1,
				kc - roff * ang, ctr    , girth * .45
				);
	} else {
		d->kpat[0] = cairo_pattern_create_linear (0.0, kc - girth, 0.0, kc + girth);
		d->kpat[1] = cairo_pattern_create_radial (
				ctr + 2, kc - 1 * ang   , 1,
				ctr    , kc - roff * ang, girth * .45
				);
	}

	if (ang > 0) {
		cairo_pattern_add_color_stop_rgb (d->kpat[0], 0.0, .1, .1, .1);
		cairo_pattern_add_color_stop_rgb (d->kpat[0], 1.0, colr, colr, colr);
	} else {
		cairo_pattern_add_color_stop_rgb (d->kpat[0], 0.0, colr, colr, colr);
		cairo_pattern_add_color_stop_rgb (d->kpat[0], 1.0, .1, .1, .1);
	}
	cairo_pattern_add_color_stop_rgb (d->kpat[1], 0.0, colr, colr, colr);
	cairo_pattern_add_color_stop_rgb (d->kpat[1], 1.0, .1, .1, .1);

	/* knob */
	if (d->horiz) {
		cairo_set_source(cr, d->kpat[0]);
		cairo_rectangle (cr, kc, ctr - girth * .2, kt - kc, girth * .4);
		cairo_fill (cr);

		cairo_arc (cr, kt, ctr, girth * .2, 0, 2 * M_PI);
		cairo_fill (cr);

		cairo_arc (cr, kc, ctr, girth * .45, 0, 2 * M_PI);
		cairo_set_source(cr, d->kpat[1]);
		cairo_fill (cr);

	} else {
		cairo_set_source(cr, d->kpat[0]);
		cairo_rectangle (cr, ctr - girth * .2, kc, girth * .4,  kt - kc);
		cairo_fill (cr);

		cairo_arc (cr, ctr, kt, girth * .2, 0, 2 * M_PI);
		cairo_fill (cr);

		cairo_arc (cr, ctr, kc, girth * .45, 0, 2 * M_PI);
		cairo_set_source(cr, d->kpat[1]);
		cairo_fill (cr);
	}
	return TRUE;
}


/******************************************************************************
 * public functions
 */

static RobTkLever * robtk_lever_new (float min, float max, float step,
		int girth, int length, bool horiz) {

	assert (max > min);
	assert (step > 0);
	assert ((max - min) / step >= 1.0);

	RobTkLever *d = (RobTkLever *) malloc (sizeof (RobTkLever));

	// TODO properly constrain size
	if (girth  < 10 + C_RAD) { girth = 10 + C_RAD; }
	if (length < 4 * girth)  { length = 4 * girth; }

	d->horiz = horiz;
	if (horiz) {
		d->w_width = d->m_width = length;
		d->w_height = d->m_height = girth;
	} else {
		d->w_width = d->m_width = girth;
		d->w_height = d->m_height = length;
	}

	d->rw = robwidget_new (d);
	ROBWIDGET_SETNAME (d->rw, "lever");

	d->cb = NULL;
	d->handle = NULL;
	d->min = min;
	d->max = max;
	d->acc = step;
	d->cur = min;
	d->dfl = min;
	d->sensitive = TRUE;
	d->prelight = FALSE;
	d->drag_x = d->drag_y = -1;
	d->bg  = NULL;
	d->g_offset = 0; // text-height
	d->l_offset = 35;
	d->recreate_patterns = true;
	d->spat[0] = NULL;
	d->spat[1] = NULL;
	d->kpat[0] = NULL;
	d->kpat[1] = NULL;

	d->label_len = 0;
	d->label_cnt = 0;
	d->label_val = NULL;
	d->label_txt = NULL;

	robwidget_set_size_request (d->rw, robtk_lever_size_request);
	robwidget_set_size_allocate (d->rw, robtk_lever_size_allocate);

	robwidget_set_expose_event (d->rw, robtk_lever_expose_event);
	robwidget_set_mouseup (d->rw, robtk_lever_mouseup);
	robwidget_set_mousedown (d->rw, robtk_lever_mousedown);
	robwidget_set_mousemove (d->rw, robtk_lever_mousemove);
	robwidget_set_mousescroll (d->rw, robtk_lever_scroll);
	robwidget_set_enter_notify (d->rw, robtk_lever_enter_notify);
	robwidget_set_leave_notify (d->rw, robtk_lever_leave_notify);

	return d;
}

static void robtk_lever_destroy (RobTkLever *d) {
	if (d->spat[0]) { cairo_pattern_destroy(d->spat[0]); }
	if (d->spat[1]) { cairo_pattern_destroy(d->spat[1]); }
	if (d->kpat[0]) { cairo_pattern_destroy(d->kpat[0]); }
	if (d->kpat[1]) { cairo_pattern_destroy(d->kpat[1]); }

	for (int i = 0; i < d->label_cnt; ++i) {
		free(d->label_txt[i]);
	}
	free(d->label_txt);
	free(d->label_val);

	robwidget_destroy (d->rw);
	free (d);
}

static RobWidget * robtk_lever_widget (RobTkLever *d) {
	return d->rw;
}

static void robtk_lever_set_callback (RobTkLever *d, bool (*cb) (RobWidget* w, void* handle), void* handle) {
	d->cb = cb;
	d->handle = handle;
}

static void robtk_lever_set_value (RobTkLever *d, float v) {
	v = d->min + rint ((v - d->min) / d->acc) * d->acc;
	robtk_lever_update_value (d, v);
}

static void robtk_lever_set_sensitive (RobTkLever *d, bool s) {
	if (d->sensitive != s) {
		d->sensitive = s;
		queue_draw (d->rw);
	}
}

static float robtk_lever_get_value (RobTkLever *d) {
	return (d->cur);
}

static void robtk_lever_set_default (RobTkLever *d, float v) {
	assert (v >= d->min);
	assert (v <= d->max);
	d->dfl = v;
}


static void robtk_lever_add_mark (RobTkLever *d, float v, const char *txt) {
	assert (txt);
	assert (v >= d->min);
	assert (v <= d->max);
	// TODO assert only during setup (no lock)

	int tw = 0;
	int th = 0;
	PangoFontDescription *font = get_font_from_theme();
	get_text_geometry(txt, font, &tw, &th);
	if (th + LEVERSPC > d->g_offset) {
		d->g_offset = th + LEVERSPC;
	}
	pango_font_description_free (font);
	d->label_len += tw + 8;

	d->label_val = (float *) realloc(d->label_val, sizeof(float) * (d->label_cnt+1));
	d->label_txt = (char **) realloc(d->label_txt, sizeof(char*) * (d->label_cnt+1));
	d->label_val[d->label_cnt] = v;
	d->label_txt[d->label_cnt] = strdup(txt);
	d->label_cnt++;
}


#endif
