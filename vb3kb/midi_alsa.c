/* Virtual Organ - operator for ALSA sequencer
 *
 * Copyright (c) 1997-2000 by Takashi Iwai
 * Copyright (c) 2012 by Robin Gareus
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "vb3kb.h"
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <alsa/asoundlib.h>

/*
 * functions
 */
static int seq_open(Tcl_Interp *ip, void **private_return);
static void seq_close(Tcl_Interp *ip, void *private);
static void note_on(Tcl_Interp *ip, void *private, int chan, int note, int vel);
static void note_off(Tcl_Interp *ip, void *private, int chan, int note, int vel);
static void control(Tcl_Interp *ip, void *private, int chan, int type, int val);
static void program(Tcl_Interp *ip, void *private, int chan, int bank, int type);


/*
 * definition of device information
 */

static vkb_oper_t alsa_oper = {
	seq_open,
	seq_close,
	program,
	note_on,
	note_off,
	control,
};

static vkb_optarg_t alsa_opts[] = {
	{"addr", "setBfree:midi_in", "--addr <client:port>     ALSA sequencer destination"},
	{"name", DEFAULT_MIDI_NAME, "--name <name>            use the specified string as client/port names"},
	{NULL},
};

vkb_devinfo_t alsa_devinfo = {
	"alsa",		/* device id */
	"ALSA sequencer",	/* name */
	0,		/* delayed open */
	&alsa_oper,	/* operators */
	alsa_opts,	/* command line options */
};

/*
 */

static snd_seq_t *seq_handle = NULL;
static int my_port;

/*
 * parse address string
 */

/*
 * open device
 */

static int
seq_open(Tcl_Interp *ip, void **private_return)
{
  const char *var;

  if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
    vkb_error(ip, "can't open sequencer device");
    seq_handle=NULL;
    return 0;
  }

  /* set client info */
  if ((var = Tcl_GetVar2(ip, "optvar", "name", TCL_GLOBAL_ONLY)) != NULL)
    snd_seq_set_client_name(seq_handle, var);
  else
    snd_seq_set_client_name(seq_handle, DEFAULT_MIDI_NAME);

  my_port = snd_seq_create_simple_port(seq_handle, "midi_out",
      SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
      SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);

  if (my_port < 0) {
    vkb_error(ip, "can't create port\n");
    snd_seq_close(seq_handle);
    return 0;
  }

  if ((var = Tcl_GetVar2(ip, "optvar", "addr", TCL_GLOBAL_ONLY)) != NULL) {
    snd_seq_addr_t port;
    if (snd_seq_parse_address(seq_handle, &port, var) < 0) {
      vkb_error(ip, "cannot find port to connect to --addr %s\n", var);
      return 1;
    }
    if (snd_seq_connect_to(seq_handle, my_port, port.client, port.port) < 0) {
      vkb_error(ip, "can't subscribe to MIDI port (%d:%d)\n", port.client, port.port);
    }
  }

  return 1;
}


static void
seq_close(Tcl_Interp *ip, void *private)
{
  if (!seq_handle) return;
  snd_seq_close(seq_handle);
}


/*
 */

static snd_seq_event_t ev;

static void
send_event(int do_flush)
{
  snd_seq_ev_set_direct(&ev);
  snd_seq_ev_set_source(&ev, my_port);
  snd_seq_ev_set_subs(&ev);

  snd_seq_event_output(seq_handle, &ev);
  if (do_flush)
    snd_seq_drain_output(seq_handle);
}

static void
note_on(Tcl_Interp *ip, void *private, int chan, int note, int vel)
{
  snd_seq_ev_set_noteon(&ev, chan, note, vel);
  send_event(1);
}

static void
note_off(Tcl_Interp *ip, void *private, int chan, int note, int vel)
{
  snd_seq_ev_set_noteoff(&ev, chan, note, vel);
  send_event(1);
}

static void
control(Tcl_Interp *ip, void *private, int chan, int type, int val)
{
  snd_seq_ev_set_controller(&ev, chan, type, val);
  send_event(1);
}

static void
program(Tcl_Interp *ip, void *private, int chan, int bank, int preset)
{
  snd_seq_ev_set_controller(&ev, 0, 0, bank);
  send_event(0);

  snd_seq_ev_set_pgmchange(&ev, chan, preset);
  send_event(1);
}
/* vi:set ts=8 sts=2 sw=2: */
