/* setBfree - DSP tonewheel organ
 *
 * Copyright (C) 2003-2004 Fredrik Kilander <fk@dsv.su.se>
 * Copyright (C) 2008-2015 Robin Gareus <robin@gareus.org>
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <locale.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifdef WITH_SIGNATURE // gpg signed verified binary
#include "gp3.h"
#include WITH_SIGNATURE // pubkey
#endif

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
#include "midnam_lv2.h"

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
  LV2_Midnam*          midnam;

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
  float bufD [2][BUFFER_SIZE_SAMPLES]; // drum, tmp.
  float bufL [2][BUFFER_SIZE_SAMPLES]; // leslie, out

  short suspend_ui_msg;
  short update_gui_now;
  short update_pgm_now;
  short swap_instances;
  short queue_panic;

  unsigned int active_keys [MAX_KEYS/32];

  struct b_instance *inst;
  struct b_instance *inst_offline;

  char   lv2nfo[128];
  uint32_t thirtysec;
  uint32_t counter;
  double   sin_phase;

} B3S;


enum {
  CMD_FREE    = 0,
  CMD_LOADPGM = 1,
  CMD_LOADCFG = 2,
  CMD_SAVEPGM = 3,
  CMD_SAVECFG = 4,
  CMD_SETCFG  = 5,
  CMD_PURGE   = 6,
  CMD_RESET   = 7,
};

struct worknfo {
  int cmd;
  int status;
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

#ifdef WITH_SIGNATURE
static void scramble (B3S *b3s, float * const buf, const size_t n_samples)
{
	if (b3s->thirtysec == 0) return;
	const uint32_t nc = b3s->counter + n_samples;
	float p;

	if (nc < b3s->thirtysec) {
		b3s->counter += n_samples;
		return;
	}
	if (nc > b3s->thirtysec + SampleRateD * 1) {
		const float f = 8284.f / SampleRateD;
		p = b3s->sin_phase;
		for (int i=0; i < n_samples; ++i, p += f) {
			const float g = i / (float)n_samples;
			buf[i] = buf[i] * g + .125f * (1.f - g) * sinf(p);
		}
		b3s->counter = 0;
		b3s->sin_phase = 0;
		return;
	} else if (b3s->counter >= b3s->thirtysec) {
		const float f = 8284.f / SampleRateD;
		p = b3s->sin_phase;
		for (int i=0; i < n_samples; ++i, p += f) {
			buf[i] = .125f * sinf(p);
		}
	} else {
		const float f = 8284.f / SampleRateD;
		p = b3s->sin_phase;
		for (int i=0; i < n_samples; ++i, p += f) {
			const float g = i / (float)n_samples;
			buf[i] = buf[i] * (1.f-g) + .125f * g * sinf(p);
		}
	}
	b3s->sin_phase = fmod(p, 2.0 * M_PI);
	b3s->counter += n_samples;
}
#endif

static uint32_t synthSound (B3S *instance, uint32_t written, uint32_t nframes, float **out) {
  B3S* b3s = (B3S*)instance;

  while (written < nframes) {
    int nremain = nframes - written;

    if (b3s->boffset >= BUFFER_SIZE_SAMPLES)  {
      b3s->boffset = 0;
      oscGenerateFragment (instance->inst->synth, b3s->bufA, BUFFER_SIZE_SAMPLES);
      preamp (instance->inst->preamp, b3s->bufA, b3s->bufB, BUFFER_SIZE_SAMPLES);
      reverb (instance->inst->reverb, b3s->bufB, b3s->bufC, BUFFER_SIZE_SAMPLES);
#ifdef WITH_SIGNATURE
      scramble (b3s, b3s->bufC, BUFFER_SIZE_SAMPLES);
#endif
      whirlProc3(instance->inst->whirl, b3s->bufC, b3s->bufL[0], b3s->bufL[1], b3s->bufD[0], b3s->bufD[1], BUFFER_SIZE_SAMPLES);

    }

    int nread = MIN(nremain, (BUFFER_SIZE_SAMPLES - b3s->boffset));

    memcpy(&out[0][written], &b3s->bufL[0][b3s->boffset], nread*sizeof(float));
    memcpy(&out[1][written], &b3s->bufL[1][b3s->boffset], nread*sizeof(float));

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
  if (b3s->midnam && fn && !strcmp (fn, "special.midimap")) {
    b3s->midnam->update (b3s->midnam->handle);
  }
}

static void rc_cb(int fnid, const char *key, const char *kv, unsigned char val, void *arg) {
  B3S* b3s = (B3S*)arg;
#ifdef DEBUGPRINT
      fprintf(stderr, "RC CB %d %s %s %d\n", fnid, key, kv?kv:"-", val);
#endif
  if (fnid >=0) {
    forge_kvcontrolmessage(&b3s->forge, &b3s->uris, key, (int32_t) val);
  } else {
    forge_kvconfigmessage(&b3s->forge, &b3s->uris, b3s->uris.sb3_cfgkv, key, kv);
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
  x_forge_object(&b3s->forge, &frame, 1, b3s->uris.sb3_midipgm);

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
  x_forge_object(&b3s->forge, &frame, 1, b3s->uris.sb3_uimccset);

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
#endif

}


/* LV2 -- state */
static void rcstate_cb(int fnid, const char *key, const char *kv, unsigned char val, void *arg) {
  char tmp[256];
  char **cfg = (char**)arg;
  if (fnid < 0) {
    sprintf(tmp, "C %s=%s\n", key, kv);
  } else {
    sprintf(tmp, "M %s=%d\n", key, val);
  }
  *cfg = (char*) realloc(*cfg, strlen(*cfg) + strlen(tmp) +1);
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

  LOCALEGUARD_START;

  char *cfg = (char*) calloc(1, sizeof(char));
  rc_loop_state(b3s->inst->state, rcstate_cb, (void*) &cfg);

  int i;
  size_t rs = 0;
  char *out = NULL;

#ifdef _WIN32
  char temppath[MAX_PATH - 13];
  char filename[MAX_PATH + 1];
  if (0 == GetTempPath(sizeof(temppath), temppath))
    return LV2_STATE_ERR_UNKNOWN;
  if (0 == GetTempFileName(temppath, "sbfstate", 0, filename))
    return LV2_STATE_ERR_UNKNOWN;
  FILE *x = fopen(filename, "w+b");
#else // POSIX
  FILE *x = open_memstream(&out, &rs);
#endif

  for (i=0 ; i < 128; ++i) {
    int pgmNr = i + b3s->inst->progs->MIDIControllerPgmOffset;
    if (!(b3s->inst->progs->programmes[pgmNr].flags[0] & FL_INUSE)) {
      continue;
    }
    fprintf(x, "P ");
    writeProgramm(pgmNr, &b3s->inst->progs->programmes[pgmNr], " ", x);
  }
  fclose(x);

#ifdef _WIN32
  x = fopen(filename, "rb");
  fseek (x , 0 , SEEK_END);
  long int rsize = ftell (x);
  rewind(x);
  out = (char*) malloc(rsize);
  fread(out, sizeof(char), rsize, x);
  fclose(x);
  unlink(filename);
#endif

  cfg = (char*) realloc(cfg, strlen(cfg) + strlen(out) +1);
  strcat(cfg, out);

  LOCALEGUARD_END;

  store(handle, b3s->uris.sb3_state,
      cfg, strlen(cfg) + 1,
      b3s->uris.atom_String,
      LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
  free(cfg);
  free(out);
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

  b3s->inst_offline = (b_instance*) calloc(1, sizeof(struct b_instance));
  allocSynth(b3s->inst_offline);

  const char* cfg = (const char*)value;
  const char *te, *ts = cfg;

  LOCALEGUARD_START;

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

  LOCALEGUARD_END;

  b3s->swap_instances = 1;
  return LV2_STATE_SUCCESS;
}

static void rcsave_cb(int fnid, const char *key, const char *kv, unsigned char val, void *arg) {
  if (fnid < 0) {
    fprintf((FILE*)arg, "%s=%s\n", key, kv);
  }
}

static void clone_cb_cfg(int fnid, const char *key, const char *kv, unsigned char val, void *arg) {
  if (fnid < 0) {
    evaluateConfigKeyValue((struct b_instance *) arg, key, kv);
  }
}

static void clone_map_cb(int fnid, const char *key, const char *kv, unsigned char val, void *arg) {
  if (fnid < 0 && !strncmp(key, "midi.controller.", 16)) {
    evaluateConfigKeyValue((struct b_instance *) arg, key, kv);
  }
}

static void clone_cb_mcc(int fnid, const char *key, const char *kv, unsigned char val, void *arg) {
  if (fnid >= 0) {
    callMIDIControlFunction(((struct b_instance *) arg)->midicfg, key, val);
  }
}

#ifdef _WIN32
#define DIRSEP_C '\\'
#else
#define DIRSEP_C '/'
#endif


static void create_containing_dir (const char *path) {
  size_t len = strlen(path);
  if (!path || len == 0) return;
  if(path[len - 1] == DIRSEP_C) {
    return;
  }

  char *tmp = strdup(path);
  char *p = tmp + 1;
#ifdef _WIN32
  if (len > 3 && tmp[1] == ':' && tmp[2] == '\\') {
    p = tmp + 3;
  }
#endif
  for(; *p; ++p)
    if(*p == DIRSEP_C) {
      *p = 0;
#ifdef _WIN32
      _mkdir(tmp);
#else
      mkdir(tmp, 0755);
#endif
      *p = DIRSEP_C;
    }
  free (tmp);
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
  FILE *x;

  if (size != sizeof(struct worknfo)) {
    return LV2_WORKER_ERR_UNKNOWN;
  }
  struct worknfo *w = (struct worknfo*) data;

  switch(w->cmd) {
    case CMD_PURGE:
      if (b3s->inst_offline) {
	// this should not happen
	fprintf(stderr, "B3LV2: re-init in progress\n");
	w->status = -1;
      } else {
	fprintf(stderr, "B3LV2: reinitialize\n");
	b3s->inst_offline = (b_instance*) calloc(1, sizeof(struct b_instance));
	allocSynth(b3s->inst_offline);
	// clone midi map only
	rc_loop_state(b3s->inst->state, clone_map_cb, b3s->inst_offline);
	// copy program info
	memcpy(b3s->inst_offline->progs,  b3s->inst->progs, sizeof(struct b_programme));
	initSynth(b3s->inst_offline, SampleRateD);
	// replay CCs after synth init
	rc_loop_state(b3s->inst->state, clone_cb_mcc, b3s->inst_offline);
	w->status = 0;
      }
      break;
    case CMD_RESET:
      if (b3s->inst_offline) {
	// this should not happen
	fprintf(stderr, "B3LV2: reset ignored. re-init in progress\n");
	w->status = -1;
      } else {
	fprintf(stderr, "B3LV2: factory reset\n");
	b3s->inst_offline = (b_instance*) calloc(1, sizeof(struct b_instance));
	allocSynth(b3s->inst_offline);
	initSynth(b3s->inst_offline, SampleRateD);
	w->status = 0;
      }
      break;
    case CMD_SETCFG:
      if (b3s->inst_offline) {
	// this should not happen
	fprintf(stderr, "B3LV2: setcfg ignored. re-init in progress\n");
	w->status = -1;
      } else {
	// fprintf(stderr, "B3LV2: adding cfg line: %s\n", w->msg);
	b3s->inst_offline = (b_instance*) calloc(1, sizeof(struct b_instance));
	allocSynth(b3s->inst_offline);
	LOCALEGUARD_START;
	// clone current state...
	rc_loop_state(b3s->inst->state, clone_cb_cfg, b3s->inst_offline);
	// copy program info
	memcpy(b3s->inst_offline->progs,  b3s->inst->progs, sizeof(struct b_programme));
	// add user-config
	parseConfigurationLine (b3s->inst_offline, "LV2", 0, w->msg);
	initSynth(b3s->inst_offline, SampleRateD);
	// replay CCs after synth init
	rc_loop_state(b3s->inst->state, clone_cb_mcc, b3s->inst_offline);
	LOCALEGUARD_END;
	w->status = 0;
      }
      break;
    case CMD_LOADPGM:
      fprintf(stderr, "B3LV2: loading pgm file: %s\n", w->msg);
      if (!(w->status=loadProgrammeFile(b3s->inst->progs, w->msg))) {
	b3s->update_pgm_now = 1;
      }
      break;
    case CMD_LOADCFG:
      if (b3s->inst_offline) {
	fprintf(stderr, "B3LV2: restore ignored. re-init in progress\n");
	return LV2_WORKER_ERR_UNKNOWN;
      }
      fprintf(stderr, "B3LV2: loading cfg file: %s\n", w->msg);
      b3s->inst_offline = (b_instance*) calloc(1, sizeof(struct b_instance));
      allocSynth(b3s->inst_offline);

      w->status = parseConfigurationFile (b3s->inst_offline, w->msg);
      initSynth(b3s->inst_offline, SampleRateD);
      break;
    case CMD_SAVECFG:
      create_containing_dir (w->msg);
      x = fopen(w->msg, "w");
      if (x) {
	fprintf(x, "# setBfree config file\n# modificaions on top of default config\n");
	LOCALEGUARD_START;
	rc_loop_state(b3s->inst->state, rcsave_cb, (void*) x);
	LOCALEGUARD_END;
	fclose(x);
	w->status = 0;
      } else {
	w->status = -1;
      }
      break;
    case CMD_SAVEPGM:
      create_containing_dir (w->msg);
      x = fopen(w->msg, "w");
      if (x) {
	fprintf(x, "# setBfree midi program file\n");
	int i;
	for (i=0 ; i < 128; ++i) {
	  int pgmNr = i + b3s->inst->progs->MIDIControllerPgmOffset;
	  if (!(b3s->inst->progs->programmes[pgmNr].flags[0] & FL_INUSE)) {
	    continue;
	  }
	  writeProgramm(pgmNr, &b3s->inst->progs->programmes[pgmNr], "\n    ", x);
	}
	fclose(x);
	w->status = 0;
      } else {
	w->status = -1;
      }
      break;
    case CMD_FREE:
#ifdef DEBUGPRINT
      fprintf(stderr, "free offline instance\n");
#endif
      freeSynth(b3s->inst_offline);
      b3s->inst_offline = NULL;
    break;
  }

  respond(handle, sizeof(struct worknfo), data);
  return LV2_WORKER_SUCCESS;
}

static void forge_message_str(B3S *b3s, LV2_URID uri, const char *msg) {
  LV2_Atom_Forge_Frame frame;
  lv2_atom_forge_frame_time(&b3s->forge, 0);
  x_forge_object(&b3s->forge, &frame, 1, uri);
  lv2_atom_forge_property_head(&b3s->forge, b3s->uris.sb3_uimsg, 0);
  lv2_atom_forge_string(&b3s->forge, msg, strlen(msg));
  lv2_atom_forge_pop(&b3s->forge, &frame);
}

static LV2_Worker_Status
work_response(LV2_Handle  instance,
              uint32_t    size,
              const void* data)
{
  B3S* b3s = (B3S*)instance;
  char tmp[1048];

  if (size != sizeof(struct worknfo)) {
    return LV2_WORKER_ERR_UNKNOWN;
  }

  struct worknfo *w = (struct worknfo*) data;

  switch(w->cmd) {
    case CMD_PURGE:
    case CMD_RESET:
      if (w->status){
	// this should not happen
	sprintf(tmp, "error modyfing CFG. Organ is busy.");
      } else {
	sprintf(tmp, "%s executed successfully.",
	    (w->cmd == CMD_RESET) ? "Factory-reset" : "Reconfigure");
	b3s->swap_instances = 1;
      }
      forge_message_str(b3s, b3s->uris.sb3_uimsg, tmp);
      break;
    case CMD_SETCFG:
      if (w->status){
	// this should not happen
	sprintf(tmp, "error modyfing CFG. Organ is busy.");
	forge_message_str(b3s, b3s->uris.sb3_uimsg, tmp);
      } else {
	b3s->swap_instances = 1;
      }
      break;
    case CMD_LOADCFG:
      b3s->swap_instances = 1;
      if (w->status)
	sprintf(tmp, "error loading CFG: '%s'", w->msg);
      else
	sprintf(tmp, "loaded CFG: '%s'", w->msg);
      forge_message_str(b3s, b3s->uris.sb3_uimsg, tmp);
      break;
    case CMD_LOADPGM:
      if (w->status)
	sprintf(tmp, "error loading PGM: '%s'", w->msg);
      else
	sprintf(tmp, "loaded PGM: '%s'", w->msg);
      forge_message_str(b3s, b3s->uris.sb3_uimsg, tmp);
      break;
    case CMD_SAVEPGM:
      if (w->status)
	sprintf(tmp, "error saving PGM: '%s'", w->msg);
      else
	sprintf(tmp, "saved PGM: '%s'", w->msg);
      forge_message_str(b3s, b3s->uris.sb3_uimsg, tmp);
      break;
    case CMD_SAVECFG:
      if (w->status)
	sprintf(tmp, "error saving CFG: '%s'", w->msg);
      else
	sprintf(tmp, "saved CFG: '%s'", w->msg);
      forge_message_str(b3s, b3s->uris.sb3_uimsg, tmp);
      break;
    break;
    default:
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
    forge_kvcontrolmessage(&b3s->forge, &b3s->uris, "special.reinit", (int32_t) 1);

    b3s->schedule->schedule_work(b3s->schedule->handle, sizeof(struct worknfo), &w);
    b3s->update_gui_now = 1;
    b3s->swap_instances = 0;
    if (b3s->midnam) {
      b3s->midnam->update (b3s->midnam->handle);
    }
  }
}

static void iowork(B3S* b3s, const LV2_Atom_Object* obj, int cmd) {
  const LV2_Atom* name = NULL;
  lv2_atom_object_get(obj, b3s->uris.sb3_cckey, &name, 0);
  if (name) {
    struct worknfo w;
    w.cmd = cmd;
    w.status = -1;;
    strncpy(w.msg, (char *)LV2_ATOM_BODY(name), 1024);
    b3s->schedule->schedule_work(b3s->schedule->handle, sizeof(struct worknfo), &w);
  }
}

static void advanced_config_set(B3S* b3s, const LV2_Atom_Object* obj) {
  const LV2_Atom* cfgline = NULL;
  lv2_atom_object_get(obj, b3s->uris.sb3_cckey, &cfgline, 0);
  if (cfgline) {
    struct worknfo w;
    char* msg = (char *)LV2_ATOM_BODY(cfgline);
    if(!strcmp("special.reset=1", msg)) {
      w.cmd = CMD_RESET;
    }
    else if(!strcmp("special.reconfigure=1", msg)) {
      w.cmd = CMD_PURGE;
    }
    else {
      w.cmd = CMD_SETCFG;
    }
    w.status = -1;
    strncpy(w.msg, msg, 1024);
    b3s->schedule->schedule_work(b3s->schedule->handle, sizeof(struct worknfo), &w);
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
    } else if (!strcmp (features[i]->URI, LV2_MIDNAM__update)) {
      b3s->midnam = (LV2_Midnam*)features[i]->data;
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
  b3s->queue_panic = 0;

  b3s->inst = (b_instance*) calloc(1, sizeof(struct b_instance));
  b3s->inst_offline = NULL;

  allocSynth(b3s->inst);

#ifdef JACK_DESCRIPT
  // CODE DUP src/main.c
  char * defaultConfigFile = NULL;
  char * defaultProgrammeFile = NULL;

#ifdef _WIN32
  char wintmp[1024] = "";
  if (ExpandEnvironmentStrings("%localappdata%\\setBfree\\default.cfg", wintmp, 1024)) {
    defaultConfigFile = strdup (wintmp);
  }
  wintmp[0] = '\0';
  if (ExpandEnvironmentStrings("%localappdata%\\setBfree\\default.pgm", wintmp, 1024)) {
    defaultProgrammeFile = strdup (wintmp);
  }
#else // unices: prefer XDG_CONFIG_HOME
  if (getenv("XDG_CONFIG_HOME")) {
    size_t hl = strlen(getenv("XDG_CONFIG_HOME"));
    defaultConfigFile=(char*) malloc(hl+22);
    defaultProgrammeFile=(char*) malloc(hl+22);
    sprintf(defaultConfigFile,    "%s/setBfree/default.cfg", getenv("XDG_CONFIG_HOME"));
    sprintf(defaultProgrammeFile, "%s/setBfree/default.pgm", getenv("XDG_CONFIG_HOME"));
  }
  else if (getenv("HOME")) {
    size_t hl = strlen(getenv("HOME"));
# ifdef __APPLE__
    defaultConfigFile=(char*) malloc(hl+42);
    defaultProgrammeFile=(char*) malloc(hl+42);
    sprintf(defaultConfigFile,    "%s/Library/Preferences/setBfree/default.cfg", getenv("HOME"));
    sprintf(defaultProgrammeFile, "%s/Library/Preferences/setBfree/default.pgm", getenv("HOME"));
# else // linux, BSD, etc
    defaultConfigFile=(char*) malloc(hl+30);
    defaultProgrammeFile=(char*) malloc(hl+30);
    sprintf(defaultConfigFile,    "%s/.config/setBfree/default.cfg", getenv("HOME"));
    sprintf(defaultProgrammeFile, "%s/.config/setBfree/default.pgm", getenv("HOME"));
# endif
  }
#endif

  if (access (defaultConfigFile, R_OK) == 0) {
    parseConfigurationFile (b3s->inst, defaultConfigFile);
  }
#endif

  setControlFunctionCallback(b3s->inst->midicfg, mctl_cb, b3s);
  initSynth(b3s->inst, rate);

#ifdef JACK_DESCRIPT
  if (access (defaultProgrammeFile, R_OK) == 0) {
    loadProgrammeFile (b3s->inst->progs, defaultProgrammeFile);
  }
  free(defaultConfigFile);
  free(defaultProgrammeFile);
#endif

  strcpy(b3s->lv2nfo, "v" VERSION);
  b3s->thirtysec = 0;
#ifdef WITH_SIGNATURE
  {
    b3s->thirtysec = 30 * rate;
    b3s->counter = 0;
    b3s->sin_phase = 0;

    gp3_initialize ();
    load_master_key (); // in header WITH_SIGNATURE
    gp3_loglevel (GP3L_SILENT);
    int rc = -1;
    char signature_file0[1024] = "";
    char signature_file1[1024] = "";
#ifdef _WIN32
	ExpandEnvironmentStrings("%localappdata%\\"SIGFILE, signature_file0, 1024);
	ExpandEnvironmentStrings("%localappdata%\\x42_license.txt", signature_file1, 1024);
#else
	const char * home = getenv("HOME");
	if (home && (strlen(home) + strlen(SIGFILE) + 3) < 1024) {
		sprintf(signature_file0, "%s/.%s", home, SIGFILE);
	}
	if (home && (strlen(home) + 18) < 1024) {
		sprintf(signature_file1, "%s/.x42_license.txt", home);
	}
#endif
	if (!access(signature_file0, R_OK)) {
	  rc = gp3_checksigfile (signature_file0);
	} else if (!access(signature_file1, R_OK)) {
	  rc = gp3_checksigfile (signature_file1);
	}
	if (rc == 0) {
	  bool ok = false;
	  char data[8192];
	  char *tmp=NULL;
	  uint32_t len = gp3_get_text(data, sizeof(data));
	  if (len == sizeof(data)) data[sizeof(data)-1] = '\0';
	  else data[len] = '\0';
	  if ((tmp = strchr(data, '\n'))) *tmp = 0;
	  b3s->lv2nfo[sizeof(b3s->lv2nfo) - 1] = 0;
	  if (tmp++ && *tmp) {
	    if ((tmp = strstr(tmp, SB3_URI))) {
	      char *t1, *t2;
	      ok = true;
	      t1 = tmp + 1 + strlen(SB3_URI);
	      t2 = strchr(t1, '\n');
	      if (t2) { *t2 = 0; }
	      if (strlen(t1) > 0 && strncmp(t1, VERSION, strlen(t1))) {
	      ok = false;
	      }
	    }
	  }
	  if (ok) {
	    b3s->thirtysec = 0;
	    strncat(b3s->lv2nfo, " ", sizeof(b3s->lv2nfo) - strlen(b3s->lv2nfo));
	    strncat(b3s->lv2nfo, data, sizeof(b3s->lv2nfo) - strlen(b3s->lv2nfo));
	  }
	}
	gp3_cleanup ();
  }
#endif
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
activate(LV2_Handle instance)
{
  B3S* b3s = (B3S*)instance;
  b3s->queue_panic = 1;
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
  B3S* b3s = (B3S*)instance;
  float* audio[2];
  bool dirty = false;

  audio[0] = b3s->outL;
  audio[1] = b3s->outR;

  /* prepare outgoing MIDI */
  const uint32_t capacity = b3s->midiout->atom.size;

  static bool warning_printed = false;
  if (!warning_printed && capacity < 4096) {
    warning_printed = true;
    fprintf(stderr, "B3LV2: LV message buffer is only %d bytes. Expect problems.\n", capacity);
    fprintf(stderr, "B3LV2: if your LV2 host allows one to configure a buffersize use at least 4kBytes.\n");

  }
  lv2_atom_forge_set_buffer(&b3s->forge, (uint8_t*)b3s->midiout, capacity);
  lv2_atom_forge_sequence_head(&b3s->forge, &b3s->frame, 0);

  uint32_t written = 0;

  if (b3s->queue_panic) {
	  b3s->queue_panic = 0;
	  midi_panic(b3s->inst);
  }

  /* Process incoming events from GUI and handle MIDI events */
  if (b3s->midiin) {
    LV2_Atom_Event* ev = lv2_atom_sequence_begin(&(b3s->midiin)->body);
    while(!lv2_atom_sequence_is_end(&(b3s->midiin)->body, (b3s->midiin)->atom.size, ev)) {
      if (ev->body.type == b3s->uris.midi_MidiEvent) {
	/* process midi messages from player */
	if (written + BUFFER_SIZE_SAMPLES < ev->time.frames
	    && ev->time.frames < n_samples) {
	  /* first syntheize sound up until the message timestamp */
	  written = synthSound(b3s, written, ev->time.frames, audio);
	}
	/* send midi message to synth, CC's will trigger hook -> update GUI */
	parse_raw_midi_data(b3s->inst, (uint8_t*)(ev+1), ev->body.size);
      } else if (ev->body.type == b3s->uris.atom_Blank || ev->body.type == b3s->uris.atom_Object) {
	/* process messages from GUI */
	const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
	if (obj->body.otype == b3s->uris.sb3_uiinit) {
	  b3s->update_gui_now = 1;
	} else if (obj->body.otype == b3s->uris.sb3_uimccquery) {
	  midi_loopCCAssignment(b3s->inst->midicfg, 7, mcc_cb, b3s);
	} else if (obj->body.otype == b3s->uris.sb3_uimccset) {
	  const LV2_Atom* cmd = NULL;
	  const LV2_Atom* flags = NULL;
	  lv2_atom_object_get(obj, b3s->uris.sb3_cckey, &flags, b3s->uris.sb3_ccval, &cmd, 0);
	  if (cmd && flags) {
	    midi_uiassign_cc(b3s->inst->midicfg, (const char*)LV2_ATOM_BODY(cmd), ((LV2_Atom_Int*)flags)->body);
	    dirty = true;
	  }
	} else if (obj->body.otype == b3s->uris.sb3_midipgm) {
	  const LV2_Atom* key = NULL;
	  lv2_atom_object_get(obj, b3s->uris.sb3_cckey, &key, 0);
	  if (key) {
	    installProgram(b3s->inst, ((LV2_Atom_Int*)key)->body);
	    dirty = true;
	  }
	} else if (obj->body.otype == b3s->uris.sb3_midisavepgm) {
	  const LV2_Atom* pgm = NULL;
	  const LV2_Atom* name = NULL;
	  lv2_atom_object_get(obj, b3s->uris.sb3_cckey, &pgm, b3s->uris.sb3_ccval, &name, 0);
	  if (pgm && name) {
	    saveProgramm(b3s->inst, (int) ((LV2_Atom_Int*)pgm)->body, (char*) LV2_ATOM_BODY(name), 0);
	    b3s->update_pgm_now = 1;
	    if (b3s->midnam) {
	      b3s->midnam->update (b3s->midnam->handle);
	    }
	  }
	} else if (obj->body.otype == b3s->uris.sb3_loadpgm) {
	  iowork(b3s, obj, CMD_LOADPGM);
	} else if (obj->body.otype == b3s->uris.sb3_loadcfg) {
	  iowork(b3s, obj, CMD_LOADCFG);
	} else if (obj->body.otype == b3s->uris.sb3_savepgm) {
	  iowork(b3s, obj, CMD_SAVEPGM);
	} else if (obj->body.otype == b3s->uris.sb3_savecfg) {
	  iowork(b3s, obj, CMD_SAVECFG);
	} else if (obj->body.otype == b3s->uris.sb3_cfgstr) {
	  if (!b3s->inst_offline) {
	    advanced_config_set(b3s, obj);
	    dirty = true;
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
	    dirty = true;
	  }
	  b3s->suspend_ui_msg = 0;
	}
      }
      ev = lv2_atom_sequence_next(ev);
    }
  }

  /* synthesize [remaining] sound */
  synthSound(b3s, written, n_samples, audio);

  /* send active keys to GUI - IFF changed */
  bool keychanged = false;
  for (int i = 0 ; i < MAX_KEYS/32; ++i) {
    if (b3s->active_keys[i] != b3s->inst->synth->_activeKeys[i]) {
      keychanged = true;
    }
    b3s->active_keys[i] = b3s->inst->synth->_activeKeys[i];
  }

  if (dirty) {
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&b3s->forge, 0);
    x_forge_object(&b3s->forge, &frame, 1, b3s->uris.state_Changed);
    lv2_atom_forge_pop(&b3s->forge, &frame);
  }

  if (keychanged) {
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&b3s->forge, 0);
    x_forge_object(&b3s->forge, &frame, 1, b3s->uris.sb3_activekeys);
    lv2_atom_forge_property_head(&b3s->forge, b3s->uris.sb3_keyarrary, 0);
    lv2_atom_forge_vector(&b3s->forge, sizeof(unsigned int), b3s->uris.atom_Int, MAX_KEYS/32, b3s->active_keys);
    lv2_atom_forge_pop(&b3s->forge, &frame);
  }

  /* check for new instances */
  postrun(b3s);

  if (b3s->update_gui_now) {
    b3s->update_gui_now = 0;
    b3s->update_pgm_now = 1;
    b3s->suspend_ui_msg = 1;
    rc_loop_state(b3s->inst->state, rc_cb, b3s);
    b3s->suspend_ui_msg = 0;
    forge_kvconfigmessage(&b3s->forge, &b3s->uris, b3s->uris.sb3_cfgkv, "lv2.info", b3s->lv2nfo);
    forge_kvcontrolmessage(&b3s->forge, &b3s->uris, "special.init", (int32_t) b3s->thirtysec);
  } else if (b3s->update_pgm_now) {
    b3s->update_pgm_now = 0;
    loopProgammes(b3s->inst->progs, 1, pgm_cb, b3s);
  }
}

static void
cleanup(LV2_Handle instance)
{
  B3S* b3s = (B3S*)instance;
  freeSynth(b3s->inst);
  freeSynth(b3s->inst_offline);
  free(instance);
}

static char*
mn_file (LV2_Handle instance)
{
  B3S* b3s = (B3S*)instance;
  char model[16];
  snprintf (model, 16, "sbf-%p", b3s);
  model[15] = 0;
  char* buf = NULL;
  size_t siz = 0;
  LOCALEGUARD_START;

#ifdef _WIN32
  char temppath[MAX_PATH - 13];
  char filename[MAX_PATH + 1];
  if (0 == GetTempPath(sizeof(temppath), temppath))
    return NULL;
  if (0 == GetTempFileName(temppath, "sbfmidnam", 0, filename))
    return NULL;
  FILE *f = fopen(filename, "wb");
#else
  FILE* f = open_memstream (&buf, &siz);
#endif
  if (!f) {
    return NULL;
  }
  save_midname (b3s->inst, f, model);
  fclose (f);

#ifdef _WIN32
  f = fopen(filename, "rb");
  fseek (f , 0 , SEEK_END);
  long int rsize = ftell (f);
  rewind(f);
  buf = (char*) malloc(rsize);
  fread(buf, sizeof(char), rsize, f);
  fclose(f);
  unlink(filename);
#endif
  LOCALEGUARD_END;

  return buf;
}

static char*
mn_model (LV2_Handle instance)
{
  B3S* b3s = (B3S*)instance;
  char* rv = (char*) malloc (16 * sizeof (char));
  snprintf (rv, 16, "sbf-%p", b3s);
  rv[15] = 0;
  return rv;
}

static void
mn_free (char* v)
{
  free (v);
}

const void*
extension_data(const char* uri)
{
  static const LV2_Worker_Interface worker = { work, work_response, NULL };
  static const LV2_State_Interface  state  = { save, restore };
  static const LV2_Midnam_Interface midnam = { mn_file, mn_model, mn_free };
  if (!strcmp(uri, LV2_WORKER__interface)) {
    return &worker;
  }
  else if (!strcmp(uri, LV2_STATE__interface)) {
    return &state;
  }
  else if (!strcmp (uri, LV2_MIDNAM__interface)) {
    return &midnam;
  }
  return NULL;
}

static const LV2_Descriptor descriptor = {
  SB3_URI,
  instantiate,
  connect_port,
  activate,
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
