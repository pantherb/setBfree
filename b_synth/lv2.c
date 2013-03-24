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
#include "lv2/lv2plug.in/ns/ext/state/state.h"

#include "uris.h"

#include "global_inst.h"
#include "vibrato.h"
#include "main.h"
#include "midi.h"
#include "state.h"
#include "cfgParser.h"
#include "pgmParser.h"
#include "program.h"

#define BUFFER_SIZE_SAMPLES  (128)

typedef enum {
  B3S_MIDIIN = 0,
  B3S_MIDIOUT,
  B3S_OUTL,
  B3S_OUTR
} PortIndex;

typedef struct {
  LV2_Atom_Forge notify_forge;
  int suspend_ui_msg;

  const LV2_Atom_Sequence* midiin;
  LV2_Atom_Sequence* midiout;
  float* outL;
  float* outR;

  setBfreeURIs uris;
  struct b_instance inst;

  int   boffset;
  float bufA [BUFFER_SIZE_SAMPLES];
  float bufB [BUFFER_SIZE_SAMPLES];
  float bufC [BUFFER_SIZE_SAMPLES];
  float bufJ [2][BUFFER_SIZE_SAMPLES];

  int update_gui_now;
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
  initRunningConfig(b3s->inst.state, b3s->inst.midicfg);
  /* end - initAll() */

  initMidiTables(b3s->inst.midicfg);

  setMIDINoteShift (b3s->inst.midicfg, 0);
  setDrawBars (&b3s->inst, 0, defaultPreset);
#if 0
  setDrawBars (&b3s->inst, 1, defaultPreset);
  setDrawBars (&b3s->inst, 2, defaultPreset);
#endif

#if 0
  if (walkProgrammes(b3s->inst.progs, 0)) {
    listProgrammes (b3s->inst.progs, stderr);
  }
  listCCAssignments(b3s->inst.midicfg, stderr);
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
      whirlProc(instance->inst.whirl, b3s->bufC, b3s->bufJ[0], b3s->bufJ[1], BUFFER_SIZE_SAMPLES);
    }

    int nread = MIN(nremain, (BUFFER_SIZE_SAMPLES - b3s->boffset));

    memcpy(&out[0][written], &b3s->bufJ[0][b3s->boffset], nread*sizeof(float));
    memcpy(&out[1][written], &b3s->bufJ[1][b3s->boffset], nread*sizeof(float));

    written+=nread;
    b3s->boffset+=nread;
  }
}

static void mctl_cb(int fnid, const char *fn, unsigned char val, midiCCmap *mm, void *arg) {
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
  if (b3s->midiout && fn && !b3s->suspend_ui_msg) {
    append_kv_event(&b3s->notify_forge, &b3s->uris, b3s->midiout, fn, val);
  }
}

static void rc_cb(int fnid, const char *fn, unsigned char val, void *arg) {
  B3S* b3s = (B3S*)arg;
  append_kv_event(&b3s->notify_forge, &b3s->uris, b3s->midiout, fn, val);
}

/* LV2 -- state */
static void rcsave_cb(int fnid, const char *fn, unsigned char val, void *arg) {
  char tmp[128];
  if (fnid < 0) return; // TODO cfg-eval
  sprintf(tmp, "M %s=%d\n", fn, val);
  strcat((char*)arg, tmp);
}

static LV2_State_Status
save(LV2_Handle                instance,
     LV2_State_Store_Function  store,
     LV2_State_Handle          handle,
     uint32_t                  flags,
     const LV2_Feature* const* features)
{
  B3S* b3s = (B3S*)instance;

  // property base-names are < 40 chars  -- see formatDoc()
  // cfg-properties may have variable postfixes, MIDICC's not
  // 5 additional bytes for "=NUM\n" and 2 for leading "ID"
  char *cfg = malloc(getCCFunctionCount() * 48 * sizeof(char));
  cfg[0] = '\0';
  rc_loop_state(b3s->inst.state, rcsave_cb, (void*) cfg);

  store(handle, b3s->uris.sb3_state,
	cfg, strlen(cfg) + 1,
	b3s->uris.atom_String,
	LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
  free(cfg);
  return LV2_STATE_SUCCESS;
}


static LV2_State_Status
restore(LV2_Handle                  instance,
        LV2_State_Retrieve_Function retrieve,
        LV2_State_Handle            handle,
        uint32_t                    flags,
        const LV2_Feature* const*   features)
{
  B3S* b3s = (B3S*)instance;

  size_t   size;
  uint32_t type;
  uint32_t valflags;
  const void* value = retrieve(handle, b3s->uris.sb3_state, &size, &type, &valflags);

  b3s->suspend_ui_msg = 1;
  if (value) {
    const char* cfg = (const char*)value;
    const char *te,*ts = cfg;
    while (ts && *ts && (te=strchr(ts, '\n'))) {
      char *val;
      char kv[1024];
      memcpy(kv, ts, te-ts);
      kv[te-ts]=0;
#if 0
      printf("CFG: %s\n", kv);
#endif
      if(kv[0]=='M' && (val=strchr(kv,'='))) {
        *val=0;
#if 0
	printf("B3LV2: callMIDIControlFunction(..,\"%s\", %d);\n", kv+2, atoi(val+1));
#endif
	callMIDIControlFunction(b3s->inst.midicfg, kv+2, atoi(val+1));
      }
      ts=te+1;
    }
  }
  b3s->update_gui_now = 1;
  b3s->suspend_ui_msg = 0;

  return LV2_STATE_SUCCESS;
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
  b3s->update_gui_now = 0;

  // todo check if any alloc fails
  b3s->inst.state = allocRunningConfig();
  b3s->inst.progs = allocProgs();
  b3s->inst.reverb = allocReverb();
  b3s->inst.whirl = allocWhirl();
  b3s->inst.midicfg = allocMidiCfg(b3s->inst.state);
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
  }
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

  /* Process incoming events from GUI and  handle MIDI events */
  if (b3s->midiin) {
    LV2_Atom_Event* ev = lv2_atom_sequence_begin(&(b3s->midiin)->body);
    // TODO - interleave events with oscGenerateFragment() -> sample accurate timing
    while(!lv2_atom_sequence_is_end(&(b3s->midiin)->body, (b3s->midiin)->atom.size, ev)) {
      if (ev->body.type == b3s->uris.midi_MidiEvent) {
	parse_raw_midi_data(&b3s->inst, (uint8_t*)(ev+1), ev->body.size);
      } else if (ev->body.type == b3s->uris.atom_Blank) {
	const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
	if (obj->body.otype == b3s->uris.sb3_uiinit) {
	  b3s->update_gui_now = 1;
	} else if (obj->body.otype == b3s->uris.sb3_control) {
	  b3s->suspend_ui_msg = 1;
	  const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
	  char *k; int v;
	  if (!get_cc_key_value(&b3s->uris, obj, &k, &v)) {
#if 0
	    printf("B3LV2: callMIDIControlFunction(..,\"%s\", %d);\n", k, v);
#endif
	    callMIDIControlFunction(b3s->inst.midicfg, k, v);
	  }
	  b3s->suspend_ui_msg = 0;
	}
      }
      ev = lv2_atom_sequence_next(ev);
    }
  }

  if (b3s->update_gui_now) {
    b3s->update_gui_now = 0;
    b3s->suspend_ui_msg = 1;
    rc_loop_state(b3s->inst.state, rc_cb, b3s);
    b3s->suspend_ui_msg = 0;
  }

  // synthesize sound
  synthSound(instance, n_samples, audio);
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
  freeRunningConfig(b3s->inst.state);
  free(instance);
}

const void*
extension_data(const char* uri)
{
  static const LV2_State_Interface  state  = { save, restore };
  if (!strcmp(uri, LV2_STATE__interface)) {
    return &state;
  }
  return NULL;
}

static const LV2_Descriptor descriptor = {
  SB3_URI,
  instantiate,
  connect_port,
  NULL,
  run,
  NULL,
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
