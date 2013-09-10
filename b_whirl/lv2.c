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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "whirl.h"

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define B3W_URI "http://gareus.org/oss/lv2/b_whirl#simple"
#define B3W_URI_EXT "http://gareus.org/oss/lv2/b_whirl#extended"

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

typedef struct {
  float *input;
  float *outL;
  float *outR;

  float *rev_select;
  float *filta_type, *filtb_type, *filtd_type;
  float *filta_freq, *filtb_freq, *filtd_freq;
  float *filta_qual, *filtb_qual, *filtd_qual;
  float *filta_gain, *filtb_gain, *filtd_gain;

  float *horn_brake, *horn_accel, *horn_decel, *horn_slow, *horn_fast;
  float *drum_brake, *drum_accel, *drum_decel, *drum_slow, *drum_fast;

  float *horn_level, *drum_level;
  float *drum_width;

  float o_rev_select;
  float o_filta_type, o_filtb_type, o_filtd_type;
  float o_filta_freq, o_filtb_freq, o_filtd_freq;
  float o_filta_qual, o_filtb_qual, o_filtd_qual;
  float o_filta_gain, o_filtb_gain, o_filtd_gain;

  float o_horn_brake, o_horn_accel, o_horn_decel, o_horn_slow, o_horn_fast;
  float o_drum_brake, o_drum_accel, o_drum_decel, o_drum_slow, o_drum_fast;

  float o_horn_level, o_drum_level;
  float o_drum_width;

  struct b_whirl *instance;

  float bufH[2][8192];
  float bufD[2][8192];

  float _w;
} B3W;

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
  B3W* b3w = (B3W*)calloc(1,sizeof(B3W));
  if(!b3w) { return NULL ;}
  if (!(b3w->instance = allocWhirl())) {
    free(b3w);
    return NULL;
  }

  /* TODO: optional.
   * configure the plugin before initializing it
   * with alternate parameters or presets.
   *
   * call whirlConfig() // 28 parameters (!)
   * TODO:
   * allow to call initWhirl() with new whirlConfig()
   * parameters  during deactive/activate cycles..
   */
#if 0
  b3w->instance->hornRadiusCm = 19.2;
  b3w->instance->drumRadiusCm = 22.0;
  b3w->instance->micDistCm    = 42.0;
#endif

  initWhirl(b3w->instance, NULL, rate);

  b3w->_w = 2000.0 / rate;
  b3w->o_horn_level = 1.0;
  b3w->o_drum_level = 1.0;
  b3w->o_drum_width = 1.0;

  return (LV2_Handle)b3w;
}

static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
  B3W* b3w = (B3W*)instance;

  switch ((PortIndex)port) {
    case B3W_INPUT:
      b3w->input = (float*)data;
      break;
    case B3W_OUTL:
      b3w->outL = (float*)data;
      break;
    case B3W_OUTR:
      b3w->outR = (float*)data;
      break;
    case B3W_REVSELECT:
      b3w->rev_select = (float*)data;
      break;
    case B3W_FILTATYPE:
      b3w->filta_type = (float*)data;
      break;
    case B3W_FILTAFREQ:
      b3w->filta_freq = (float*)data;
      break;
    case B3W_FILTAQUAL:
      b3w->filta_qual = (float*)data;
      break;
    case B3W_FILTAGAIN:
      b3w->filta_gain = (float*)data;
      break;
    case B3W_FILTBTYPE:
      b3w->filtb_type = (float*)data;
      break;
    case B3W_FILTBFREQ:
      b3w->filtb_freq = (float*)data;
      break;
    case B3W_FILTBQUAL:
      b3w->filtb_qual = (float*)data;
      break;
    case B3W_FILTBGAIN:
      b3w->filtb_gain = (float*)data;
      break;
    case B3W_FILTDTYPE:
      b3w->filtd_type = (float*)data;
      break;
    case B3W_FILTDFREQ:
      b3w->filtd_freq = (float*)data;
      break;
    case B3W_FILTDQUAL:
      b3w->filtd_qual = (float*)data;
      break;
    case B3W_FILTDGAIN:
      b3w->filtd_gain = (float*)data;
      break;
    case B3W_HORNBRAKE:
      b3w->horn_brake = (float*)data;
      break;
    case B3W_HORNACCEL:
      b3w->horn_accel = (float*)data;
      break;
    case B3W_HORNDECEL:
      b3w->horn_decel = (float*)data;
      break;
    case B3W_HORNRPMSLOW:
      b3w->horn_slow = (float*)data;
      break;
    case B3W_HORNRPMFAST:
      b3w->horn_fast = (float*)data;
      break;
    case B3W_DRUMBRAKE:
      b3w->drum_brake = (float*)data;
      break;
    case B3W_DRUMACCEL:
      b3w->drum_accel = (float*)data;
      break;
    case B3W_DRUMDECEL:
      b3w->drum_decel = (float*)data;
      break;
    case B3W_DRUMRPMSLOW:
      b3w->drum_slow = (float*)data;
      break;
    case B3W_DRUMRPMFAST:
      b3w->drum_fast = (float*)data;
      break;
    case B3W_HORNLVL:
      b3w->horn_level = (float*)data;
      break;
    case B3W_DRUMLVL:
      b3w->drum_level = (float*)data;
      break;
    case B3W_DRUMWIDTH:
      b3w->drum_width = (float*)data;
      break;
  }
}

static void
activate(LV2_Handle instance)
{
}

#define SETPARAM(FN, NAME, PROC) \
  if (b3w->NAME) { \
    if (b3w->o_##NAME != *(b3w->NAME)) { \
      FN (b3w->instance, PROC (*(b3w->NAME))); \
      b3w->o_##NAME = *(b3w->NAME); \
    } \
  }

#define SETVALUE(VAR, NAME, PROC, FN) \
  if (b3w->NAME) { \
    if (b3w->o_##NAME != *(b3w->NAME)) { \
      b3w->instance->VAR = PROC (*(b3w->NAME)); \
      b3w->o_##NAME = *(b3w->NAME); \
      FN; \
    } \
  }

static inline float db_to_coefficient(const float d) {
  return powf(10.0f, 0.05f * d);
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
  B3W* b3w = (B3W*)instance;
  uint32_t i;

  const float* const input  = b3w->input;
  float* const       outL = b3w->outL;
  float* const       outR = b3w->outR;

  assert(n_samples <= 8192);

  SETPARAM(isetHornFilterAType, filta_type, (int) floorf)
  SETPARAM(fsetHornFilterAFrequency, filta_freq, )
  SETPARAM(fsetHornFilterAQ, filta_qual, )
  SETPARAM(fsetHornFilterAGain, filta_gain, )

  SETPARAM(isetHornFilterBType, filtb_type, (int) floorf)
  SETPARAM(fsetHornFilterBFrequency, filtb_freq, )
  SETPARAM(fsetHornFilterBQ, filtb_qual, )
  SETPARAM(fsetHornFilterBGain, filtb_gain, )

  SETPARAM(isetDrumFilterType, filtd_type, (int) floorf)
  SETPARAM(fsetDrumFilterFrequency, filtd_freq, )
  SETPARAM(fsetDrumFilterQ, filtd_qual, )
  SETPARAM(fsetDrumFilterGain, filtd_gain, )

  SETVALUE(hnBreakPos, horn_brake, (double), )
  SETVALUE(hornAcc, horn_accel, , )
  SETVALUE(hornDec, horn_decel, , )

  SETVALUE(hornRPMslow, horn_slow, , computeRotationSpeeds(b3w->instance); b3w->o_rev_select = -1;)
  SETVALUE(hornRPMfast, horn_fast, , computeRotationSpeeds(b3w->instance); b3w->o_rev_select = -1;)
  SETVALUE(drumRPMslow, drum_slow, , computeRotationSpeeds(b3w->instance); b3w->o_rev_select = -1;)
  SETVALUE(drumRPMfast, drum_fast, , computeRotationSpeeds(b3w->instance); b3w->o_rev_select = -1;)

  SETPARAM(useRevOption, rev_select, (int) floorf)

  SETVALUE(drBreakPos, drum_brake, (double), )
  SETVALUE(drumAcc, drum_accel, , )
  SETVALUE(drumDec, drum_decel, , )

  whirlProc2(b3w->instance, input, NULL, NULL, b3w->bufH[0], b3w->bufH[1], b3w->bufD[0], b3w->bufD[1], n_samples);

  const float hl = db_to_coefficient(*b3w->horn_level);
  const float dl = db_to_coefficient(*b3w->drum_level);
  const float dw = *b3w->drum_width;
  const float _w = b3w->_w; // TODO * pow n_samples

  b3w->o_horn_level += _w * (hl - b3w->o_horn_level) + 1e-15;
  b3w->o_drum_level += _w * (dl - b3w->o_drum_level) + 1e-15;
  b3w->o_drum_width += _w * (dw - b3w->o_drum_width) + 1e-15;

  const float dw2 = b3w->o_drum_width / 2.0;

  const float hll = b3w->o_horn_level;
  const float hrr = b3w->o_horn_level;
  const float dll = b3w->o_drum_level * (.5 + dw2);
  const float dlr = b3w->o_drum_level * (.5 - dw2);
  const float drl = b3w->o_drum_level * (.5 - dw2);
  const float drr = b3w->o_drum_level * (.5 + dw2);

  for (i=0; i < n_samples; ++i) {
    outL[i] = b3w->bufH[0][i] * hll + b3w->bufD[0][i] * dll + b3w->bufD[1][i] * dlr;
    outR[i] = b3w->bufH[1][i] * hrr + b3w->bufD[0][i] * drl + b3w->bufD[1][i] * drr;
  }
}

static void
deactivate(LV2_Handle instance)
{
}

static void
cleanup(LV2_Handle instance)
{
  B3W* b3w = (B3W*)instance;
  freeWhirl(b3w->instance);
  free(instance);
}

const void*
extension_data(const char* uri)
{
  return NULL;
}

static const LV2_Descriptor descriptor = {
  B3W_URI,
  instantiate,
  connect_port,
  activate,
  run,
  deactivate,
  cleanup,
  extension_data
};

static const LV2_Descriptor descriptorExt = {
  B3W_URI_EXT,
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
  case 1:
    return &descriptorExt;
  default:
    return NULL;
  }
}

void useMIDIControlFunction (void *m, char * cfname, void (* f) (void *d, unsigned char), void *d) { }
int getConfigParameter_dr (char * par, ConfigContext * cfg, double * dp, double lowInc, double highInc) { return 0; }
int getConfigParameter_d (char * par, ConfigContext * cfg, double * dp) { return 0; }
int getConfigParameter_ir (char * par, ConfigContext * cfg, int * ip, int lowInc, int highInc) { return 0; }
int getConfigParameter_i (char * par, ConfigContext * cfg, int * ip) { return 0; }
void notifyControlChangeByName (void *mcfg, char * cfname, unsigned char val) { }
/* vi:set ts=8 sts=2 sw=2: */
