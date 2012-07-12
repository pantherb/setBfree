/* Virtual Tiny Keyboard -- setBfree variant
 *
 * Copyright (c) 1997-2000 by Takashi Iwai
 * Copyright (C) 2008-2012 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#ifndef VKB_H_DEF
#define VKB_H_DEF

#include <tcl.h>

#ifndef VKB_TCLFILE
#define VKB_TCLFILE "/usr/local/bin/vb3kb.tcl"
#endif

#ifndef DEFAULT_MIDI_NAME
#define DEFAULT_MIDI_NAME	"BKeyboard"
#endif

/*
 * device operator
 */
typedef struct vkb_oper_t {
  int (*open)(Tcl_Interp *ip, void **private_return);
  void (*close)(Tcl_Interp *ip, void *private);
  void (*program)(Tcl_Interp *ip, void *private, int chan, int bank, int prg); /* bank=128: drum */
  void (*noteon)(Tcl_Interp *ip, void *private, int chan, int note, int vel);
  void (*noteoff)(Tcl_Interp *ip, void *private, int chan, int note, int vel);
  void (*control)(Tcl_Interp *ip, void *private, int chan, int type, int val);
} vkb_oper_t;

/*
 * Tcl global option variables
 */
typedef struct vkb_optarg_t {
  char *name;
  char *defval;
  char *desc;
} vkb_optarg_t;

/*
 * device information
 */
typedef struct vkb_devinfo_t {
  char *name;
  char *desc;
  int delayed_open;
  vkb_oper_t *oper;
  vkb_optarg_t *opts;
} vkb_devinfo_t;

void vkb_error(Tcl_Interp *ip, char *fmt, ...);

#endif
/* vi:set ts=8 sts=2 sw=2: */
