/* Virtual Organ - operator for JACK MIDI
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
 */

#include "vb3kb.h"
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <jack/jack.h>
#include <jack/midiport.h>

/*
 * functions
 */
static int seq_open(Tcl_Interp *ip, void **private_return);
static void seq_close(Tcl_Interp *ip, void *private);
static void note_on(Tcl_Interp *ip, void *private, int chan, int note, int vel);
static void note_off(Tcl_Interp *ip, void *private, int chan, int note, int vel);
static void control(Tcl_Interp *ip, void *private, int chan, int type, int val);
static void program(Tcl_Interp *ip, void *private, int chan, int bank, int type);


static jack_client_t *j_client = NULL;
static jack_port_t *j_output_port; 
static const char *jack_outport_name = NULL;

#define JACK_MIDI_QUEUE_SIZE (1024)
typedef struct my_midi_event {
  jack_nframes_t time;
  size_t size;
  jack_midi_data_t buffer[4];
} my_midi_event_t;

static my_midi_event_t event_queue[JACK_MIDI_QUEUE_SIZE];
static int queued_events_start = 0;
static int queued_events_end = 0;
static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;


int jack_midi_callback (jack_nframes_t nframes, void *arg) {
  void *jack_buf = jack_port_get_buffer(j_output_port, nframes);
  jack_midi_clear_buffer(jack_buf);
  while (queued_events_end != queued_events_start) {
    jack_midi_event_write(jack_buf,
	event_queue[queued_events_end].time,
	event_queue[queued_events_end].buffer,
	event_queue[queued_events_end].size);
    queued_events_end = (queued_events_end + 1)%JACK_MIDI_QUEUE_SIZE;
  }
  return(0);
}

void jack_shutdown_callback(void *arg) {
  j_client=NULL;
  exit(1);
}

static int queue_midi_frame(void *d, size_t l) {
  if (l>3) return -1;
  pthread_mutex_lock (&qlock);
  event_queue[queued_events_start].time = 0;
  memcpy(event_queue[queued_events_start].buffer, d, l);
  event_queue[queued_events_start].size = l;
  queued_events_start = (queued_events_start + 1)%JACK_MIDI_QUEUE_SIZE;
  pthread_mutex_unlock (&qlock);
  return 0;
}

/*
 * definition of device information
 */
static vkb_oper_t jack_oper = {
  seq_open,
  seq_close,
  program,
  note_on,
  note_off,
  control,
};

static vkb_optarg_t jack_opts[] = {
  {"port", "setBfree:midi_in", "--port <portname>        auto-connect to this client."},
  {"name", DEFAULT_MIDI_NAME,   "--name <name>            use the specified string as client name"},
  {NULL},
};

vkb_devinfo_t jack_devinfo = {
  "jack",		/* device id */
  "JACK MIDI",	/* name */
  0,		/* delayed open */
  &jack_oper,	/* operators */
  jack_opts,	/* command line options */
};

static int
seq_open(Tcl_Interp *ip, void **private_return)
{
  jack_outport_name = Tcl_GetVar2(ip, "optvar", "port", TCL_GLOBAL_ONLY);

  const char *name = Tcl_GetVar2(ip, "optvar", "name", TCL_GLOBAL_ONLY);
  if (!name) name = DEFAULT_MIDI_NAME;

  j_client = jack_client_open (name, JackNullOption, NULL);
  if (!j_client) {
    vkb_error(ip, "could not connect to jack.\n");
    return(0);
  }
  j_output_port = jack_port_register (j_client, "out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
  if (!j_output_port) {
    vkb_error(ip, "no more jack ports available.\n");
    jack_client_close (j_client);
    return(0);
  }

#if 1 // FLUSH queue
  pthread_mutex_lock (&qlock);
  queued_events_start=queued_events_end=0;
  pthread_mutex_unlock (&qlock);
#endif

  jack_on_shutdown (j_client, jack_shutdown_callback, NULL);
  jack_set_process_callback(j_client,jack_midi_callback,NULL);
  jack_activate(j_client);

  if (jack_outport_name) {
    if (jack_connect(j_client, jack_port_name(j_output_port), jack_outport_name)) {
      vkb_error(ip, "cannot connect port %s to %s\n", jack_port_name(j_output_port), jack_outport_name);
    }
  }

  return 1;
}


static void
seq_close(Tcl_Interp *ip, void *private)
{
  if (!j_client) return;
  jack_deactivate(j_client);
  jack_client_close (j_client);
  j_client=NULL;
}


static void
note_on(Tcl_Interp *ip, void *private, int chan, int note, int vel)
{
  jack_midi_data_t d[3];
  d[0]= 0x90 | (chan&0xf);
  d[1]= (note&0x7f);
  d[2]= (vel&0x7f);
  queue_midi_frame(d,3);
}

static void
note_off(Tcl_Interp *ip, void *private, int chan, int note, int vel)
{
  jack_midi_data_t d[3];
  d[0]= 0x80 | (chan&0xf);
  d[1]= (note&0x7f);
  d[2]= (vel&0x7f);
  queue_midi_frame(d,3);
}

static void
control(Tcl_Interp *ip, void *private, int chan, int type, int val)
{
  jack_midi_data_t d[3];
  d[0]= 0xB0 | (chan&0xf);
  d[1]= (type&0x7f);
  d[2]= (val&0x7f);
  queue_midi_frame(d,3);
}

static void
program(Tcl_Interp *ip, void *private, int chan, int bank, int preset)
{
  jack_midi_data_t d[3];
#if 1 // MSB Bank
  d[0]= 0xB0;
  d[1]= 0x00;
  d[2]= ((bank>>7)&0x7f);
  queue_midi_frame(d,3);
#endif
#if 1 // LSB Bank
  d[0]= 0xB0;
  d[1]= 0x20;
  d[2]= (bank&0x7f);
  queue_midi_frame(d,3);
#endif

  d[0]= 0xC0 | (chan&0xf);
  d[1]= (preset&0x7f);
  queue_midi_frame(d,2);
}
/* vi:set ts=8 sts=2 sw=2: */
