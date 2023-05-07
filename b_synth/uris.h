/* setBfree - DSP tonewheel organ LV2
 *
 * Copyright (C) 2011-2012 David Robillard <d@drobilla.net>
 * Copyright (C) 2013-2018 Robin Gareus <robin@gareus.org>
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

#ifndef SB3_URIS_H
#define SB3_URIS_H

#ifdef HAVE_LV2_1_18_6
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/midi/midi.h>
#include <lv2/patch/patch.h>
#include <lv2/urid/urid.h>
#else
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/patch/patch.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#endif

#define SB3_URI "http://gareus.org/oss/lv2/b_synth"

/* clang-format off */

#undef LV2_SYMBOL_EXPORT
#ifdef JACK_DESCRIPT
# define LV2_SYMBOL_EXPORT
#else
# ifdef _WIN32
#  define LV2_SYMBOL_EXPORT __declspec(dllexport)
# else
#  define LV2_SYMBOL_EXPORT  __attribute__ ((visibility ("default")))
# endif
#endif

#ifdef HAVE_LV2_1_8
# define x_forge_object lv2_atom_forge_object
#else
# define x_forge_object lv2_atom_forge_blank
#endif

#define SB3__state    SB3_URI "#state"
#define SB3__uiinit   SB3_URI "#uiinit"
#define SB3__uimccq   SB3_URI "#uimccquery"
#define SB3__uimccs   SB3_URI "#uimccset"
#define SB3__midipgm  SB3_URI "#midipgm"
#define SB3__storepgm SB3_URI "#midisave"
#define SB3__control  SB3_URI "#controlmsg"
#define SB3__cckey    SB3_URI "#controlkey"
#define SB3__ccval    SB3_URI "#controlval"
#define SB3__ccdsc    SB3_URI "#controldsc"
#define SB3__loadpgm  SB3_URI "#loadpgm"
#define SB3__savepgm  SB3_URI "#savepgm"
#define SB3__loadcfg  SB3_URI "#loadcfg"
#define SB3__savecfg  SB3_URI "#savecfg"
#define SB3__uimsg    SB3_URI "#uimessage"
#define SB3__kactive  SB3_URI "#activekeys"
#define SB3__karray   SB3_URI "#keyarray"
#define SB3__cfgstr   SB3_URI "#cfgstr"
#define SB3__cfgkv    SB3_URI "#cfgkv"

/* clang-format on */

typedef struct {
	LV2_URID atom_Blank;
	LV2_URID atom_Object;
	LV2_URID atom_Path;
	LV2_URID atom_String;
	LV2_URID atom_Int;
	LV2_URID atom_Vector;
	LV2_URID atom_URID;
	LV2_URID atom_eventTransfer;

	LV2_URID sb3_state;
	LV2_URID sb3_uiinit;
	LV2_URID sb3_uimccquery;
	LV2_URID sb3_uimccset;
	LV2_URID sb3_midipgm;
	LV2_URID sb3_midisavepgm;
	LV2_URID sb3_control;
	LV2_URID sb3_cckey;
	LV2_URID sb3_ccdsc;
	LV2_URID sb3_ccval;
	LV2_URID sb3_loadpgm;
	LV2_URID sb3_savepgm;
	LV2_URID sb3_loadcfg;
	LV2_URID sb3_savecfg;
	LV2_URID sb3_uimsg;
	LV2_URID sb3_activekeys;
	LV2_URID sb3_keyarrary;
	LV2_URID sb3_cfgstr;
	LV2_URID sb3_cfgkv;
	LV2_URID state_Changed;

	LV2_URID midi_MidiEvent;
	LV2_URID atom_Sequence;
} setBfreeURIs;

static inline void
map_setbfree_uris (LV2_URID_Map* map, setBfreeURIs* uris)
{
	uris->atom_Blank         = map->map (map->handle, LV2_ATOM__Blank);
	uris->atom_Object        = map->map (map->handle, LV2_ATOM__Object);
	uris->atom_Path          = map->map (map->handle, LV2_ATOM__Path);
	uris->atom_String        = map->map (map->handle, LV2_ATOM__String);
	uris->atom_Int           = map->map (map->handle, LV2_ATOM__Int);
	uris->atom_Vector        = map->map (map->handle, LV2_ATOM__Vector);
	uris->atom_URID          = map->map (map->handle, LV2_ATOM__URID);
	uris->atom_eventTransfer = map->map (map->handle, LV2_ATOM__eventTransfer);
	uris->sb3_state          = map->map (map->handle, SB3__state);
	uris->sb3_uiinit         = map->map (map->handle, SB3__uiinit);
	uris->sb3_uimccquery     = map->map (map->handle, SB3__uimccq);
	uris->sb3_uimccset       = map->map (map->handle, SB3__uimccs);
	uris->sb3_midipgm        = map->map (map->handle, SB3__midipgm);
	uris->sb3_midisavepgm    = map->map (map->handle, SB3__storepgm);
	uris->sb3_control        = map->map (map->handle, SB3__control);
	uris->sb3_cckey          = map->map (map->handle, SB3__cckey);
	uris->sb3_ccval          = map->map (map->handle, SB3__ccval);
	uris->sb3_ccdsc          = map->map (map->handle, SB3__ccdsc);
	uris->midi_MidiEvent     = map->map (map->handle, LV2_MIDI__MidiEvent);
	uris->atom_Sequence      = map->map (map->handle, LV2_ATOM__Sequence);
	uris->sb3_loadpgm        = map->map (map->handle, SB3__loadpgm);
	uris->sb3_savepgm        = map->map (map->handle, SB3__savepgm);
	uris->sb3_loadcfg        = map->map (map->handle, SB3__loadcfg);
	uris->sb3_savecfg        = map->map (map->handle, SB3__savecfg);
	uris->sb3_uimsg          = map->map (map->handle, SB3__uimsg);
	uris->sb3_activekeys     = map->map (map->handle, SB3__kactive);
	uris->sb3_keyarrary      = map->map (map->handle, SB3__karray);
	uris->sb3_cfgstr         = map->map (map->handle, SB3__cfgstr);
	uris->sb3_cfgkv          = map->map (map->handle, SB3__cfgkv);
	uris->state_Changed      = map->map (map->handle, "http://lv2plug.in/ns/ext/state#StateChanged");
}

static inline void
forge_midimessage (LV2_Atom_Forge*      forge,
                   const setBfreeURIs*  uris,
                   const uint8_t* const msg, uint32_t size)
{
	//printf("UIcom: Tx MIDI msg\n");
	LV2_Atom midiatom;
	midiatom.type = uris->midi_MidiEvent;
	midiatom.size = size;

	lv2_atom_forge_frame_time (forge, 0);
	lv2_atom_forge_raw (forge, &midiatom, sizeof (LV2_Atom));
	lv2_atom_forge_raw (forge, msg, size);
	lv2_atom_forge_pad (forge, sizeof (LV2_Atom) + size);
}

static inline LV2_Atom*
forge_kvcontrolmessage (LV2_Atom_Forge*     forge,
                        const setBfreeURIs* uris,
                        const char* key, int32_t value)
{
	//printf("UIcom: Tx '%s' -> %d \n", key, value);

	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time (forge, 0);
	LV2_Atom* msg = (LV2_Atom*)x_forge_object (forge, &frame, 1, uris->sb3_control);

	lv2_atom_forge_property_head (forge, uris->sb3_cckey, 0);
	lv2_atom_forge_string (forge, key, strlen (key));
	lv2_atom_forge_property_head (forge, uris->sb3_ccval, 0);
	lv2_atom_forge_int (forge, value);
	lv2_atom_forge_pop (forge, &frame);
	return msg;
}

static inline LV2_Atom*
forge_kvconfigmessage (LV2_Atom_Forge*     forge,
                       const setBfreeURIs* uris,
                       LV2_URID            uri,
                       const char* key, const char* value)
{
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time (forge, 0);
	LV2_Atom* msg = (LV2_Atom*)x_forge_object (forge, &frame, 1, uri);

	lv2_atom_forge_property_head (forge, uris->sb3_cckey, 0);
	lv2_atom_forge_string (forge, key, strlen (key));
	lv2_atom_forge_property_head (forge, uris->sb3_ccval, 0);
	lv2_atom_forge_string (forge, value, strlen (value));
	lv2_atom_forge_pop (forge, &frame);
	return msg;
}

static inline int
get_cc_key_value (
    const setBfreeURIs* uris, const LV2_Atom_Object* obj,
    char** k, int* v)
{
	const LV2_Atom* key   = NULL;
	const LV2_Atom* value = NULL;
	if (!k || !v)
		return -1;
	*k = NULL;
	*v = 0;

	if (obj->body.otype != uris->sb3_control) {
		//fprintf(stderr, "B3Lv2: Ignoring message type %d (expect [ctrl] %d\n", obj->body.otype, uris->sb3_control);
		return -1;
	}
	lv2_atom_object_get (obj, uris->sb3_cckey, &key, uris->sb3_ccval, &value, 0);
	if (!key) {
		fprintf (stderr, "B3Lv2: Malformed ctrl message has no key.\n");
		return -1;
	}
	if (!value) {
		fprintf (stderr, " Malformed ctrl message has no value for key '%s'.\n", (char*)LV2_ATOM_BODY (key));
		return -1;
	}
	//printf("UIcom: Rx '%s' -> %d \n", (char*)LV2_ATOM_BODY(key), ((LV2_Atom_Int*)value)->body);

	*k = (char*)LV2_ATOM_BODY (key);
	*v = (int)((LV2_Atom_Int*)value)->body;

	return 0;
}

static inline int
get_cc_midi_mapping (
    const setBfreeURIs* uris, const LV2_Atom_Object* obj,
    char** fnname, char** mms)
{
	const LV2_Atom* key   = NULL;
	const LV2_Atom* value = NULL;
	if (!mms || !fnname)
		return -1;
	*mms    = NULL;
	*fnname = NULL;

	if (obj->body.otype != uris->sb3_uimccset) {
		//fprintf(stderr, "B3Lv2: Ignoring message type %d (expect [CC] %d\n", obj->body.otype, uris->sb3_uimccset);
		return -1;
	}
	lv2_atom_object_get (obj, uris->sb3_cckey, &key, uris->sb3_ccval, &value, 0);
	if (!key) {
		fprintf (stderr, "B3Lv2: Malformed CCmap message has no key.\n");
		return -1;
	}
	if (!value) {
		fprintf (stderr, " Malformed CCmap message has no value for key '%s'.\n", (char*)LV2_ATOM_BODY (key));
		return -1;
	}

	*fnname = (char*)LV2_ATOM_BODY (key);
	*mms    = (char*)LV2_ATOM_BODY (value);

	return 0;
}

static inline int
get_pgm_midi_mapping (
    const setBfreeURIs* uris, const LV2_Atom_Object* obj,
    int* pgmnum, char** pgmname, char** pgmdesc)
{
	const LV2_Atom* key   = NULL;
	const LV2_Atom* value = NULL;
	const LV2_Atom* descr = NULL;
	if (!pgmnum || !pgmname || !pgmdesc)
		return -1;
	*pgmnum  = 0;
	*pgmname = NULL;
	;
	*pgmdesc = NULL;

	if (obj->body.otype != uris->sb3_midipgm) {
		//fprintf(stderr, "B3Lv2: Ignoring message type %d (expect [CC] %d\n", obj->body.otype, uris->sb3_uimccset);
		return -1;
	}
	lv2_atom_object_get (obj, uris->sb3_cckey, &key, uris->sb3_ccval, &value, uris->sb3_ccdsc, &descr, 0);
	if (!key) {
		fprintf (stderr, "B3Lv2: Malformed PGMmap message has no key.\n");
		return -1;
	}
	if (!value) {
		fprintf (stderr, " Malformed PGMmap message has no value\n");
		return -1;
	}
	if (!descr) {
		fprintf (stderr, " Malformed PGMmap message has no value\n");
		return -1;
	}

	*pgmdesc = (char*)LV2_ATOM_BODY (descr);
	*pgmname = (char*)LV2_ATOM_BODY (value);
	*pgmnum  = (int)((LV2_Atom_Int*)key)->body;

	return 0;
}
#endif
