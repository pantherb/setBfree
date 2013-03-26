/* setBfree - DSP tonewheel organ LV2
 *
 * Copyright 2011-2012 David Robillard <d@drobilla.net>
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

#ifndef SB3_URIS_H
#define SB3_URIS_H

#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/patch/patch.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"

#define SB3_URI "http://gareus.org/oss/lv2/b_synth"

#define SB3__load    SB3_URI "#load"
#define SB3__state   SB3_URI "#state"
#define SB3__uiinit  SB3_URI "#uiinit"
#define SB3__control SB3_URI "#controlmsg"
#define SB3__cckey   SB3_URI "#controlkey"
#define SB3__ccval   SB3_URI "#controlval"

typedef struct {
	LV2_URID atom_Blank;
	LV2_URID atom_Path;
	LV2_URID atom_String;
	LV2_URID atom_Int;
	LV2_URID atom_URID;
	LV2_URID atom_eventTransfer;

	LV2_URID sb3_state;
	LV2_URID sb3_uiinit;
	LV2_URID sb3_control;
	LV2_URID sb3_cckey;
	LV2_URID sb3_ccval;

	LV2_URID midi_MidiEvent;
	LV2_URID atom_Sequence;
} setBfreeURIs;

static inline void
map_setbfree_uris(LV2_URID_Map* map, setBfreeURIs* uris)
{
	uris->atom_Blank         = map->map(map->handle, LV2_ATOM__Blank);
	uris->atom_Path          = map->map(map->handle, LV2_ATOM__Path);
	uris->atom_String        = map->map(map->handle, LV2_ATOM__String);
	uris->atom_Int           = map->map(map->handle, LV2_ATOM__Int);
	uris->atom_URID          = map->map(map->handle, LV2_ATOM__URID);
	uris->atom_eventTransfer = map->map(map->handle, LV2_ATOM__eventTransfer);
	uris->sb3_state          = map->map(map->handle, SB3__state);
	uris->sb3_uiinit         = map->map(map->handle, SB3__uiinit);
	uris->sb3_control        = map->map(map->handle, SB3__control);
	uris->sb3_cckey          = map->map(map->handle, SB3__cckey);
	uris->sb3_ccval          = map->map(map->handle, SB3__ccval);
	uris->midi_MidiEvent     = map->map(map->handle, LV2_MIDI__MidiEvent);
  uris->atom_Sequence      = map->map(map->handle, LV2_ATOM__Sequence);
}

static inline void
forge_midimessage(LV2_Atom_Forge* forge,
		const setBfreeURIs* uris,
		const uint8_t* const msg, uint32_t size)
{
	//printf("UIcom: Tx MIDI msg\n");
	LV2_Atom midiatom;
	midiatom.type = uris->midi_MidiEvent;
	midiatom.size = size;

	lv2_atom_forge_frame_time(forge, 0);
	lv2_atom_forge_raw(forge, &midiatom, sizeof(LV2_Atom));
	lv2_atom_forge_raw(forge, msg, size);
	lv2_atom_forge_pad(forge, sizeof(LV2_Atom) + size);
}


static inline LV2_Atom *
forge_kvcontrolmessage(LV2_Atom_Forge* forge,
		const setBfreeURIs* uris,
		const char* key, int32_t value)
{
	//printf("UIcom: Tx '%s' -> %d \n", key, value);

	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time(forge, 0);
	LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_blank(forge, &frame, 1, uris->sb3_control);

	lv2_atom_forge_property_head(forge, uris->sb3_cckey, 0);
	lv2_atom_forge_string(forge, key, strlen(key));
	lv2_atom_forge_property_head(forge, uris->sb3_ccval, 0);
	lv2_atom_forge_int(forge, value);
	lv2_atom_forge_pop(forge, &frame);
	return msg;
}

static inline int
get_cc_key_value(
		const setBfreeURIs* uris, const LV2_Atom_Object* obj,
		char **k, int *v)
{
	const LV2_Atom* key = NULL;
	const LV2_Atom* value = NULL;
	if (!k || !v) return -1;
	*k = NULL; *v = 0;

	if (obj->body.otype != uris->sb3_control) {
		fprintf(stderr, "B3Lv2: Ignoring unknown message type %d\n", obj->body.otype);
		return -1;
	}
	lv2_atom_object_get(obj, uris->sb3_cckey, &key, uris->sb3_ccval, &value, 0);
	if (!key) {
		fprintf(stderr, "B3Lv2: Malformed message has no key.\n");
		return -1;
	}
	if (!value) {
		fprintf(stderr, " Malformed message has no value for key '%s'.\n", (char*)LV2_ATOM_BODY(key));
		return -1;
	}
	//printf("UIcom: Rx '%s' -> %d \n", (char*)LV2_ATOM_BODY(key), ((LV2_Atom_Int*)value)->body);

	*k = LV2_ATOM_BODY(key);
	*v = ((LV2_Atom_Int*)value)->body;

	return 0;
}

#endif
