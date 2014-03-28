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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SETBFREE_STATE_H_
#define SETBFREE_STATE_H_

#include "cfgParser.h"

void *allocRunningConfig(void);
void initRunningConfig(void *t, void *mcfg);
void freeRunningConfig(void *t);

void rc_add_midicc(void *t, int id, unsigned char val);
void rc_add_cfg(void *t, ConfigContext *cfg);

void rc_loop_state(void *t, void (*cb)(int, const char *, const char *, unsigned char, void *), void *arg);

void rc_dump_state(void *t);
#endif
