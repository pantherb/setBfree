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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
 */

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "vibrato.h"
#include "reverb.h"
#include "main.h"
#include "midi.h"
#include "cfgParser.h"
#include "pgmParser.h"
#include "whirl.h"
#include "tonegen.h"
#include "program.h"
#include "overdrive.h"

/* LV2 */

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/event/event-helpers.h"

#define B3S_URI "http://gareus.org/oss/lv2/b_synth"

typedef enum {
  B3S_MIDIIN   = 0,
  B3S_OUTL     = 1,
  B3S_OUTR     = 2,
} PortIndex;

typedef struct {
  LV2_Event_Buffer* midiin;
  float* outL;
  float* outR;
  uint32_t event_id;

  struct b_reverb *inst_reverb;
} B3S;

/* main synth wrappers */

const ConfigDoc *mainDoc () { return NULL;}
int mainConfig (ConfigContext * cfg) { return 0; }


double SampleRateD = 48000.0;
int SampleRateI = 48000;

//temp global -- src/cfgParser.c, src/program.c
struct b_reverb *inst_reverb = NULL;

void initSynth(B3S *b3s) {
  // equicalent to ../src/main.c main()
  unsigned int defaultPreset[9] = {8,8,8, 0,0,0,0, 0,0};

  srand ((unsigned int) time (NULL));
  midiPrimeControllerMapping ();

  /* initAll() */
  initVibrato ();
  initToneGenerator ();
  initPreamp ();
  initReverb (b3s->inst_reverb);
  initWhirl ();
  /* end - initAll() */

  initMidiTables();

  setMIDINoteShift (0);
  setDrawBars (0, defaultPreset);
  setDrawBars (1, defaultPreset);
  setDrawBars (2, defaultPreset);

#if 1
  setRevSelect (WHIRL_SLOW);
#endif
}

#ifndef MIN
#define MIN(A,B) (((A)<(B))?(A):(B))
#endif

#define BUFFER_SIZE_SAMPLES  (128)
static float bufA [BUFFER_SIZE_SAMPLES];
static float bufB [BUFFER_SIZE_SAMPLES];
static float bufC [BUFFER_SIZE_SAMPLES];
static float bufJ [2][BUFFER_SIZE_SAMPLES];


void synthSound (B3S *instance, uint32_t nframes, float **out) {
  static int boffset = BUFFER_SIZE_SAMPLES;

  jack_nframes_t written = 0;

  while (written < nframes) {
    int nremain = nframes - written;

    if (boffset >= BUFFER_SIZE_SAMPLES)  {
      boffset = 0;
      oscGenerateFragment (bufA, BUFFER_SIZE_SAMPLES);
      preamp (bufA, bufB, BUFFER_SIZE_SAMPLES);
      reverb (instance->inst_reverb, bufB, bufC, BUFFER_SIZE_SAMPLES);
      whirlProc(bufC, bufJ[0], bufJ[1], BUFFER_SIZE_SAMPLES);
    }

    int nread = MIN(nremain, (BUFFER_SIZE_SAMPLES - boffset));

    memcpy(&out[0][written], &bufJ[0][boffset], nread*sizeof(float));
    memcpy(&out[1][written], &bufJ[1][boffset], nread*sizeof(float));

    written+=nread;
    boffset+=nread;
  }
}

/* LV2 */

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
  B3S* b3s = (B3S*)malloc(sizeof(B3S));
  b3s->event_id=0;
  b3s->midiin=NULL;

  SampleRateD = rate;
  SampleRateI = (int) rate;

  int i;
  for (i=0; features[i]; ++i) {
    if (!strcmp(features[i]->URI, LV2_URID__map)) {
      LV2_URID_Map *urid_map = (LV2_URID_Map *) features[i]->data;
      if (urid_map) {
        b3s->event_id = urid_map->map(urid_map->handle, LV2_MIDI__MidiEvent);
        break;
      }
    }
  }
  b3s->inst_reverb = allocReverb();
  inst_reverb = b3s->inst_reverb; // XXX temp. until src/*.c is updated.

  initSynth(b3s);

  return (LV2_Handle)b3s;
}

static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
  B3S* b3s = (B3S*)instance;

  switch ((PortIndex)port) {
    case B3S_MIDIIN:
      b3s->midiin = (LV2_Event_Buffer*)data;
      break;
    case B3S_OUTL:
      b3s->outL = (float*)data;
      break;
    case B3S_OUTR:
      b3s->outR = (float*)data;
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
  B3S* b3s = (B3S*)instance;
  float* audio[2];

  audio[0] = b3s->outL;
  audio[1] = b3s->outR;

  // handle midi events
  if (b3s->midiin) {
    LV2_Event_Iterator iter;
    lv2_event_begin(&iter, b3s->midiin);
    while (lv2_event_is_valid(&iter)) {
      uint8_t   *data;
      LV2_Event *event = lv2_event_get(&iter, &data);
      if (event && (b3s->event_id == 0 || event->type == b3s->event_id)) {
	parse_lv2_midi_event(data, event->size);
      }
      lv2_event_increment(&iter);
    }
  }

  // synthesize sound
  synthSound(instance, n_samples, audio);
}

static void
deactivate(LV2_Handle instance)
{
}

static void
cleanup(LV2_Handle instance)
{
  B3S* b3s = (B3S*)instance;
  freeReverb(b3s->inst_reverb);
  freeToneGenerator();
  free(instance);
}

const void*
extension_data(const char* uri)
{
  return NULL;
}

static const LV2_Descriptor descriptor = {
  B3S_URI,
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
/* vi:set ts=8 sts=2 sw=2: */
