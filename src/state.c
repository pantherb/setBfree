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

#include "midi.h"

struct b_rc {
  int mccc; // count of midi-CCs
  int *mcc; // midi-CC values for each midi-CC function
};

void freeRunningConfig(void *t) {
  struct b_rc *rc = (struct b_rc*) t;
  free (rc->mcc);
}

void *allocRunningConfig(void) {
  int i,mccc;
  struct b_rc *rc = (struct b_rc*) malloc(sizeof(struct b_rc));
  if (!rc) return NULL;

  mccc = rc->mccc = getCCFunctionCount();
  rc->mcc = malloc(mccc * sizeof(int));

  for (i = 0; i < mccc; ++i) {
    rc->mcc[i] = -1; // mark as unset
  }

  return rc;
}

void rc_add_midicc(void *t, int id, unsigned char val) {
  struct b_rc *rc = (struct b_rc*) t;
  if (id < 0 || id >= rc->mccc) {
#if 0 // devel&debug
    fprintf(stderr, "ignored state save: fn:%d -> %d\n", id, val);
#endif
    return;
  }
  rc->mcc[id] = val;
}


void rc_add_cfg(void *t, ConfigContext *cfg) {
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
  }
#endif
  // TODO use hash-table
  // printf ("  cfg_eval(\"%s\", \"%s\")\n", cfg->name, cfg->value);
}


void rc_loop_state(void *t, void (*cb)(int, const char *, unsigned char, void *), void *arg) {
  struct b_rc *rc = (struct b_rc*) t;
  int i;
  for (i = 0; i < rc->mccc; ++i) {
    if (rc->mcc[i] < 0) continue;
    cb(i, getCCFunctionName(i), rc->mcc[i], arg);
  }
  // TODO hash-table of 'cfg'
}

/* ------------- */

static void state_print_cb(int fnid, const char *fnname, unsigned char val, void *arg) {
  if (fnid < 0) {
    printf("  rc_cfg (\"%s\", %d);\n", fnname, val);
  } else {
    printf("  rc_ccf (\"%s\", %d); // id:%d\n", fnname, val, fnid);
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
