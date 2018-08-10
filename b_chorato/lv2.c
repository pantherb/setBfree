/* setBfree - Chorus/Vibrato Plugin
 *
 * Copyright (C) 2018 Robin Gareus <robin@gareus.org>
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
#include <string.h>

#include "vibrato.h"

double SampleRateD = 48000.0; // global

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define B3V_URI "http://gareus.org/oss/lv2/b_chorato"

typedef enum {
	B3V_INPUT    = 0,
	B3V_OUTPUT,
	B3V_MODE,
	B3V_FREQ,
} PortIndex;

typedef struct {
	float* input;
	float* output;
	float* mode;
	float* freq;
	struct b_vibrato* instance;
} B3V;

static LV2_Handle
instantiate (const LV2_Descriptor*     descriptor,
             double                    rate,
             const char*               bundle_path,
             const LV2_Feature* const* features)
{
	B3V* b3v = (B3V*)calloc (1, sizeof (B3V));
	if (!b3v) {
		return NULL;
	}

	b3v->instance = (struct b_vibrato*)calloc (1, sizeof (struct b_vibrato));
	if (!b3v->instance) {
		free (b3v);
		return NULL;
	}

	SampleRateD = rate;
	reset_vibrato (b3v->instance);
	init_vibrato (b3v->instance);

	return (LV2_Handle)b3v;
}

static void
connect_port (LV2_Handle instance,
              uint32_t   port,
              void*      data)
{
	B3V* b3v = (B3V*)instance;

	switch ((PortIndex)port) {
		case B3V_INPUT:
			b3v->input = (float*)data;
			break;
		case B3V_OUTPUT:
			b3v->output = (float*)data;
			break;
		case B3V_MODE:
			b3v->mode = (float*)data;
			break;
		case B3V_FREQ:
			b3v->freq = (float*)data;
			break;
	}
}

static void
activate (LV2_Handle instance)
{
}

static void
run (LV2_Handle instance, uint32_t n_samples)
{
	B3V* b3v = (B3V*)instance;
	struct b_vibrato* v = b3v->instance;

	int mode = rint (*(b3v->mode));
	switch (mode) {
		default:
		case 0:
		case 1:
			v->offsetTable   = v->offset1Table;
			break;
		case 2:
		case 3:
			v->offsetTable   = v->offset2Table;
			break;
		case 4:
		case 5:
			v->offsetTable   = v->offset3Table;
			break;
	}
	v->effectEnabled = 1;
	v->mixedBuffers = mode & 1; /* chorus */

	v->vibFqHertz = *(b3v->freq);
	v->statorIncrement =
		(unsigned int)(((v->vibFqHertz * INCTBL_SIZE) / SampleRateD) * 65536.0);

	vibratoProc (v, b3v->input, b3v->output, n_samples);
}

static void
deactivate (LV2_Handle instance)
{
}

static void
cleanup (LV2_Handle instance)
{
	B3V* b3v = (B3V*)instance;
	free (b3v->instance);
	free (instance);
}

const void*
extension_data (const char* uri)
{
	return NULL;
}

static const LV2_Descriptor descriptor = {
	B3V_URI,
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

#include <assert.h>

void
setVibratoLower (struct b_tonegen* t, int isEnabled)
{
	assert (0);
}
void
setVibratoUpper (struct b_tonegen* t, int isEnabled)
{
	assert (0);
}
int
getVibratoRouting (struct b_tonegen* t)
{
	assert (0);
	return 0;
}
void
notifyControlChangeByName (void* mcfg, const char* cfname, unsigned char val)
{
}
void
useMIDIControlFunction (void* m, const char* cfname, void (*f) (void* d, unsigned char), void* d)
{
}
int
getConfigParameter_dr (const char* par, ConfigContext* cfg, double* dp, double lowInc, double highInc)
{
	return 0;
}
int
getConfigParameter_d (const char* par, ConfigContext* cfg, double* dp)
{
	return 0;
}
