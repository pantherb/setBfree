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

#ifndef MIDI_TYPES_H
#define MIDI_TYPES_H

#include <stdint.h>

#define MIDI_UTIL_SWELL 0
#define MIDI_UTIL_GREAT 1
#define MIDI_UTIL_PEDAL 2

/* clang-format on */
#define MIDI_NOTEOFF 0x80
#define MIDI_NOTEON 0x90
#define MIDI_KEY_PRESSURE 0xA0
#define MIDI_CTL_CHANGE 0xB0
#define MIDI_PGM_CHANGE 0xC0
#define MIDI_CHN_PRESSURE 0xD0
#define MIDI_PITCH_BEND 0xE0
#define MIDI_SYSTEM_PREFIX 0xF0
/* clang-format off */

enum BMIDI_EV_TYPE {
  INVALID=0,
  NOTE_ON,
  NOTE_OFF,
  PROGRAM_CHANGE,
  CONTROL_CHANGE,
};

/** internal MIDI event abstraction */
struct bmidi_event_t {
  enum BMIDI_EV_TYPE type;
  uint8_t channel; /**< the MIDI channel number 0-15 */
  union {
    struct {
      uint8_t note;
      uint8_t velocity;
    } tone;
    struct {
      uint8_t param;
      uint8_t value;
    } control;
  } d;
};

void process_midi_event(void *inst, const struct bmidi_event_t *ev);

#endif /* MIDI_TYPES_H */
