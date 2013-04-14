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

#define _GNU_SOURCE

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
#include "lv2/lv2plug.in/ns/ext/worker/worker.h"

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
  LV2_Atom_Forge forge;
  LV2_Atom_Forge_Frame frame;
  LV2_Worker_Schedule* schedule;

  const LV2_Atom_Sequence* midiin;
  LV2_Atom_Sequence* midiout;
  float* outL;
  float* outR;

  LV2_URID_Map* map;
  setBfreeURIs uris;

  int   boffset;
  float bufA [BUFFER_SIZE_SAMPLES];
  float bufB [BUFFER_SIZE_SAMPLES];
  float bufC [BUFFER_SIZE_SAMPLES];
  float bufJ [2][BUFFER_SIZE_SAMPLES];

  short suspend_ui_msg;
  short update_gui_now;
  short update_pgm_now;
  short swap_instances;

  struct b_instance *inst;
  struct b_instance *inst_offline;
} B3S;

enum {
  CMD_FREE    = 0,
  CMD_LOADPGM = 1,
  CMD_LOADCFG = 2,
};

struct worknfo {
  int cmd;
  char msg[1024];
};

/* main synth wrappers */

const ConfigDoc *mainDoc () { return NULL;}

int mainConfig (ConfigContext * cfg) {
  if (strcasecmp (cfg->name, "midi.driver") == 0) {
    return 1;
  }
  else if (strcasecmp (cfg->name, "midi.port") == 0) {
    return 1;
  }
  else if (strcasecmp (cfg->name, "jack.connect") == 0) {
    return 1;
  }
  else if (strcasecmp (cfg->name, "jack.out.left") == 0) {
    return 1;
  }
  else if (strcasecmp (cfg->name, "jack.out.right") == 0) {
    return 1;
  }
  return 0;
}

double SampleRateD = 48000.0;

void initSynth(struct b_instance *inst, double rate) {
  // equivalent to ../src/main.c main()
  unsigned int defaultPreset[9] = {8,8,6, 0,0,0,0, 0,0};

  /* initAll() */
  initToneGenerator (inst->synth, inst->midicfg);
  initVibrato (inst->synth, inst->midicfg);
  initPreamp (inst->preamp, inst->midicfg);
  initReverb (inst->reverb, inst->midicfg, rate);
  initWhirl (inst->whirl, inst->midicfg, rate);
  initRunningConfig(inst->state, inst->midicfg);
  /* end - initAll() */

  initMidiTables(inst->midicfg);

  setMIDINoteShift (inst->midicfg, 0);
  setDrawBars (inst, 0, defaultPreset);
#if 0
  setDrawBars (inst, 1, defaultPreset);
  setDrawBars (inst, 2, defaultPreset);
#endif

#ifdef DEBUGPRINT
  if (walkProgrammes(inst->progs, 0)) {
    listProgrammes (inst->progs, stderr);
  }
  listCCAssignments(inst->midicfg, stderr);
#endif
}

static void
freeSynth(struct b_instance *inst)
{
  if (!inst) return;
  freeReverb(inst->reverb);
  freeWhirl(inst->whirl);
  freeToneGenerator(inst->synth);
  freeMidiCfg(inst->midicfg);
  freePreamp(inst->preamp);
  freeProgs(inst->progs);
  freeRunningConfig(inst->state);
}

#ifndef MIN
#define MIN(A,B) (((A)<(B))?(A):(B))
#endif

uint32_t synthSound (B3S *instance, uint32_t written, uint32_t nframes, float **out) {
  B3S* b3s = (B3S*)instance;

  while (written < nframes) {
    int nremain = nframes - written;

    if (b3s->boffset >= BUFFER_SIZE_SAMPLES)  {
      b3s->boffset = 0;
      oscGenerateFragment (instance->inst->synth, b3s->bufA, BUFFER_SIZE_SAMPLES);
      preamp (instance->inst->preamp, b3s->bufA, b3s->bufB, BUFFER_SIZE_SAMPLES);
      reverb (instance->inst->reverb, b3s->bufB, b3s->bufC, BUFFER_SIZE_SAMPLES);
      whirlProc(instance->inst->whirl, b3s->bufC, b3s->bufJ[0], b3s->bufJ[1], BUFFER_SIZE_SAMPLES);
    }

    int nread = MIN(nremain, (BUFFER_SIZE_SAMPLES - b3s->boffset));

    memcpy(&out[0][written], &b3s->bufJ[0][b3s->boffset], nread*sizeof(float));
    memcpy(&out[1][written], &b3s->bufJ[1][b3s->boffset], nread*sizeof(float));

    written+=nread;
    b3s->boffset+=nread;
  }
  return written;
}

static void mctl_cb(int fnid, const char *fn, unsigned char val, midiCCmap *mm, void *arg) {
  B3S* b3s = (B3S*)arg;
#ifdef DEBUGPRINT
  fprintf(stderr, "xfn: %d (\"%s\", %d) mm:%s\n", fnid, fn, val, mm?"yes":"no");
#endif
  if (b3s->midiout && mm) {
    while (mm) {
#ifdef DEBUGPRINT
      fprintf(stderr, "MIDI FEEDBACK %d %d %d\n", mm->channel, mm->param, val);
#endif
      uint8_t msg[3];
      msg[0] = 0xb0 | (mm->channel&0x0f); // Control Change
      msg[1] = mm->param;
      msg[2] = val;
      forge_midimessage(&b3s->forge, &b3s->uris, msg, 3);
      mm = mm->next;
    }
  }
  if (b3s->midiout && fn && !b3s->suspend_ui_msg) {
    forge_kvcontrolmessage(&b3s->forge, &b3s->uris, fn, (int32_t) val);
  }
}

static void rc_cb(int fnid, const char *key, const char *kv, unsigned char val, void *arg) {
  B3S* b3s = (B3S*)arg;
#ifdef DEBUGPRINT
      fprintf(stderr, "RC CB %d %s %s %d\n", fnid, key, kv?kv:"-", val);
#endif
  if (fnid >=0) {
    forge_kvcontrolmessage(&b3s->forge, &b3s->uris, key, (int32_t) val);
  }
}

static void pgm_cb(int num, int pc, const char *name, void *arg) {
  B3S* b3s = (B3S*)arg;
  char tmp[256];
  int pco = pc - b3s->inst->progs->MIDIControllerPgmOffset;
#ifdef DEBUGPRINT
      fprintf(stderr, "PGM CB %d %d %s\n",num, pc, name);
#endif
  LV2_Atom_Forge_Frame frame;
  lv2_atom_forge_frame_time(&b3s->forge, 0);
  lv2_atom_forge_blank(&b3s->forge, &frame, 1, b3s->uris.sb3_midipgm);

  lv2_atom_forge_property_head(&b3s->forge, b3s->uris.sb3_cckey, 0);
  lv2_atom_forge_int(&b3s->forge, pco);
  lv2_atom_forge_property_head(&b3s->forge, b3s->uris.sb3_ccval, 0);
  lv2_atom_forge_string(&b3s->forge, name, strlen(name));

  formatProgram(&b3s->inst->progs->programmes[pc], tmp, 256);
  lv2_atom_forge_property_head(&b3s->forge, b3s->uris.sb3_ccdsc, 0);
  lv2_atom_forge_string(&b3s->forge, tmp, strlen(tmp));

  lv2_atom_forge_pop(&b3s->forge, &frame);
}

static void mcc_cb(const char *fnname, const unsigned char chn, const unsigned char cc, const unsigned char flags, void *arg) {
  B3S* b3s = (B3S*)arg;
  char mmv[20];
  sprintf(mmv, "%d|%d ", chn, cc);

  LV2_Atom_Forge_Frame frame;
  lv2_atom_forge_frame_time(&b3s->forge, 0);
  lv2_atom_forge_blank(&b3s->forge, &frame, 1, b3s->uris.sb3_uimccset);

  lv2_atom_forge_property_head(&b3s->forge, b3s->uris.sb3_cckey, 0);
  lv2_atom_forge_string(&b3s->forge, fnname, strlen(fnname));
  lv2_atom_forge_property_head(&b3s->forge, b3s->uris.sb3_ccval, 0);
  lv2_atom_forge_string(&b3s->forge, mmv, strlen(mmv));
  lv2_atom_forge_pop(&b3s->forge, &frame);
}

void allocSynth(struct b_instance *inst) {
  inst->state = allocRunningConfig();
  inst->progs = allocProgs();
  inst->reverb = allocReverb();
  inst->whirl = allocWhirl();
  inst->midicfg = allocMidiCfg(inst->state);
  inst->synth = allocTonegen();
  inst->preamp = allocPreamp();

  initControllerTable (inst->midicfg);
#if 1
  midiPrimeControllerMapping (inst->midicfg);
#elif 0 // rg test midi-feedback
  parseConfigurationFile (inst, "/home/rgareus/data/coding/setBfree/cfg/bcf2000.cfg");
#endif

}


/* LV2 -- state */
static void rcsave_cb(int fnid, const char *key, const char *kv, unsigned char val, void *arg) {
  char tmp[256];
  char **cfg = (char**)arg;
  if (fnid < 0) {
    sprintf(tmp, "C %s=%s\n", key, kv);
  } else {
    sprintf(tmp, "M %s=%d\n", key, val);
  }
  *cfg = realloc(*cfg, strlen(*cfg) + strlen(tmp) +1);
  strcat(*cfg, tmp);
}

static LV2_State_Status
save(LV2_Handle                instance,
     LV2_State_Store_Function  store,
     LV2_State_Handle          handle,
     uint32_t                  flags,
     const LV2_Feature* const* features)
{
  B3S* b3s = (B3S*)instance;

  char *cfg = calloc(1, sizeof(char));
  rc_loop_state(b3s->inst->state, rcsave_cb, (void*) &cfg);

  int i;
  size_t rs = 0;
  char *out = NULL;
  FILE *x = open_memstream(&out, &rs);
  for (i=0 ; i < 128; ++i) {
    int pgmNr = i + b3s->inst->progs->MIDIControllerPgmOffset;
    if (!(b3s->inst->progs->programmes[pgmNr].flags[0] & FL_INUSE)) {
      continue;
    }
    fprintf(x, "P ");
    writeProgramm(pgmNr, &b3s->inst->progs->programmes[pgmNr], " ", x);
  }
  fclose(x);
  cfg = realloc(cfg, strlen(cfg) + strlen(out) +1);
  strcat(cfg, out);

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

  if (!value) {
    return LV2_STATE_ERR_UNKNOWN;
  }

  if (b3s->inst_offline) {
    fprintf(stderr, "B3LV2: restore ignored. re-init in progress\n");
    return LV2_STATE_ERR_UNKNOWN;
  }

  b3s->inst_offline = calloc(1, sizeof(struct b_instance));
  allocSynth(b3s->inst_offline);

  const char* cfg = (const char*)value;
  const char *te, *ts = cfg;

  /* pass1 - evaulate CFG -- before initializing synth */
  while (ts && *ts && (te=strchr(ts, '\n'))) {
    char *val;
    char kv[1024];
    memcpy(kv, ts, te-ts);
    kv[te-ts]=0;
#ifdef DEBUGPRINT
    fprintf(stderr, "B3LV2 CFG Pass1: %s\n", kv);
#endif
    if(kv[0]=='C' && (val=strchr(kv,'='))) {
      *val=0;
#ifdef DEBUGPRINT
      fprintf(stderr, "B3LV2: evaluateConfigKeyValue(..,\"%s\", \"%s\");\n", kv+2, val+1);
#endif
      evaluateConfigKeyValue((void*)b3s->inst_offline, kv+2, val+1);
    }
    else if(kv[0]=='P') {
#ifdef DEBUGPRINT
      printf("PGM '%s'\n", kv+2);
#endif
      loadProgrammeString(b3s->inst_offline->progs, kv+2);
    }
    ts=te+1;
  }

  initSynth(b3s->inst_offline, SampleRateD);

  /* pass2 - replay CC's after initializing synth */
  ts = cfg;
  while (ts && *ts && (te=strchr(ts, '\n'))) {
    char *val;
    char kv[1024];
    memcpy(kv, ts, te-ts);
    kv[te-ts]=0;
#ifdef DEBUGPRINT
    fprintf(stderr, "B3LV2 CFG Pass2: %s\n", kv);
#endif
    if(kv[0]=='M' && (val=strchr(kv,'='))) {
      *val=0;
#ifdef DEBUGPRINT
      fprintf(stderr, "B3LV2: callMIDIControlFunction(..,\"%s\", %d);\n", kv+2, atoi(val+1));
#endif
      callMIDIControlFunction(b3s->inst_offline->midicfg, kv+2, atoi(val+1));
    }
    ts=te+1;
  }

  b3s->swap_instances = 1;
  return LV2_STATE_SUCCESS;
}

/* LV2 -- worker */
static LV2_Worker_Status
work(LV2_Handle                  instance,
     LV2_Worker_Respond_Function respond,
     LV2_Worker_Respond_Handle   handle,
     uint32_t                    size,
     const void*                 data)
{
  B3S* b3s = (B3S*)instance;

  if (size != sizeof(struct worknfo)) {
    return LV2_WORKER_ERR_UNKNOWN;
  }
  struct worknfo *w = (struct worknfo*) data;

  switch(w->cmd) {
    case CMD_LOADPGM:
      fprintf(stderr, "B3LV2: loading pgm file: %s\n", w->msg);
      if (!loadProgrammeFile(b3s->inst->progs, w->msg)) {
	b3s->update_pgm_now = 1;
      }
      break;
    case CMD_LOADCFG:
      if (b3s->inst_offline) {
	fprintf(stderr, "B3LV2: restore ignored. re-init in progress\n");
	return LV2_STATE_ERR_UNKNOWN;
      }
      b3s->inst_offline = calloc(1, sizeof(struct b_instance));
      allocSynth(b3s->inst_offline);
      parseConfigurationFile (b3s->inst_offline, w->msg);
      initSynth(b3s->inst_offline, SampleRateD);
      break;
    case CMD_FREE:
#ifdef DEBUGPRINT
      fprintf(stderr, "free offline instance\n");
#endif
      freeSynth(b3s->inst_offline);
      b3s->inst_offline = NULL;
    break;
  }

  return LV2_WORKER_SUCCESS;
}

static LV2_Worker_Status
work_response(LV2_Handle  instance,
              uint32_t    size,
              const void* data)
{
  B3S* b3s = (B3S*)instance;

  if (size != sizeof(struct worknfo)) {
    return LV2_WORKER_ERR_UNKNOWN;
  }

  struct worknfo *w = (struct worknfo*) data;

  switch(w->cmd) {
    case CMD_LOADPGM:
      break;
    case CMD_LOADCFG:
	b3s->swap_instances = 1;
      break;
    case CMD_FREE:
    break;
  }
  return LV2_WORKER_SUCCESS;
}

static inline void
postrun (B3S* b3s)
{
  if (b3s->swap_instances) {
#ifdef DEBUGPRINT
    fprintf(stderr, "swap instances..\n");
#endif
    struct worknfo w;
    w.cmd = CMD_FREE;
    /* swap engine instances */
    struct b_instance *old  = b3s->inst;
    b3s->inst = b3s->inst_offline;
    b3s->inst_offline = old;
    setControlFunctionCallback(b3s->inst_offline->midicfg, NULL, NULL);
    setControlFunctionCallback(b3s->inst->midicfg, mctl_cb, b3s);

    /* hide midi-maps, stop possibly pending midi-bind process */
    forge_kvcontrolmessage(&b3s->forge, &b3s->uris, "special.midimap", (int32_t) 0);

    b3s->schedule->schedule_work(b3s->schedule->handle, sizeof(struct worknfo), &w);
    b3s->update_gui_now = 1;
    b3s->swap_instances = 0;
  }
}

/* main LV2 */

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
      b3s->map = (LV2_URID_Map*)features[i]->data;
    } else if (!strcmp(features[i]->URI, LV2_WORKER__schedule)) {
      b3s->schedule = (LV2_Worker_Schedule*)features[i]->data;
    }
  }

  if (!b3s->map || !b3s->schedule) {
    fprintf(stderr, "B3Lv2 error: Host does not support urid:map or work:schedule\n");
    free(b3s);
    return NULL;
  }

  map_setbfree_uris(b3s->map, &b3s->uris);
  lv2_atom_forge_init(&b3s->forge, b3s->map);

  srand ((unsigned int) time (NULL));
  b3s->suspend_ui_msg = 1;
  b3s->boffset = BUFFER_SIZE_SAMPLES;

  b3s->swap_instances = 0;
  b3s->update_gui_now = 0;
  b3s->update_pgm_now = 0;

  b3s->inst = calloc(1, sizeof(struct b_instance));
  b3s->inst_offline = NULL;

  allocSynth(b3s->inst);
  setControlFunctionCallback(b3s->inst->midicfg, mctl_cb, b3s);
  initSynth(b3s->inst, rate);

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
  const uint32_t capacity = b3s->midiout->atom.size;

  static bool warning_printed = false;
  if (!warning_printed && capacity < 4096) {
    warning_printed = true;
    fprintf(stderr, "B3LV2: LV message buffer is only %d bytes. Expect problems.\n", capacity);
    fprintf(stderr, "B3LV2: if your LV2 host allows to configure a buffersize use at least 4kBytes.\n");

  }
  lv2_atom_forge_set_buffer(&b3s->forge, (uint8_t*)b3s->midiout, capacity);
  lv2_atom_forge_sequence_head(&b3s->forge, &b3s->frame, 0);

  uint32_t written = 0;

  /* Process incoming events from GUI and handle MIDI events */
  if (b3s->midiin) {
    LV2_Atom_Event* ev = lv2_atom_sequence_begin(&(b3s->midiin)->body);
    while(!lv2_atom_sequence_is_end(&(b3s->midiin)->body, (b3s->midiin)->atom.size, ev)) {
      if (ev->body.type == b3s->uris.midi_MidiEvent) {
	/* process midi messages from player */
	if (written + BUFFER_SIZE_SAMPLES < ev->time.frames
	    && ev->time.frames < n_samples) {
	  /* first syntheize sound up until the message timestamp */
	  written = synthSound(instance, written, ev->time.frames, audio);
	}
	/* send midi message to synth, CC's will trigger hook -> update GUI */
	parse_raw_midi_data(b3s->inst, (uint8_t*)(ev+1), ev->body.size);
      } else if (ev->body.type == b3s->uris.atom_Blank) {
	/* process messages from GUI */
	const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
	if (obj->body.otype == b3s->uris.sb3_uiinit) {
	  b3s->update_gui_now = 1;
	} else if (obj->body.otype == b3s->uris.sb3_uimccquery) {
	  midi_loopCCAssignment(b3s->inst->midicfg, 7, mcc_cb, b3s);
	} else if (obj->body.otype == b3s->uris.sb3_uimccset) {
	  const LV2_Atom* key = NULL;
	  lv2_atom_object_get(obj, b3s->uris.sb3_cckey, &key, 0);
	  if (key) {
	    midi_uiassign_cc(b3s->inst->midicfg, (const char*)LV2_ATOM_BODY(key));
	  }
	} else if (obj->body.otype == b3s->uris.sb3_midipgm) {
	  const LV2_Atom* key = NULL;
	  lv2_atom_object_get(obj, b3s->uris.sb3_cckey, &key, 0);
	  if (key) {
	    installProgram(b3s->inst, ((LV2_Atom_Int*)key)->body);
	  }
	} else if (obj->body.otype == b3s->uris.sb3_midisavepgm) {
	  const LV2_Atom* pgm = NULL;
	  const LV2_Atom* name = NULL;
	  lv2_atom_object_get(obj, b3s->uris.sb3_cckey, &pgm, b3s->uris.sb3_ccval, &name, 0);
	  if (pgm && name) {
	    saveProgramm(b3s->inst, (int) ((LV2_Atom_Int*)pgm)->body, (char*) LV2_ATOM_BODY(name), 0);
	    b3s->update_pgm_now = 1;
	  }
	} else if (obj->body.otype == b3s->uris.sb3_loadpgm) {
	  const LV2_Atom* name = NULL;
	  lv2_atom_object_get(obj, b3s->uris.sb3_cckey, &name, 0);
	  if (name) {
	    struct worknfo w;
	    w.cmd = CMD_LOADPGM;
	    strncpy(w.msg, (char *)LV2_ATOM_BODY(name), 1024);
	    b3s->schedule->schedule_work(b3s->schedule->handle, sizeof(struct worknfo), &w);
	  }
	} else if (obj->body.otype == b3s->uris.sb3_loadcfg) {
	  const LV2_Atom* name = NULL;
	  lv2_atom_object_get(obj, b3s->uris.sb3_cckey, &name, 0);
	  if (name) {
	    struct worknfo w;
	    w.cmd = CMD_LOADCFG;
	    strncpy(w.msg, (char *)LV2_ATOM_BODY(name), 1024);
	    b3s->schedule->schedule_work(b3s->schedule->handle, sizeof(struct worknfo), &w);
	  }
	} else if (obj->body.otype == b3s->uris.sb3_control) {
	  b3s->suspend_ui_msg = 1;
	  const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
	  char *k; int v;
	  if (!get_cc_key_value(&b3s->uris, obj, &k, &v)) {
#ifdef DEBUGPRINT
	    fprintf(stderr, "B3LV2: callMIDIControlFunction(..,\"%s\", %d);\n", k, v);
#endif
	    callMIDIControlFunction(b3s->inst->midicfg, k, v);
	  }
	  b3s->suspend_ui_msg = 0;
	}
      }
      ev = lv2_atom_sequence_next(ev);
    }
  }

  if (b3s->update_gui_now) {
    b3s->update_gui_now = 0;
    b3s->update_pgm_now = 1;
    b3s->suspend_ui_msg = 1;
    rc_loop_state(b3s->inst->state, rc_cb, b3s);
    b3s->suspend_ui_msg = 0;
  } else if (b3s->update_pgm_now) {
    b3s->update_pgm_now = 0;
    loopProgammes(b3s->inst->progs, 1, pgm_cb, b3s);
  }

  /* synthesize [remaining] sound */
  synthSound(instance, written, n_samples, audio);

  /* check for new instances */
  postrun(b3s);
}

static void
cleanup(LV2_Handle instance)
{
  B3S* b3s = (B3S*)instance;
  freeSynth(b3s->inst);
  freeSynth(b3s->inst_offline);
  free(instance);
}

const void*
extension_data(const char* uri)
{
  static const LV2_Worker_Interface worker = { work, work_response, NULL };
  static const LV2_State_Interface  state  = { save, restore };
  if (!strcmp(uri, LV2_WORKER__interface)) {
    return &worker;
  }
  else if (!strcmp(uri, LV2_STATE__interface)) {
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
