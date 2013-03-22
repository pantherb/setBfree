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
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

/* LV2 */
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/ext/atom/util.h"
#include "lv2/lv2plug.in/ns/ext/event/event-helpers.h"

#include "uris.h"

#include "global_inst.h"
#include "vibrato.h"
#include "main.h"
#include "midi.h"
#include "cfgParser.h"
#include "pgmParser.h"
#include "program.h"

#define BUFFER_SIZE_SAMPLES  (128)

typedef enum {
  B3S_CONTROL = 0,
  B3S_NOTIFY,
  B3S_MIDIIN,
  B3S_MIDIOUT,
  B3S_OUTL,
  B3S_OUTR
} PortIndex;

typedef struct {
  LV2_Atom_Forge notify_forge;
  LV2_Atom_Forge_Frame notify_frame;
  int suspend_ui_msg;

  const LV2_Atom_Sequence* midiin;
  LV2_Atom_Sequence* midiout;
  const LV2_Atom_Sequence* control_port;
  LV2_Atom_Sequence*       notify_port;
  float* outL;
  float* outR;

  setBfreeURIs uris;
  struct b_instance inst;

  int   boffset;
  float bufA [BUFFER_SIZE_SAMPLES];
  float bufB [BUFFER_SIZE_SAMPLES];
  float bufC [BUFFER_SIZE_SAMPLES];
  float bufJ [2][BUFFER_SIZE_SAMPLES];
} B3S;

/* main synth wrappers */

const ConfigDoc *mainDoc () { return NULL;}
int mainConfig (ConfigContext * cfg) { return 0; }

double SampleRateD = 48000.0;

void initSynth(B3S *b3s, double rate) {
  // equicalent to ../src/main.c main()
  unsigned int defaultPreset[9] = {8,8,6, 0,0,0,0, 0,0};

  srand ((unsigned int) time (NULL));
  initControllerTable (b3s->inst.midicfg);
#if 1
  midiPrimeControllerMapping (b3s->inst.midicfg);
#else // rg test midi-feedback
  parseConfigurationFile (&b3s->inst, "/home/rgareus/data/coding/setBfree/cfg/bcf2000.cfg");
#endif

  /* initAll() */
  initToneGenerator (b3s->inst.synth, b3s->inst.midicfg);
  initVibrato (b3s->inst.synth, b3s->inst.midicfg);
  initPreamp (b3s->inst.preamp, b3s->inst.midicfg);
  initReverb (b3s->inst.reverb, b3s->inst.midicfg, rate);
  initWhirl (b3s->inst.whirl, b3s->inst.midicfg, rate);
  /* end - initAll() */

  initMidiTables(b3s->inst.midicfg);

  setMIDINoteShift (b3s->inst.midicfg, 0);
  setDrawBars (&b3s->inst, 0, defaultPreset);
#if 0
  setDrawBars (&b3s->inst, 1, defaultPreset);
  setDrawBars (&b3s->inst, 2, defaultPreset);
#endif

#if 1
  setRevSelect (b3s->inst.whirl, WHIRL_SLOW);
#endif
}

#ifndef MIN
#define MIN(A,B) (((A)<(B))?(A):(B))
#endif

void synthSound (B3S *instance, uint32_t nframes, float **out) {
  B3S* b3s = (B3S*)instance;

  uint32_t written = 0;

  while (written < nframes) {
    int nremain = nframes - written;

    if (b3s->boffset >= BUFFER_SIZE_SAMPLES)  {
      b3s->boffset = 0;
      oscGenerateFragment (instance->inst.synth, b3s->bufA, BUFFER_SIZE_SAMPLES);
      preamp (instance->inst.preamp, b3s->bufA, b3s->bufB, BUFFER_SIZE_SAMPLES);
      reverb (instance->inst.reverb, b3s->bufB, b3s->bufC, BUFFER_SIZE_SAMPLES);
      whirlProc(instance->inst.whirl, b3s->bufA, b3s->bufJ[0], b3s->bufJ[1], BUFFER_SIZE_SAMPLES);
    }

    int nread = MIN(nremain, (BUFFER_SIZE_SAMPLES - b3s->boffset));

    memcpy(&out[0][written], &b3s->bufJ[0][b3s->boffset], nread*sizeof(float));
    memcpy(&out[1][written], &b3s->bufJ[1][b3s->boffset], nread*sizeof(float));

    written+=nread;
    b3s->boffset+=nread;
  }
}

void mctl_cb(int fnid, const char *fn, unsigned char val, midiCCmap *mm, void *arg) {
  B3S* b3s = (B3S*)arg;
#if 0
  printf("xfn: %d (\"%s\", %d)\n", fnid, fn, val);
#endif
  // TODO enqueue outgoing midi commands foreach mm;
  if (b3s->midiout && mm) {
    uint8_t msg[3];
    msg[0] = 0xb0 | (mm->channel&0x0f); // CONTROL_CHANGE
    msg[1] = mm->param;
    msg[2] = val;
    append_midi_event(&b3s->uris, b3s->midiout, 0, msg, 3);
  }
  // TODO notfiy UI -- unless change originates from UI
  if (b3s->notify_port && fn && !b3s->suspend_ui_msg) {
    write_cc_key_value(&b3s->notify_forge, &b3s->uris, fn, val);
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
  if(!b3s) {
    return NULL;
  }
  memset(b3s, 0, sizeof(B3S));

  SampleRateD = rate;

  int i;
  for (i=0; features[i]; ++i) {
    if (!strcmp(features[i]->URI, LV2_URID__map)) {
      LV2_URID_Map *urid_map = (LV2_URID_Map *) features[i]->data;
      if (urid_map) {
	map_setbfree_uris(urid_map, &b3s->uris);
	lv2_atom_forge_init(&b3s->notify_forge, urid_map);
        break;
      }
    }
  }
  // TODO fail if LV2_URID__map is N/A

  b3s->suspend_ui_msg = 1;

  b3s->inst.progs = allocProgs();
  b3s->inst.reverb = allocReverb();
  b3s->inst.whirl = allocWhirl();
  b3s->inst.midicfg = allocMidiCfg();
  setControlFunctionCallback(b3s->inst.midicfg, mctl_cb, b3s);
  b3s->inst.synth = allocTonegen();
  b3s->inst.preamp = allocPreamp();
  b3s->boffset = BUFFER_SIZE_SAMPLES;

  initSynth(b3s, rate);

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
      b3s->midiin = (const LV2_Atom_Sequence*)data;
      break;
    case B3S_MIDIOUT:
      b3s->midiout = (LV2_Atom_Sequence*)data;
      break;
    case B3S_OUTL:
      b3s->outL = (float*)data;
      break;
    case B3S_OUTR:
      b3s->outR = (float*)data;
      break;
    case B3S_CONTROL:
      b3s->control_port = (const LV2_Atom_Sequence*)data;
      break;
    case B3S_NOTIFY:
      b3s->notify_port = (LV2_Atom_Sequence*)data;
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

  /* prepare outgoing MIDI */
  clear_sequence(&b3s->uris, b3s->midiout);

  /* Set up forge to write directly to notify output port. */
  const uint32_t notify_capacity = b3s->notify_port->atom.size;
  lv2_atom_forge_set_buffer(&b3s->notify_forge,
                            (uint8_t*)b3s->notify_port,
                            notify_capacity);

  /* Start a sequence in the notify output port. */
  lv2_atom_forge_sequence_head(&b3s->notify_forge, &b3s->notify_frame, 0);

  /* Process incoming events from GUI */
  b3s->suspend_ui_msg = 1;
  LV2_ATOM_SEQUENCE_FOREACH(b3s->control_port, ev) {
      const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
      setBfreeURIs* uris = &b3s->uris;
      if (obj->body.otype == uris->sb3_uiinit) {
	// TODO request current config dump from setBfree
	// ..but setBfree does not support that, yet
	//
	// Solution 1: [probably best, needed anyway]
	//   setBfree will send callbacks during initialiazation
	//   load the synth in a worker-thread -> info received
	//   can be sent to the UI and MIDI devices
	// Solution 2: [nope]
	//   hack setBfree; function that parses ConfigDoc structs and extracts defaults
	// Solution 3: [mmh, might be needed to save/restore state]
	//   setBfree; add a getValue function for each ccFuncNames[]
	//   require  useMIDIControlFunction() to return a lookup-function
	//   implement lookup-function callbacks in all parts of setBfree that use CtrlFuns.
	//
#if 1
	/* hardcoded defaults - we don't read a cfg, so we can do that :) */
	write_cc_key_value(&b3s->notify_forge, uris, "upper.drawbar16", 0);
	write_cc_key_value(&b3s->notify_forge, uris, "upper.drawbar513", 0);
	write_cc_key_value(&b3s->notify_forge, uris, "upper.drawbar8", 32);
	write_cc_key_value(&b3s->notify_forge, uris, "upper.drawbar4", 127);
	write_cc_key_value(&b3s->notify_forge, uris, "upper.drawbar223", 127);
	write_cc_key_value(&b3s->notify_forge, uris, "upper.drawbar2", 127);
	write_cc_key_value(&b3s->notify_forge, uris, "upper.drawbar135", 127);
	write_cc_key_value(&b3s->notify_forge, uris, "upper.drawbar113", 127);
	write_cc_key_value(&b3s->notify_forge, uris, "upper.drawbar1", 127);
	write_cc_key_value(&b3s->notify_forge, uris, "lower.drawbar16", 0);
	write_cc_key_value(&b3s->notify_forge, uris, "lower.drawbar513", 80);
	write_cc_key_value(&b3s->notify_forge, uris, "lower.drawbar8", 0);
	write_cc_key_value(&b3s->notify_forge, uris, "lower.drawbar4", 127);
	write_cc_key_value(&b3s->notify_forge, uris, "lower.drawbar223", 127);
	write_cc_key_value(&b3s->notify_forge, uris, "lower.drawbar2", 127);
	write_cc_key_value(&b3s->notify_forge, uris, "lower.drawbar135", 127);
	write_cc_key_value(&b3s->notify_forge, uris, "lower.drawbar113", 127);
	write_cc_key_value(&b3s->notify_forge, uris, "lower.drawbar1", 127);
	write_cc_key_value(&b3s->notify_forge, uris, "pedal.drawbar16", 0);
	write_cc_key_value(&b3s->notify_forge, uris, "pedal.drawbar8", 32);
	// TODO add vibrato, percussion,...
#endif
      } else if (obj->body.otype == uris->sb3_control) {
	char *k; int v;
	if (!get_cc_key_value(uris, obj, &k, &v)) {
#if 0
	  printf("B3LV2: callMIDIControlFunction(..,\"%s\", %d);\n", k, v);
#endif
	  callMIDIControlFunction(b3s->inst.midicfg, k, v);
	} else {
	  printf("B3LV2: invalid control message\n");
	}
      } else {
	printf("B3LV2: non control message reveived on ctrl port\n");
      }
  }
  b3s->suspend_ui_msg = 0;

  // handle MIDI events
  if (b3s->midiin) {
    LV2_Atom_Event* ev = lv2_atom_sequence_begin(&(b3s->midiin)->body);
    // TODO - interleave events with oscGenerateFragment() -> sample accurate timing
    while(!lv2_atom_sequence_is_end(&(b3s->midiin)->body, (b3s->midiin)->atom.size, ev)) {
      if (ev->body.type == b3s->uris.midi_MidiEvent) {
	parse_raw_midi_data(&b3s->inst, (uint8_t*)(ev+1), ev->body.size);
      }
      ev = lv2_atom_sequence_next(ev);
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
  freeReverb(b3s->inst.reverb);
  freeWhirl(b3s->inst.whirl);
  freeToneGenerator(b3s->inst.synth);
  freeMidiCfg(b3s->inst.midicfg);
  freePreamp(b3s->inst.preamp);
  freeProgs(b3s->inst.progs);
  free(instance);
}

const void*
extension_data(const char* uri)
{
  return NULL;
}

static const LV2_Descriptor descriptor = {
  SB3_URI,
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
