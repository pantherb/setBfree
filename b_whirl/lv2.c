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
#include "whirl.h"

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define B3W_URI "http://gareus.org/oss/lv2/b_whirl"
#define B3W_URI_EXT "http://gareus.org/oss/lv2/b_whirlExt"

typedef enum {
  B3W_INPUT      = 0,
  B3W_OUTL       = 1,
  B3W_OUTR       = 2,
  B3W_REVSELECT  = 3,
  B3W_FILTATYPE  = 4,
  B3W_FILTAFREQ  = 5,
  B3W_FILTAQUAL  = 6,
  B3W_FILTAGAIN  = 7,
  B3W_FILTBTYPE  = 8,
  B3W_FILTBFREQ  = 9,
  B3W_FILTBQUAL  =10,
  B3W_FILTBGAIN  =11,

  B3W_HORNBREAK  =12,
  B3W_HORNACCEL  =13,
  B3W_HORNDECEL  =14,

  B3W_DRUMBREAK  =15,
  B3W_DRUMACCEL  =16,
  B3W_DRUMDECEL  =17,
} PortIndex;

typedef struct {
  float *input;
  float *outL;
  float *outR;

  float *rev_select;
  float *filta_type, *filtb_type;
  float *filta_freq, *filtb_freq;
  float *filta_qual, *filtb_qual;
  float *filta_gain, *filtb_gain;

  float *horn_break, *horn_accel, *horn_decel;
  float *drum_break, *drum_accel, *drum_decel;

  float o_rev_select;
  float o_filta_type, o_filtb_type;
  float o_filta_freq, o_filtb_freq;
  float o_filta_qual, o_filtb_qual;
  float o_filta_gain, o_filtb_gain;

  float o_horn_break, o_horn_accel, o_horn_decel;
  float o_drum_break, o_drum_accel, o_drum_decel;

  struct b_whirl *instance;
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

  initWhirl(b3w->instance, NULL, rate);

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
    case B3W_HORNBREAK:
      b3w->horn_break = (float*)data;
      break;
    case B3W_HORNACCEL:
      b3w->horn_accel = (float*)data;
      break;
    case B3W_HORNDECEL:
      b3w->horn_decel = (float*)data;
      break;
    case B3W_DRUMBREAK:
      b3w->drum_break = (float*)data;
      break;
    case B3W_DRUMACCEL:
      b3w->drum_accel = (float*)data;
      break;
    case B3W_DRUMDECEL:
      b3w->drum_decel = (float*)data;
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

#define SETVALUE(VAR, NAME, PROC) \
  if (b3w->NAME) { \
    if (b3w->o_##NAME != *(b3w->NAME)) { \
      b3w->instance->VAR = PROC (*(b3w->NAME)); \
      b3w->o_##NAME = *(b3w->NAME); \
    } \
  }

static void
run(LV2_Handle instance, uint32_t n_samples)
{
  B3W* b3w = (B3W*)instance;

  const float* const input  = b3w->input;
  float* const       outL = b3w->outL;
  float* const       outR = b3w->outR;

  SETPARAM(useRevOption, rev_select, (int) floorf)

  SETPARAM(isetHornFilterAType, filta_type, (int) floorf)
  SETPARAM(fsetHornFilterAFrequency, filta_freq, )
  SETPARAM(fsetHornFilterAQ, filta_qual, )
  SETPARAM(fsetHornFilterAGain, filta_gain, )

  SETPARAM(isetHornFilterBType, filtb_type, (int) floorf)
  SETPARAM(fsetHornFilterBFrequency, filtb_freq, )
  SETPARAM(fsetHornFilterBQ, filtb_qual, )
  SETPARAM(fsetHornFilterBGain, filtb_gain, )

  SETVALUE(hnBreakPos, horn_break, (double))
  SETVALUE(hornAcc, horn_accel, )
  SETVALUE(hornDec, horn_decel, )

  SETVALUE(drBreakPos, drum_break, (double))
  SETVALUE(drumAcc, drum_accel, )
  SETVALUE(drumDec, drum_decel, )

  whirlProc(b3w->instance, input, outL, outR, n_samples);
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
/* vi:set ts=8 sts=2 sw=2: */
