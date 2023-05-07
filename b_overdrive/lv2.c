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

#include "overdrive.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LV2_1_18_6
#include <lv2/core/lv2.h>
#else
#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#endif

#define B3O_URI "http://gareus.org/oss/lv2/b_overdrive"

typedef enum {
	B30_INPUT     = 0,
	B30_OUTPUT    = 1,
	B30_BIAS      = 2,
	B30_FEEDBACK  = 3,
	B30_SAGTOBIAS = 4,
	B30_POSTFEED  = 5,
	B30_GLOBFEED  = 6,
	B30_GAININ    = 7,
	B30_GAINOUT   = 8
} PortIndex;

typedef struct {
	float* input;
	float* output;

	float* bias;
	float* feedback;
	float* sagtobias;
	float* postfeed;
	float* globfeed;
	float* gainin;
	float* gainout;

	float o_bias;
	float o_feedback;
	float o_sagtobias;
	float o_postfeed;
	float o_globfeed;
	float o_gainin;
	float o_gainout;

	void* pa;
} B3O;

static LV2_Handle
instantiate (const LV2_Descriptor*     descriptor,
             double                    rate,
             const char*               bundle_path,
             const LV2_Feature* const* features)
{
	B3O* b3o    = (B3O*)calloc (1, sizeof (B3O));
	b3o->o_bias = b3o->o_feedback = b3o->o_sagtobias = b3o->o_postfeed = b3o->o_globfeed = b3o->o_gainin = b3o->o_gainout = -1;

	b3o->pa = allocPreamp ();
	initPreamp (b3o->pa, NULL);

	return (LV2_Handle)b3o;
}

static void
connect_port (LV2_Handle instance,
              uint32_t   port,
              void*      data)
{
	B3O* b3o = (B3O*)instance;

	switch ((PortIndex)port) {
		case B30_INPUT:
			b3o->input = (float*)data;
			break;
		case B30_OUTPUT:
			b3o->output = (float*)data;
			break;
		case B30_BIAS:
			b3o->bias = (float*)data;
			break;
		case B30_FEEDBACK:
			b3o->feedback = (float*)data;
			break;
		case B30_SAGTOBIAS:
			b3o->sagtobias = (float*)data;
			break;
		case B30_POSTFEED:
			b3o->postfeed = (float*)data;
			break;
		case B30_GLOBFEED:
			b3o->globfeed = (float*)data;
			break;
		case B30_GAININ:
			b3o->gainin = (float*)data;
			break;
		case B30_GAINOUT:
			b3o->gainout = (float*)data;
			break;
	}
}

static void
activate (LV2_Handle instance)
{
}

/* clang-format off */
#define SETPARAM(FN, NAME)                            \
        if (b3o->NAME) {                              \
                if (b3o->o_##NAME != *(b3o->NAME)) {  \
                        FN (b3o->pa, *(b3o->NAME));   \
                        b3o->o_##NAME = *(b3o->NAME); \
                }                                     \
        }
/* clang-format on */

static void
run (LV2_Handle instance, uint32_t n_samples)
{
	B3O* b3o = (B3O*)instance;

	const float* const input  = b3o->input;
	float* const       output = b3o->output;

	SETPARAM (fctl_biased, bias);
	SETPARAM (fctl_biased_fb, feedback);
	SETPARAM (fctl_sagtoBias, sagtobias);
	SETPARAM (fctl_biased_fb2, postfeed);
	SETPARAM (fctl_biased_gfb, globfeed);
	SETPARAM (fsetInputGain, gainin);
	SETPARAM (fsetOutputGain, gainout);

	overdrive (b3o->pa, input, output, n_samples);
}

static void
deactivate (LV2_Handle instance)
{
}

static void
cleanup (LV2_Handle instance)
{
	B3O* b3o = (B3O*)instance;
	freePreamp (b3o->pa);
	free (instance);
}

const void*
extension_data (const char* uri)
{
	return NULL;
}

static const LV2_Descriptor descriptor = {
	B3O_URI,
	instantiate,
	connect_port,
	activate,
	run,
	deactivate,
	cleanup,
	extension_data
};

// fix for -fvisibility=hidden
#undef LV2_SYMBOL_EXPORT
#ifdef _WIN32
#define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
#define LV2_SYMBOL_EXPORT __attribute__ ((visibility ("default")))
#endif

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor (uint32_t index)
{
	switch (index) {
		case 0:
			return &descriptor;
		default:
			return NULL;
	}
}

void
useMIDIControlFunction (void* m, const char* cfname, void (*f) (void*, unsigned char), void* d)
{
}
int
getConfigParameter_fr (const char* par, ConfigContext* cfg, float* fp, float lowInc, float highInc)
{
	return 0;
}
int
getConfigParameter_f (const char* par, ConfigContext* cfg, float* fp)
{
	return 0;
}
