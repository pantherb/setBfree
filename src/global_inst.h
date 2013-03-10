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

#ifndef GLOBAL_INST_H
#define GLOBAL_INST_H

#include "tonegen.h"
#include "vibrato.h"
#include "midi.h"
#include "whirl.h"
#include "overdrive.h"
#include "reverb.h"
#include "program.h"

typedef struct b_instance {
	struct b_reverb *reverb;
	struct b_whirl *whirl;
	struct b_tonegen *synth;
	struct b_programme *progs;
	void * midicfg;
	void * preamp;
} b_instance;

#endif
