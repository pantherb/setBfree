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

#ifndef MIDI_H
#define MIDI_H

#include <stdio.h>
#include <stdint.h>

#include "cfgParser.h"

typedef uint8_t midiccflags_t;

enum { // 1,2,4,8,.. - adjust ctrlflg once >8 to uint16_t
  MFLAG_INV = 1,
};

void useMIDIControlFunction (void *m, const char * cfname, void (* f) (void *, unsigned char), void *d);
void callMIDIControlFunction (void *m, const char * cfname, unsigned char val);
void notifyControlChangeByName (void *mcfg, const char * cfname, unsigned char val);
void notifyControlChangeById (void *mcfg, int id, unsigned char val);

extern int  getCCFunctionCount ();
extern const char * getCCFunctionName(int x);

extern void setKeyboardSplitMulti (void *mcfg,
				   int flags,
				   int p_splitA_PL,
				   int p_splitA_UL,
				   int p_nshA_PL,
				   int p_nshA_UL,
				   int p_nshA_U);

extern void setKeyboardTransposeA (void *mcfg, int t);
extern void setKeyboardTransposeB (void *mcfg, int t);
extern void setKeyboardTransposeC (void *mcfg, int t);
extern void setKeyboardTranspose  (void *mcfg, int t);

extern void midiPrimeControllerMapping (void *mcfg);

extern int midiConfig (void *mcfg, ConfigContext * cfg);
extern const ConfigDoc *midiDoc ();

extern void setMIDINoteShift (void *mcfg, char offset);

extern void initMidiTables(void *mcfg);
extern void initControllerTable (void *mcfg);
extern int getCCFunctionId (const char * name);
extern void listCCAssignments(void *mctl, FILE * fp);

extern void *allocMidiCfg(void *stateptr);
extern void freeMidiCfg(void *mcfg);

#ifdef HAVE_ASEQ
extern void *aseq_run(void *arg);
extern int aseq_open(char *port_name);
extern void aseq_close(void);
#endif

extern void parse_raw_midi_data(void *inst, uint8_t *buffer, size_t size);

typedef struct _midicc {
	struct _midicc *next;
  uint8_t channel; /**< the MIDI channel number 0-15 for CC (NYI: PGM changes maybe >16 or < 0)*/
	uint8_t param;
} midiCCmap;

void setControlFunctionCallback(void *mcfg, void (* fn) (int, const char *, unsigned char, midiCCmap *, void *), void *d);

void midi_uiassign_cc (void *mcfg, const char *fname, midiccflags_t flags);
void midi_loopCCAssignment(void *mcfg, int ulp, void (*cb)(const char *, const unsigned char, const unsigned char, const unsigned char, void *), void *arg);
void midi_channels(void *mcfg, int *upper, int *lower, int *pedals);
#endif /* MIDI_H */
