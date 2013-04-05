/* setBfree - DSP tonewheel organ
 *
 * Copyright (C) 2013 Robin Gareus <robin@gareus.org>
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

/* keep track of the current state/config.
 * This is done by using the initial-state + all runtime changes.
 * We only keep track of the latest (aka current) state -> no undo.
 *
 *
 * The current organ state:
 *   * /hardcoded/ initialization
 *   + configuration file
 *   + MIDI-CCs
 *   + [MIDI program-changes]
 *     -> collection of MIDI-CCs (remembered via MIDI-CC above)
 *     -> keytable changes; affect loadKeyTable[A-C] (transpose, etc)
 *
 * We hook into midi-CC function calls and remember the latest value
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rc_dump_state(void *t);
/* simple key-value store */

struct b_kv {
  struct b_kv *next;
  char *key;
  char *value;
};

static void *kvstore_alloc() {
  struct b_kv *kv = calloc(1, sizeof(struct b_kv));
  return kv;
}

static void kvstore_free(void *kvs) {
  struct b_kv *kv = (struct b_kv*) kvs;
  while (kv) {
    struct b_kv *me = kv;
    free(kv->key);
    free(kv->value);
    kv = kv->next;
    free(me);
  }
}

static void kvstore_store(void *kvs, const char *key, const char *value) {
  struct b_kv *kv = (struct b_kv*) kvs;
  struct b_kv *it = NULL;
  while (kv) {
    if (!kv->next) {
      /* "->next == NULL" : "terminal node" */
      break;
    }
    if (!strcmp(kv->key, key)) {
      it = kv;
      break;
    }
    kv = kv->next;
  }
  if (!it) {
    /* allocate new terminal node */
    it = calloc(1, sizeof(struct b_kv));
    kv->next = it;
    it = kv;
    it->key = strdup(key);
  }
  free(it->value);
  it->value = strdup(value);
}

/* setBfree resource/running config */

#include "midi.h"

struct b_midirc {
  int mccc; // count of midi-CCs
  int *mcc; // midi-CC values for each midi-CC function
};

struct b_rc {
  struct b_midirc mrc;
  struct b_kv *rrc;
};


void freeRunningConfig(void *t) {
  struct b_rc *rc = (struct b_rc*) t;
  free (rc->mrc.mcc);
  kvstore_free(rc->rrc);
  free(rc);
}

void *allocRunningConfig(void) {
  int i,mccc;
  struct b_rc *rc = (struct b_rc*) malloc(sizeof(struct b_rc));
  if (!rc) return NULL;

  mccc = rc->mrc.mccc = getCCFunctionCount();
  rc->mrc.mcc = malloc(mccc * sizeof(int));
  if (!rc->mrc.mcc) {
    free(rc);
    return NULL;
  }

  rc->rrc = kvstore_alloc();

  if (!rc->rrc) {
    free(rc->mrc.mcc);
    free(rc);
    return NULL;
  }

  for (i = 0; i < mccc; ++i) {
    rc->mrc.mcc[i] = -1; // mark as unset
  }

  return rc;
}

void rc_add_midicc(void *t, int id, unsigned char val) {
  struct b_rc *rc = (struct b_rc*) t;
  if (id < 0 || id >= rc->mrc.mccc) {
#if 0 // devel&debug
    fprintf(stderr, "ignored state save: fn:%d -> %d\n", id, val);
#endif
    return;
  }
  rc->mrc.mcc[id] = (int) val;
}


void rc_add_cfg(void *t, ConfigContext *cfg) {
  struct b_rc *rc = (struct b_rc*) t;
#if 0
  if (getCCFunctionId(cfg->name) > 0) {
    /* if there is a MIDI-CC function corresponding to the cfg -> use it */

    // mmh. what to do with 'cfg->value' ?!
    // - check doc for function parameter type.
    //  CFG_DOUBLE, CFG_FLOAT  ->  * 127.0 -- won't work for all though :(
    //  CFG_INT -> mmh, keep as is ?!
    //  CFG_TEXT -> fall though to cfg_eval()
    //
    // major rework: have all useMIDIControlFunction() /users/
    // specify a target range and/or conversion function.
    // -> that'll also remove lots of wrapper function bloat:
    //  eg. setDrumBreakPosition(), revControl(), setPercEnableFromMIDI(),..  etc
    printf ("  cfg_midi(\"%s\", \"%s\")\n", cfg->name, cfg->value);
    return;
  } else
#endif
  kvstore_store(rc->rrc, cfg->name, cfg->value);
}


void rc_loop_state(void *t, void (*cb)(int, const char *, const char *, unsigned char, void *), void *arg) {
  struct b_rc *rc = (struct b_rc*) t;
  int i;
  for (i = 0; i < rc->mrc.mccc; ++i) {
    if (rc->mrc.mcc[i] < 0) continue;
    cb(i, getCCFunctionName(i), NULL, (unsigned char) rc->mrc.mcc[i], arg);
  }

  struct b_kv *kv = rc->rrc;
  while (kv && kv->next != NULL) {
    if (kv->key == NULL) continue;
    cb(-1, kv->key, kv->value, 0, arg);
    kv = kv->next;
  }
}

/* ------------- */

static void state_print_cb(int fnid, const char *key, const char *kv, unsigned char val, void *arg) {
  if (fnid < 0) {
    printf("  rc_cfg (\"%s\", \"%s\");\n", key, kv);
  } else {
    printf("  rc_ccf (\"%s\", %d); // id:%d\n", key, val, fnid);
  }
}

void rc_dump_state(void *t) {
  rc_loop_state(t, &state_print_cb, NULL);
}

/* ------------- */

/* this is a bit of a dirty hack
 *
 * In order for the LV2 plugin to query the complete state,
 * we save defaults as as midi-CC.
 *
 * All modules should /register/ their default values and settings!
 *
 * some stuff, e.g. midiPrimeControllerMapping() and
 * program-table are also dependent on commandline args.. Ugh.
 *
 */
void initRunningConfig(void *t, void *mcfg) {
#if 0 // these are set by main() -> midi-CC hooks are called.
  notifyControlChangeByName(mcfg, "upper.drawbar16", 0);
  notifyControlChangeByName(mcfg, "upper.drawbar513", 0);
  notifyControlChangeByName(mcfg, "upper.drawbar8", 32);
  notifyControlChangeByName(mcfg, "upper.drawbar4", 127);
  notifyControlChangeByName(mcfg, "upper.drawbar223", 127);
  notifyControlChangeByName(mcfg, "upper.drawbar2", 127);
  notifyControlChangeByName(mcfg, "upper.drawbar135", 127);
  notifyControlChangeByName(mcfg, "upper.drawbar113", 127);
  notifyControlChangeByName(mcfg, "upper.drawbar1", 127);
#endif
  notifyControlChangeByName(mcfg, "lower.drawbar16", 0);
  notifyControlChangeByName(mcfg, "lower.drawbar513", 80);
  notifyControlChangeByName(mcfg, "lower.drawbar8", 0);
  notifyControlChangeByName(mcfg, "lower.drawbar4", 127);
  notifyControlChangeByName(mcfg, "lower.drawbar223", 127);
  notifyControlChangeByName(mcfg, "lower.drawbar2", 127);
  notifyControlChangeByName(mcfg, "lower.drawbar135", 127);
  notifyControlChangeByName(mcfg, "lower.drawbar113", 127);
  notifyControlChangeByName(mcfg, "lower.drawbar1", 127);
  notifyControlChangeByName(mcfg, "pedal.drawbar16", 0);
  notifyControlChangeByName(mcfg, "pedal.drawbar8", 32);

  notifyControlChangeByName(mcfg, "vibrato.routing", 0);
  notifyControlChangeByName(mcfg, "vibrato.knob", 0);

  notifyControlChangeByName(mcfg, "percussion.enable", 0);
  notifyControlChangeByName(mcfg, "percussion.volume", 0);
  notifyControlChangeByName(mcfg, "percussion.decay", 0);
  notifyControlChangeByName(mcfg, "percussion.harmonic", 0);

  notifyControlChangeByName(mcfg, "overdrive.enable", 0);
  notifyControlChangeByName(mcfg, "overdrive.character", 0);

  notifyControlChangeByName(mcfg, "reverb.mix", 38);
  notifyControlChangeByName(mcfg, "swellpedal1", 127);
  notifyControlChangeByName(mcfg, "rotary.speed-select", 4*15);
}


/* vi:set ts=8 sts=2 sw=2: */
