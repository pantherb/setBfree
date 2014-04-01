/* setBfree - DSP tonewheel organ
 *
 * Copyright (C) 2003-2004 Fredrik Kilander <fk@dsv.su.se>
 * Copyright (C) 2008-2012 Robin Gareus <robin@gareus.org>
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
#include <string.h>
#include "reverb.h"

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define B3R_URI "http://gareus.org/oss/lv2/b_reverb"

typedef enum {
  B3R_INPUT      = 0,
  B3R_OUTPUT     = 1,
  B3R_MIX        = 2,
  B3R_GAIN_IN    = 3,
  B3R_GAIN_OUT   = 4,
} PortIndex;

typedef struct {
  float* input;
  float* output;

  float* mix;
  float* gain_in;
  float* gain_out;
  struct b_reverb *instance;
} B3R;

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
  B3R* b3r = (B3R*)calloc(1, sizeof(B3R));
  if(!b3r) { return NULL ;}

  if (!(b3r->instance = allocReverb())) {
    free(b3r);
    return NULL;
  }

  initReverb(b3r->instance, NULL, rate);

  return (LV2_Handle)b3r;
}

static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
  B3R* b3r = (B3R*)instance;

  switch ((PortIndex)port) {
    case B3R_INPUT:
      b3r->input = (float*)data;
      break;
    case B3R_OUTPUT:
      b3r->output = (float*)data;
      break;
    case B3R_MIX:
      b3r->mix = (float*)data;
      break;
    case B3R_GAIN_IN:
      b3r->gain_in = (float*)data;
      break;
    case B3R_GAIN_OUT:
      b3r->gain_out = (float*)data;
      break;
  }
}

static void
activate(LV2_Handle instance)
{
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
  B3R* b3r = (B3R*)instance;

  const float* const input  = b3r->input;
  float* const       output = b3r->output;

  if(b3r->mix)
    setReverbMix (b3r->instance, *(b3r->mix));
  if(b3r->gain_in)
    setReverbInputGain (b3r->instance, *(b3r->gain_in));
  if(b3r->gain_out)
    setReverbOutputGain (b3r->instance, *(b3r->gain_out));
  reverb(b3r->instance, input, output, n_samples);
}

static void
deactivate(LV2_Handle instance)
{
}

static void
cleanup(LV2_Handle instance)
{
  B3R* b3r = (B3R*)instance;
  freeReverb(b3r->instance);
  free(instance);
}

const void*
extension_data(const char* uri)
{
  return NULL;
}

static const LV2_Descriptor descriptor = {
  B3R_URI,
  instantiate,
  connect_port,
  activate,
  run,
  deactivate,
  cleanup,
  extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
  switch (index) {
  case 0:
    return &descriptor;
  default:
    return NULL;
  }
}

void useMIDIControlFunction (void *m, const char * cfname, void (* f) (void *d, unsigned char), void *d) { }
int getConfigParameter_dr (const char * par, ConfigContext * cfg, double * dp, double lowInc, double highInc) { return 0; }
int getConfigParameter_d (const char * par, ConfigContext * cfg, double * dp) { return 0; }
/* vi:set ts=8 sts=2 sw=2: */
