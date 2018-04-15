/* setBfree - DSP tonewheel organ
 *
 * Copyright (C) 2003-2004 Fredrik Kilander <fk@dsv.su.se>
 * Copyright (C) 2008-2018 Robin Gareus <robin@gareus.org>
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

#ifdef HAVE_ASEQ /* ALSA SEQUENCER MIDI INTERFACE */

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/types.h>
#include <unistd.h>

#include "midi.h"
#include "midi_aseq.h"
#include "midi_types.h"

snd_seq_t* seq = NULL;
extern int aseq_stop;

void
aseq_close (void)
{
	if (!seq)
		return;
	snd_seq_close (seq);
	seq = NULL;
}

int
aseq_open (char* port_name)
{
	int            err = 0;
	snd_seq_addr_t port;
	char           seq_name[32];
	snprintf (seq_name, 32, "setBfree");

	if (seq)
		return (-1);

	if ((err = snd_seq_open (&seq, "default", SND_SEQ_OPEN_INPUT, 0)) < 0) {
		fprintf (stderr, "cannot open alsa sequencer: %s\n", snd_strerror (err));
		seq = NULL;
		return (-1);
	}

	if ((err = snd_seq_set_client_name (seq, seq_name)) < 0) {
		fprintf (stderr, "cannot set client name: %s\n", snd_strerror (err));
		aseq_close ();
		return (-1);
	}

	if ((err = snd_seq_create_simple_port (seq, "midi_in",
	                                       SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
	                                       SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION)) < 0) {
		fprintf (stderr, "cannot create port: %s\n", snd_strerror (err));
		aseq_close ();
		return (-1);
	}

	if (port_name && strlen (port_name) > 0) {
		err = snd_seq_parse_address (seq, &port, port_name);
		if (err < 0) {
			fprintf (stderr, "ALSA: Cannot find port %s - %s\n", port_name, snd_strerror (err));
		} else {
			err = snd_seq_connect_from (seq, 0, port.client, port.port);
			if (err < 0) {
				fprintf (stderr, "ALSA: Cannot connect from port %d:%d - %s\n", port.client, port.port, snd_strerror (err));
			}
		}
	}

	snd_seq_nonblock (seq, 1);
	return (0);
}

/** convert ALSA-sequencer event into  internal MIDI message
 * format  and process the event
 */
static void
process_seq_event (void* inst, const snd_seq_event_t* ev)
{
	// see "snd_seq_event_type" file:///usr/share/doc/libasound2-doc/html/group___seq_events.html
	struct bmidi_event_t bev;
	memset (&bev, 0, sizeof (struct bmidi_event_t));

	switch (ev->type) {
		case SND_SEQ_EVENT_NOTEON:
			bev.channel         = ev->data.note.channel;
			bev.type            = NOTE_ON;
			bev.d.tone.note     = ev->data.note.note;
			bev.d.tone.velocity = ev->data.note.velocity;
			break;
		case SND_SEQ_EVENT_NOTEOFF:
			bev.channel         = ev->data.note.channel;
			bev.type            = NOTE_OFF;
			bev.d.tone.note     = ev->data.note.note;
			bev.d.tone.velocity = 0;
			break;
		case SND_SEQ_EVENT_PGMCHANGE:
			bev.type            = PROGRAM_CHANGE;
			bev.d.control.value = ev->data.control.value;
			break;
		case SND_SEQ_EVENT_CONTROLLER:
			bev.type            = CONTROL_CHANGE;
			bev.channel         = ev->data.note.channel;
			bev.d.control.param = ev->data.control.param;
			bev.d.control.value = ev->data.control.value;
			break;
		default:
			return;
	}
	process_midi_event (inst, &bev);
}

void*
aseq_run (void* arg)
{
	int            err;
	int            npfds = 0;
	struct pollfd* pfds;

	npfds = snd_seq_poll_descriptors_count (seq, POLLIN);
	pfds  = (struct pollfd*)malloc (sizeof (*pfds) * npfds);
	while (1) {
		snd_seq_poll_descriptors (seq, pfds, npfds, POLLIN);
		if (poll (pfds, npfds, 1) < 0)
			break;
		do {
			snd_seq_event_t* event;
			err = snd_seq_event_input (seq, &event);
			if (err < 0)
				break;
			if (event)
				process_seq_event (arg, event);
		} while (err > 0);
		if (aseq_stop)
			break;
	}
	free (pfds);
	pthread_exit (NULL);
	return (NULL);
}
#endif
