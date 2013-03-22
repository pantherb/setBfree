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

static inline bool
is_object_type(const setBfreeURIs* uris, LV2_URID type)
{
	return type == uris->atom_Blank;
}


#if 0
/**
 * Write a message like the following to @p forge:
 * []
 *     a patch:Set ;
 *     patch:property convolv2:impulse ;
 *     patch:value </home/me/foo.wav> .
 */
static inline LV2_Atom*
write_set_file(LV2_Atom_Forge*     forge,
               const setBfreeURIs* uris,
               const char*         filename)
{
	LV2_Atom_Forge_Frame frame;
	LV2_Atom* set = (LV2_Atom*)lv2_atom_forge_blank(
		forge, &frame, 1, uris->patch_Set);

	lv2_atom_forge_property_head(forge, uris->patch_property, 0);
	lv2_atom_forge_urid(forge, uris->clv2_impulse);
	lv2_atom_forge_property_head(forge, uris->patch_value, 0);
	lv2_atom_forge_path(forge, filename, strlen(filename));

	lv2_atom_forge_pop(forge, &frame);

	return set;
}

/**
 * Get the file path from a message like:
 * []
 *     a patch:Set ;
 *     patch:property convolv2:impulse ;
 *     patch:value </home/me/foo.wav> .
 */
static inline const LV2_Atom*
read_set_file(const setBfreeURIs*    uris,
              const LV2_Atom_Object* obj)
{
	if (obj->body.otype != uris->patch_Set) {
		fprintf(stderr, "Ignoring unknown message type %d\n", obj->body.otype);
		return NULL;
	}

	/* Get property URI. */
	const LV2_Atom* property = NULL;
	lv2_atom_object_get(obj, uris->patch_property, &property, 0);
	if (!property) {
		fprintf(stderr, "Malformed set message has no body.\n");
		return NULL;
	} else if (property->type != uris->atom_URID) {
		fprintf(stderr, "Malformed set message has non-URID property.\n");
		return NULL;
	} else if (((LV2_Atom_URID*)property)->body != uris->clv2_impulse) {
		fprintf(stderr, "Set message for unknown property.\n");
		return NULL;
	}

	/* Get value. */
	const LV2_Atom* file_path = NULL;
	lv2_atom_object_get(obj, uris->patch_value, &file_path, 0);
	if (!file_path) {
		fprintf(stderr, "Malformed set message has no value.\n");
		return NULL;
	} else if (file_path->type != uris->atom_Path) {
		fprintf(stderr, "Set message value is not a Path.\n");
		return NULL;
	}

	return file_path;
}
#endif

static inline LV2_Atom *
write_cc_key_value(LV2_Atom_Forge* forge,
		const setBfreeURIs* uris,
		const char* key, int value)
{
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time(forge, 0);
	LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_blank(forge, &frame, 1, uris->sb3_control);

	//printf("UIcom: Tx '%s' -> %d \n", key, value);

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
		fprintf(stderr, "Ignoring unknown message type %d\n", obj->body.otype);
		return -1;
	}
	lv2_atom_object_get(obj, uris->sb3_cckey, &key, uris->sb3_ccval, &value, 0);
	if (!key) {
		fprintf(stderr, "Malformed set message has no key.\n");
		return -1;
	}
	if (!value) {
		fprintf(stderr, "Malformed set message has no value.\n");
		return -1;
	}
	//printf("UIcom: Rx '%s' -> %d \n", (char*)LV2_ATOM_BODY(key), ((LV2_Atom_Int*)value)->body);

	*k = LV2_ATOM_BODY(key);
	*v = ((LV2_Atom_Int*)value)->body;

	return 0;
}


static inline void
append_midi_event(const setBfreeURIs*  uris,
                  LV2_Atom_Sequence*   seq,
                  uint32_t             time,
                  const uint8_t* const msg,
                  uint32_t             size)
{
  LV2_Atom_Event* end = lv2_atom_sequence_end(&seq->body, seq->atom.size);
  end->time.frames = time;
  end->body.type   = uris->midi_MidiEvent;
  end->body.size   = size;
  memcpy(end + 1, msg, size);
  seq->atom.size += lv2_atom_pad_size(sizeof(LV2_Atom_Event) + size);
}

static inline void
clear_sequence(const setBfreeURIs* uris, LV2_Atom_Sequence* seq)
{
  seq->atom.type = uris->atom_Sequence;
  seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
  seq->body.unit = 0;
  seq->body.pad  = 0;
}

#endif
