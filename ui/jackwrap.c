/* x42 jack wrapper / minimal LV2 host
 *
 * Copyright (C) 2012-2014 Robin Gareus
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef UPDATE_FREQ_RATIO
#define UPDATE_FREQ_RATIO 60 // MAX # of audio-cycles per GUI-refresh
#endif

#ifndef UI_UPDATE_FPS
#define UI_UPDATE_FPS 25
#endif

#ifndef MAXDELAY
#define MAXDELAY 192001 // delayline max possible delay
#endif

#ifndef MAXPERIOD
#define MAXPERIOD 8192 // delayline - max period size (jack-period)
#endif

///////////////////////////////////////////////////////////////////////////////

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef WIN32
#include <windows.h>
#include <pthread.h>
#define pthread_t //< override jack.h def
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
extern void rtk_osx_api_init(void);
extern void rtk_osx_api_terminate(void);
extern void rtk_osx_api_run(void);
extern void rtk_osx_api_err(const char *msg);
#endif

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <getopt.h>
#include <assert.h>

#if (defined _WIN32 && defined RTK_STATIC_INIT)
#include <glib-object.h>
#endif

#ifndef _WIN32
#include <sys/mman.h>
#endif

#ifdef USE_WEAK_JACK
#include "weakjack/weak_libjack.h"
#else
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>
#endif

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "lv2/lv2plug.in/ns/ext/uri-map/uri-map.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/time/time.h"

#include "./gl/xternalui.h"

#ifndef WIN32
#include <signal.h>
#include <pthread.h>
#endif

#define LV2_EXTERNAL_UI_RUN(ptr) (ptr)->run(ptr)
#define LV2_EXTERNAL_UI_SHOW(ptr) (ptr)->show(ptr)
#define LV2_EXTERNAL_UI_HIDE(ptr) (ptr)->hide(ptr)

#define nan NAN

const LV2_Descriptor* plugin_dsp;
const LV2UI_Descriptor *plugin_gui;

LV2_Handle plugin_instance = NULL;
LV2UI_Handle gui_instance = NULL;

float  *plugin_ports_pre  = NULL;
float  *plugin_ports_post = NULL;

LV2_Atom_Sequence *atom_in = NULL;
LV2_Atom_Sequence *atom_out = NULL;

static jack_port_t **input_port = NULL;
static jack_port_t **output_port = NULL;

static jack_port_t *midi_in = NULL;
static jack_port_t *midi_out = NULL;

static jack_client_t *j_client = NULL;
static uint32_t j_samplerate = 48000;

struct transport_position {
	jack_nframes_t position;
	float          bpm;
	bool           rolling;
} j_transport = {0, 0, false};

static jack_ringbuffer_t *rb_ctrl_to_ui = NULL;
static jack_ringbuffer_t *rb_ctrl_from_ui = NULL;
static jack_ringbuffer_t *rb_atom_to_ui = NULL;
static jack_ringbuffer_t *rb_atom_from_ui = NULL;

static pthread_mutex_t gui_thread_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  data_ready = PTHREAD_COND_INITIALIZER;

uint32_t uri_midi_MidiEvent = 0;
uint32_t uri_atom_Sequence = 0;
uint32_t uri_atom_EventTransfer = 0;

uint32_t uri_time_Position = 0;
uint32_t uri_time_frame    = 0;
uint32_t uri_time_speed    = 0;
uint32_t uri_time_bar     = 0;
uint32_t uri_time_barBeat = 0;
uint32_t uri_time_beatUnit = 0;
uint32_t uri_time_beatsPerBar = 0;
uint32_t uri_time_beatsPerMinute = 0;

char **urimap = NULL;
uint32_t urimap_len = 0;

enum PortType {
	CONTROL_IN = 0,
	CONTROL_OUT,
	AUDIO_IN,
	AUDIO_OUT,
	MIDI_IN,
	MIDI_OUT,
	ATOM_IN,
	ATOM_OUT
};

struct DelayBuffer {
	jack_latency_range_t port_latency;
	int wanted_delay;
	int c_dly; // current delay
	int w_ptr;
	int r_ptr;
	float out_buffer[MAXPERIOD]; // TODO dynamically allocate, use jack-period
	float delay_buffer[MAXDELAY];
};

struct LV2Port {
	const char *name;
	enum PortType porttype;
	float val_default;
};

typedef struct _RtkLv2Description {
	const LV2_Descriptor* (*lv2_descriptor)(uint32_t index);
	const LV2UI_Descriptor* (*lv2ui_descriptor)(uint32_t index);

	const uint32_t dsp_descriptor_id;
	const uint32_t gui_descriptor_id;
	const char *plugin_human_id;

	const struct LV2Port *ports;

	const uint32_t nports_total;
	const uint32_t nports_audio_in;
	const uint32_t nports_audio_out;
	const uint32_t nports_midi_in;
	const uint32_t nports_midi_out;
	const uint32_t nports_atom_in;
	const uint32_t nports_atom_out;
	const uint32_t nports_ctrl;
	const uint32_t nports_ctrl_in;
	const uint32_t nports_ctrl_out;
	const uint32_t min_atom_bufsiz;
	const bool     send_time_info;
} RtkLv2Description;

RtkLv2Description const *inst;

/* a simple state machine for this client */
static volatile enum {
	Run,
	Exit
} client_state = Run;

struct lv2_external_ui_host extui_host;
struct lv2_external_ui *extui = NULL;

LV2UI_Controller controller = NULL;

LV2_Atom_Forge lv2_forge;
uint32_t *portmap_a_in;
uint32_t *portmap_a_out;
uint32_t *portmap_rctl;
int      *portmap_ctrl;
uint32_t  portmap_atom_to_ui = -1;
uint32_t  portmap_atom_from_ui = -1;

static uint32_t uri_to_id(LV2_URI_Map_Callback_Data callback_data, const char* uri);

struct DelayBuffer **delayline = NULL;
uint32_t worst_capture_latency = 0;
uint32_t plugin_latency = 0;

/******************************************************************************
 * Delayline for latency compensation
 */
#define FADE_LEN (16)

#define INCREMENT_PTRS \
		dly->r_ptr = (dly->r_ptr + 1) % MAXDELAY; \
		dly->w_ptr = (dly->w_ptr + 1) % MAXDELAY;

static float *
delay_port (struct DelayBuffer *dly, uint32_t n_samples, float *in)
{
	uint32_t pos = 0;
	const int delay = dly->wanted_delay;
	const float * const input = in;
	float* const output  = dly->out_buffer;

	if (dly->c_dly == delay && delay == 0) {
		// only copy data into buffer in case delay time changes
		for (; pos < n_samples; pos++) {
			dly->delay_buffer[ dly->w_ptr ] = input[pos];
			INCREMENT_PTRS;
		}
		return in;
	}

	// fade if delaytime changes
	if (dly->c_dly != delay) {
		const uint32_t fade_len = (n_samples >= FADE_LEN) ? FADE_LEN : n_samples / 2;

		// fade out
		for (; pos < fade_len; pos++) {
			const float gain = (float)(fade_len - pos) / (float)fade_len;
			dly->delay_buffer[ dly->w_ptr ] = input[pos];
			output[pos] = dly->delay_buffer[ dly->r_ptr ] * gain;
			INCREMENT_PTRS;
		}

		// update read pointer
		dly->r_ptr += dly->c_dly - delay;
		if (dly->r_ptr < 0) {
			dly->r_ptr -= MAXDELAY * floor(dly->r_ptr / (float)MAXDELAY);
		}

		//printf("Delay changed %d -> %d\n", dly->c_dly, delay); // DEBUG
		dly->r_ptr = dly->r_ptr % MAXDELAY;
		dly->c_dly = delay;

		// fade in
		for (; pos < 2 * fade_len; pos++) {
			const float gain = (float)(pos - fade_len) / (float)fade_len;
			dly->delay_buffer[ dly->w_ptr ] = input[pos];
			output[pos] = dly->delay_buffer[ dly->r_ptr ] * gain;
			INCREMENT_PTRS;
		}
	}

	for (; pos < n_samples; pos++) {
		dly->delay_buffer[ dly->w_ptr ] = input[pos];
		output[pos] = dly->delay_buffer[ dly->r_ptr ];
		INCREMENT_PTRS;
	}

	return dly->out_buffer;
}


///////////////////////////
// GET INFO FROM LV2 TTL //
//     see lv2ttl2c      //
//    define _plugin     //
///////////////////////////
#include JACK_DESCRIPT ////
///////////////////////////


/******************************************************************************
 * JACK
 */

int process (jack_nframes_t nframes, void *arg) {
	while (jack_ringbuffer_read_space(rb_ctrl_from_ui) >= sizeof(uint32_t) + sizeof(float)) {
		uint32_t idx;
		jack_ringbuffer_read(rb_ctrl_from_ui, (char*) &idx, sizeof(uint32_t));
		jack_ringbuffer_read(rb_ctrl_from_ui, (char*) &(plugin_ports_pre[idx]), sizeof(float));
	}

	/* Get Jack transport position */
	jack_position_t pos;
	const bool rolling = (jack_transport_query(j_client, &pos) == JackTransportRolling);
	const bool transport_changed = (rolling != j_transport.rolling
			|| pos.frame != j_transport.position
			|| ((pos.valid & JackPositionBBT) && (pos.beats_per_minute != j_transport.bpm)));

	/* atom buffers */
	if (inst->nports_atom_in > 0 || inst->nports_midi_in > 0) {
		/* start Atom sequence */
		atom_in->atom.type = uri_atom_Sequence;
		atom_in->atom.size = 8;
		LV2_Atom_Sequence_Body *body = &atom_in->body;
		body->unit = 0; // URID of unit of event time stamp LV2_ATOM__timeUnit ??
		body->pad  = 0; // unused
		uint8_t * seq = (uint8_t*) (body + 1);

		if (transport_changed && inst->send_time_info) {
			uint8_t   pos_buf[256];
			LV2_Atom* lv2_pos = (LV2_Atom*)pos_buf;

			lv2_atom_forge_set_buffer(&lv2_forge, pos_buf, sizeof(pos_buf));
			LV2_Atom_Forge* forge = &lv2_forge;
			LV2_Atom_Forge_Frame frame;
#ifdef HAVE_LV2_1_8
			lv2_atom_forge_object(&lv2_forge, &frame, 1, uri_time_Position);
#else
			lv2_atom_forge_blank(&lv2_forge, &frame, 1, uri_time_Position);
#endif
			lv2_atom_forge_property_head(forge, uri_time_frame, 0);
			lv2_atom_forge_long(forge, pos.frame);
			lv2_atom_forge_property_head(forge, uri_time_speed, 0);
			lv2_atom_forge_float(forge, rolling ? 1.0 : 0.0);
			if (pos.valid & JackPositionBBT) {
				lv2_atom_forge_property_head(forge, uri_time_barBeat, 0);
				lv2_atom_forge_float(
						forge, pos.beat - 1 + (pos.tick / pos.ticks_per_beat));
				lv2_atom_forge_property_head(forge, uri_time_bar, 0);
				lv2_atom_forge_long(forge, pos.bar - 1);
				lv2_atom_forge_property_head(forge, uri_time_beatUnit, 0);
				lv2_atom_forge_int(forge, pos.beat_type);
				lv2_atom_forge_property_head(forge, uri_time_beatsPerBar, 0);
				lv2_atom_forge_float(forge, pos.beats_per_bar);
				lv2_atom_forge_property_head(forge, uri_time_beatsPerMinute, 0);
				lv2_atom_forge_float(forge, pos.beats_per_minute);
			}

			uint32_t size = lv2_pos->size;
			uint32_t padded_size = ((sizeof(LV2_Atom_Event) + size) +  7) & (~7);

			if (inst->min_atom_bufsiz > padded_size) {
				printf("send time..\n");
				LV2_Atom_Event *aev = (LV2_Atom_Event *)seq;
				aev->time.frames = 0;
				aev->body.size   = size;
				aev->body.type   = lv2_pos->type;
				memcpy(LV2_ATOM_BODY(&aev->body), LV2_ATOM_BODY(lv2_pos), size);
				atom_in->atom.size += padded_size;
				seq +=  padded_size;
			}
		}
		// TODO only if UI..?
		while (jack_ringbuffer_read_space(rb_atom_from_ui) > sizeof(LV2_Atom)) {
			LV2_Atom a;
			jack_ringbuffer_read(rb_atom_from_ui, (char *) &a, sizeof(LV2_Atom));
			uint32_t padded_size = atom_in->atom.size + a.size + sizeof(int64_t);
			if (inst->min_atom_bufsiz > padded_size) {
				memset(seq, 0, sizeof(int64_t)); // LV2_Atom_Event->time
				seq += sizeof(int64_t);
				jack_ringbuffer_read(rb_atom_from_ui, (char *) seq, a.size);
				seq += a.size;
				atom_in->atom.size += a.size + sizeof(int64_t);
			}
		}
		if (inst->nports_midi_in > 0) {
			/* inject midi events */
			void* buf = jack_port_get_buffer(midi_in, nframes);
			for (uint32_t i = 0; i < jack_midi_get_event_count(buf); ++i) {
				jack_midi_event_t ev;
				jack_midi_event_get(&ev, buf, i);

				uint32_t size = ev.size;
				uint32_t padded_size = ((sizeof(LV2_Atom_Event) + size) +  7) & (~7);

				if (inst->min_atom_bufsiz > padded_size) {
					LV2_Atom_Event *aev = (LV2_Atom_Event *)seq;
					aev->time.frames = ev.time;
					aev->body.size  = size;
					aev->body.type  = uri_midi_MidiEvent;
					memcpy(LV2_ATOM_BODY(&aev->body), ev.buffer, size);
					atom_in->atom.size += padded_size;
					seq += padded_size;
				}
			}
		}
	}

	if (inst->nports_atom_out > 0 || inst->nports_midi_out > 0) {
		atom_out->atom.type = 0;
		atom_out->atom.size = inst->min_atom_bufsiz;
	}

	/* make a backup copy, to see what was changed */
	memcpy(plugin_ports_post, plugin_ports_pre, inst->nports_ctrl * sizeof(float));

	/* expected transport state in next cycle */
	j_transport.position = rolling ? pos.frame + nframes : pos.frame;
	j_transport.bpm      = pos.beats_per_minute;
	j_transport.rolling  = rolling;

	/* [re] connect jack audio buffers */
	for (uint32_t i=0 ; i < inst->nports_audio_out; i++) {
		plugin_dsp->connect_port(plugin_instance, portmap_a_out[i], jack_port_get_buffer (output_port[i], nframes));
	}

	for (uint32_t i=0; i < inst->nports_audio_in; i++) {
		delayline[i]->wanted_delay = worst_capture_latency - delayline[i]->port_latency.max;
		plugin_dsp->connect_port(
				plugin_instance, portmap_a_in[i],
				delay_port(delayline[i], nframes, (float*) jack_port_get_buffer (input_port[i], nframes))
				);
	}

	/* run the plugin */
	plugin_dsp->run(plugin_instance, nframes);

	/* create port-events for change values */
	// TODO only if UI..?
	for (uint32_t p = 0; p < inst->nports_ctrl; p++) {
		if (inst->ports[portmap_rctl[p]].porttype != CONTROL_OUT) continue;

		if (plugin_ports_pre[p] != plugin_ports_post[p]) {
#if 0
			if (TODO this port reportsLatency) {
				plugin_latency = rintf(plugin_ports_pre[p]);
				jack_recompute_total_latencies(j_client);
			}
#endif
			if (jack_ringbuffer_write_space(rb_ctrl_to_ui) >= sizeof(uint32_t) + sizeof(float)) {
				jack_ringbuffer_write(rb_ctrl_to_ui, (char *) &portmap_rctl[p], sizeof(uint32_t));
				jack_ringbuffer_write(rb_ctrl_to_ui, (char *) &plugin_ports_pre[p], sizeof(float));
			}
		}
	}

	if (inst->nports_midi_out > 0) {
		void* buf = jack_port_get_buffer(midi_out, nframes);
		jack_midi_clear_buffer(buf);
	}

	/* Atom sequence port-events */
	if (inst->nports_atom_out + inst->nports_midi_out > 0 && atom_out->atom.size > sizeof(LV2_Atom)) {
		// TODO only if UI..?
		if (jack_ringbuffer_write_space(rb_atom_to_ui) >= atom_out->atom.size + 2 * sizeof(LV2_Atom)) {
			LV2_Atom a = {atom_out->atom.size + (uint32_t) sizeof(LV2_Atom), 0};
			jack_ringbuffer_write(rb_atom_to_ui, (char *) &a, sizeof(LV2_Atom));
			jack_ringbuffer_write(rb_atom_to_ui, (char *) atom_out, a.size);
		}

		if (inst->nports_midi_out) {
			void* buf = jack_port_get_buffer(midi_out, nframes);
			LV2_Atom_Event const* ev = (LV2_Atom_Event const*)((&(atom_out)->body) + 1); // lv2_atom_sequence_begin
			while((const uint8_t*)ev < ((const uint8_t*) &(atom_out)->body + (atom_out)->atom.size)) {
				if (ev->body.type == uri_midi_MidiEvent) {
					jack_midi_event_write(buf, ev->time.frames, (const uint8_t*)(ev+1), ev->body.size);
				}
				ev = (LV2_Atom_Event const*) /* lv2_atom_sequence_next() */
					((const uint8_t*)ev + sizeof(LV2_Atom_Event) + ((ev->body.size + 7) & ~7));
			}
		}
	}

	/* wake up UI */
	if (jack_ringbuffer_read_space(rb_ctrl_to_ui) > sizeof(uint32_t) + sizeof(float)
			|| jack_ringbuffer_read_space(rb_atom_to_ui) > sizeof(LV2_Atom)
			) {
		if (pthread_mutex_trylock (&gui_thread_lock) == 0) {
			pthread_cond_signal (&data_ready);
			pthread_mutex_unlock (&gui_thread_lock);
		}
	}
	return 0;
}


void jack_shutdown (void *arg) {
	fprintf(stderr,"recv. shutdown request from jackd.\n");
	client_state=Exit;
	pthread_cond_signal (&data_ready);
}

int jack_graph_order_cb (void *arg) {
	worst_capture_latency = 0;
	for (uint32_t i = 0; i < inst->nports_audio_in; i++) {
		jack_port_get_latency_range(input_port[i], JackCaptureLatency, &(delayline[i]->port_latency));
		if (delayline[i]->port_latency.max > worst_capture_latency) {
			worst_capture_latency = delayline[i]->port_latency.max;
		}
	}
	return 0;
}

void jack_latency_cb (jack_latency_callback_mode_t mode, void *arg) {
	// assume 1 -> 1 map
	// TODO add systemic latency of plugin (currently no robtk plugins add latency)
	jack_graph_order_cb(NULL); // update worst-case latency, delayline alignment
	if (mode == JackCaptureLatency) {
		for (uint32_t i = 0; i < inst->nports_audio_out; i++) {
			jack_latency_range_t r;
			if (i < inst->nports_audio_in) {
				const uint32_t port_delay = worst_capture_latency - delayline[i]->port_latency.max;
				jack_port_get_latency_range(input_port[i], JackCaptureLatency, &r);
				r.min += port_delay;
				r.max += port_delay;
			} else {
				r.min = r.max = 0;
			}
			r.min += plugin_latency;
			r.max += plugin_latency;
			jack_port_set_latency_range(output_port[i], JackCaptureLatency, &r);
		}
	} else { // JackPlaybackLatency
		for (uint32_t i = 0; i < inst->nports_audio_in; i++) {
			const uint32_t port_delay = worst_capture_latency - delayline[i]->port_latency.max;
			jack_latency_range_t r;
			if (i < inst->nports_audio_out) {
				jack_port_get_latency_range(output_port[i], JackPlaybackLatency, &r);
			} else {
				r.min = r.max = 0;
			}
			r.min += port_delay + plugin_latency;
			r.max += port_delay + plugin_latency;
			jack_port_set_latency_range(input_port[i], JackPlaybackLatency, &r);
		}
	}
}

static int init_jack(const char *client_name) {
	jack_status_t status;
	j_client = jack_client_open (client_name, JackNoStartServer, &status);
	if (j_client == NULL) {
		fprintf (stderr, "jack_client_open() failed, status = 0x%2.0x\n", status);
		if (status & JackServerFailed) {
			fprintf (stderr, "Unable to connect to JACK server\n");
		}
		return (-1);
	}
	if (status & JackServerStarted) {
		fprintf (stderr, "JACK server started\n");
	}
	if (status & JackNameNotUnique) {
		client_name = jack_get_client_name(j_client);
		fprintf (stderr, "jack-client name: `%s'\n", client_name);
	}

	jack_set_process_callback (j_client, process, 0);
	jack_set_graph_order_callback (j_client, jack_graph_order_cb, 0);
	jack_set_latency_callback(j_client, jack_latency_cb, 0);

#ifndef WIN32
	jack_on_shutdown (j_client, jack_shutdown, NULL);
#endif
	j_samplerate=jack_get_sample_rate (j_client);
	return (0);
}

static int jack_portsetup(void) {
	/* Allocate data structures that depend on the number of ports. */
	input_port = (jack_port_t **) malloc (sizeof (jack_port_t *) * inst->nports_audio_in);
	delayline = (struct DelayBuffer **) calloc (inst->nports_audio_in, sizeof (struct DelayBuffer *));

	for (uint32_t i = 0; i < inst->nports_audio_in; i++) {
		if ((input_port[i] = jack_port_register (j_client,
						inst->ports[portmap_a_in[i]].name,
						JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0)) == 0) {
			fprintf (stderr, "cannot register input port \"%s\"!\n", inst->ports[portmap_a_in[i]].name);
			return (-1);
		}
		delayline[i] = (struct DelayBuffer *) calloc (1, sizeof (struct DelayBuffer));
	}

	output_port = (jack_port_t **) malloc (sizeof (jack_port_t *) * inst->nports_audio_out);

	for (uint32_t i = 0; i < inst->nports_audio_out; i++) {
		if ((output_port[i] = jack_port_register (j_client,
						inst->ports[portmap_a_out[i]].name,
						JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0)) == 0) {
			fprintf (stderr, "cannot register output port \"%s\"!\n", inst->ports[portmap_a_out[i]].name);
			return (-1);
		}
	}

	if (inst->nports_midi_in){
		if ((midi_in = jack_port_register (j_client,
						inst->ports[portmap_atom_from_ui].name,
						JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0)) == 0) {
			fprintf (stderr, "cannot register midi input port \"%s\"!\n", inst->ports[portmap_atom_from_ui].name);
			return (-1);
		}
	}

	if (inst->nports_midi_out){
		if ((midi_out = jack_port_register (j_client,
						inst->ports[portmap_atom_to_ui].name,
						JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0)) == 0) {
			fprintf (stderr, "cannot register midi ouput port \"%s\"!\n", inst->ports[portmap_atom_to_ui].name);
			return (-1);
		}
	}
	jack_graph_order_cb (NULL); // query port latencies
	jack_recompute_total_latencies(j_client);
	return (0);
}

/******************************************************************************
 * LV2
 */

static uint32_t uri_to_id(LV2_URI_Map_Callback_Data callback_data, const char* uri) {
	for (uint32_t i=0; i < urimap_len; ++i) {
		if (!strcmp(urimap[i], uri)) {
			//printf("Found mapped URI '%s' -> %d\n", uri, i);
			return i;
		}
	}
	//printf("map URI '%s' -> %d\n", uri, urimap_len);
	urimap = (char**) realloc(urimap, (urimap_len + 1) * sizeof(char*));
	urimap[urimap_len] = strdup(uri);
	return urimap_len++;
}

static void free_uri_map() {
	for (uint32_t i=0; i < urimap_len; ++i) {
		free(urimap[i]);
	}
	free(urimap);
}

void write_function(
		LV2UI_Controller controller,
		uint32_t         port_index,
		uint32_t         buffer_size,
		uint32_t         port_protocol,
		const void*      buffer) {

	if (buffer_size == 0) return;

	if (port_protocol != 0) {
		if (jack_ringbuffer_write_space(rb_atom_from_ui) >= buffer_size + sizeof(LV2_Atom)) {
			LV2_Atom a = {buffer_size, 0};
			jack_ringbuffer_write(rb_atom_from_ui, (char *) &a, sizeof(LV2_Atom));
			jack_ringbuffer_write(rb_atom_from_ui, (char *) buffer, buffer_size);
		}
		return;
	}
	if (buffer_size != sizeof(float)) {
		fprintf(stderr, "LV2Host: write_function() unsupported buffer\n");
		return;
	}
	if (port_index < inst->nports_total && portmap_ctrl[port_index] < 0) {
		fprintf(stderr, "LV2Host: write_function() unmapped port\n");
		return;
	}
	if (jack_ringbuffer_write_space(rb_ctrl_from_ui) >= sizeof(uint32_t) + sizeof(float)) {
		jack_ringbuffer_write(rb_ctrl_from_ui, (char *) &portmap_ctrl[port_index], sizeof(uint32_t));
		jack_ringbuffer_write(rb_ctrl_from_ui, (char *) buffer, sizeof(float));
	}
}


/******************************************************************************
 * MAIN
 */

static void cleanup(int sig) {
	if (j_client) {
		jack_client_close (j_client);
		j_client=NULL;
	}

	if (plugin_dsp && plugin_instance && plugin_dsp->deactivate) {
		plugin_dsp->deactivate(plugin_instance);
	}
	if (plugin_gui && gui_instance && plugin_gui->cleanup) {
		plugin_gui->cleanup(gui_instance);
	}
	if (plugin_dsp && plugin_instance && plugin_dsp->cleanup) {
		plugin_dsp->cleanup(plugin_instance);
	}

	jack_ringbuffer_free(rb_ctrl_to_ui);
	jack_ringbuffer_free(rb_ctrl_from_ui);

	jack_ringbuffer_free(rb_atom_to_ui);
	jack_ringbuffer_free(rb_atom_from_ui);

	free(input_port);
	free(output_port);

	if (delayline) {
		for (uint32_t i = 0; i < inst->nports_audio_in; i++) {
			free(delayline[i]);
		}
	}
	free(delayline);

	free(plugin_ports_pre);
	free(plugin_ports_post);
	free(portmap_a_in);
	free(portmap_a_out);
	free(portmap_ctrl);
	free(portmap_rctl);
	free_uri_map();
	fprintf(stderr, "bye.\n");
}

static void run_one(LV2_Atom_Sequence *data) {

	while (jack_ringbuffer_read_space(rb_ctrl_to_ui) >= sizeof(uint32_t) + sizeof(float)) {
		uint32_t idx;
		float val;
		jack_ringbuffer_read(rb_ctrl_to_ui, (char*) &idx, sizeof(uint32_t));
		jack_ringbuffer_read(rb_ctrl_to_ui, (char*) &val, sizeof(float));
		plugin_gui->port_event(gui_instance, idx, sizeof(float), 0, &val);
	}

	while (jack_ringbuffer_read_space(rb_atom_to_ui) > sizeof(LV2_Atom)) {
		LV2_Atom a;
		jack_ringbuffer_read(rb_atom_to_ui, (char *) &a, sizeof(LV2_Atom));
		assert(a.size < inst->min_atom_bufsiz);
		jack_ringbuffer_read(rb_atom_to_ui, (char *) data, a.size);
		LV2_Atom_Event const* ev = (LV2_Atom_Event const*)((&(data)->body) + 1); // lv2_atom_sequence_begin
		while((const uint8_t*)ev < ((const uint8_t*) &(data)->body + (data)->atom.size)) {
			plugin_gui->port_event(gui_instance, portmap_atom_to_ui,
					ev->body.size, uri_atom_EventTransfer, &ev->body);
			ev = (LV2_Atom_Event const*) /* lv2_atom_sequence_next() */
				((const uint8_t*)ev + sizeof(LV2_Atom_Event) + ((ev->body.size + 7) & ~7));
		}
	}

	LV2_EXTERNAL_UI_RUN(extui);
}

#ifdef __APPLE__

static void osx_loop (CFRunLoopTimerRef timer, void *info) {
	if (client_state == Run) {
		run_one((LV2_Atom_Sequence*)info);
	}
	if (client_state == Exit) {
		rtk_osx_api_terminate();
	}
}

#else


static void main_loop(void) {
	struct timespec timeout;
	LV2_Atom_Sequence *data = (LV2_Atom_Sequence*) malloc(inst->min_atom_bufsiz * sizeof(uint8_t));

	pthread_mutex_lock (&gui_thread_lock);
	while (client_state != Exit) {
		run_one(data);

		if (client_state == Exit) break;

#ifdef _WIN32
		Sleep(1000/UI_UPDATE_FPS);
#else
		clock_gettime(CLOCK_REALTIME, &timeout);
		timeout.tv_nsec += 1000000000 / (UI_UPDATE_FPS);
		if (timeout.tv_nsec >= 1000000000) {timeout.tv_nsec -= 1000000000; timeout.tv_sec+=1;}
#endif
		pthread_cond_timedwait (&data_ready, &gui_thread_lock, &timeout);

	} /* while running */
	free(data);
	pthread_mutex_unlock (&gui_thread_lock);
}
#endif // APPLE RUNLOOP

static void catchsig (int sig) {
	fprintf(stderr,"caught signal - shutting down.\n");
	client_state=Exit;
	pthread_cond_signal (&data_ready);
}

static void on_external_ui_closed(void* controller) {
	catchsig(0);
}

int main (int argc, char **argv) {
	int rv = 0;
	uint32_t c_ain  = 0;
	uint32_t c_aout = 0;
	uint32_t c_ctrl = 0;

#ifdef X42_MULTIPLUGIN
	if (argc > 1 && atoi(argv[1]) < 0) {
		unsigned int i;
		for (i = 0; i < sizeof(_plugins) / sizeof(RtkLv2Description); ++i) {
			const LV2_Descriptor* d = _plugins[i].lv2_descriptor(_plugins[i].dsp_descriptor_id);
			printf("* %d '%s' %s\n", i, _plugins[i].plugin_human_id, d->URI);
		}
		return 0;
	}

	inst = NULL;
	if (argc > 1 && strlen(argv[1]) > 2 && atoi(argv[1]) == 0) {
		unsigned int i;
		for (i = 0; i < sizeof(_plugins) / sizeof(RtkLv2Description); ++i) {
			const LV2_Descriptor* d = _plugins[i].lv2_descriptor(_plugins[i].dsp_descriptor_id);
			if (strstr(d->URI, argv[1]) || strstr(_plugins[i].plugin_human_id, argv[1])) {
				inst = &_plugins[i];
				break;
			}
		}
	}
	if (argc > 1 && !inst && atoi(argv[1]) >= 0) {
		unsigned int plugid = atoi(argv[1]);
		if (plugid < (sizeof(_plugins) / sizeof(RtkLv2Description))) {
			inst = &_plugins[plugid];
		}
	}
	if (!inst) {
		inst = &_plugins[0];
	}
#elif defined X42_PLUGIN_STRUCT
	inst = & X42_PLUGIN_STRUCT;
#else
	inst = &_plugin;
#endif

#ifdef __APPLE__
	rtk_osx_api_init();
#endif

#ifdef USE_WEAK_JACK
	if (have_libjack()) {
		fprintf(stderr, "JACK is not available. http://jackaudio.org/\n");
#ifdef _WIN32
		MessageBox(NULL, TEXT(
					"JACK is not available.\n"
					"You must have the JACK Audio Connection Kit installed to use the tools. "
					"Please see http://jackaudio.org/ and http://jackaudio.org/faq/jack_on_windows.html"
					), TEXT("Error"), MB_ICONERROR | MB_OK);
#elif __APPLE__
		rtk_osx_api_err (
					"JACK is not available.\n"
					"You must have the JACK Audio Connection Kit installed to use the tools. "
					"Please see http://jackaudio.org/ and http://jackosx.com/"
				);
#endif
		return 1;
	}
#endif

#if (defined _WIN32 && defined RTK_STATIC_INIT)
	pthread_win32_process_attach_np();
	glib_init_static();
	gobject_init_ctor();
#endif


	LV2_URID_Map uri_map            = { NULL, &uri_to_id };
	const LV2_Feature map_feature   = { LV2_URID__map, &uri_map};
	const LV2_Feature unmap_feature = { LV2_URID__unmap, NULL };

	const LV2_Feature* features[] = {
		&map_feature, &unmap_feature, NULL
	};

	const LV2_Feature external_lv_feature = { LV2_EXTERNAL_UI_URI, &extui_host};
	const LV2_Feature external_kx_feature = { LV2_EXTERNAL_UI_URI__KX__Host, &extui_host};
	LV2_Feature instance_feature          = { "http://lv2plug.in/ns/ext/instance-access", NULL };

	const LV2_Feature* ui_features[] = {
		&map_feature, &unmap_feature,
		&instance_feature,
		&external_lv_feature,
		&external_kx_feature,
		NULL
	};

	/* check sourced settings */
	assert ((inst->nports_midi_in + inst->nports_atom_in) <= 1);
	assert ((inst->nports_midi_out + inst->nports_atom_out) <= 1);
	assert (inst->plugin_human_id);
	assert (inst->nports_total > 0);

	extui_host.plugin_human_id = inst->plugin_human_id;

	// TODO check if allocs succeeded - OOM -> exit
	/* allocate data structure */
	portmap_a_in  = (uint32_t*) malloc(inst->nports_audio_in * sizeof(uint32_t));
	portmap_a_out = (uint32_t*) malloc(inst->nports_audio_out * sizeof(uint32_t));
	portmap_rctl  = (uint32_t*) malloc(inst->nports_ctrl  * sizeof(uint32_t));
	portmap_ctrl  = (int*)      malloc(inst->nports_total * sizeof(int));

	plugin_ports_pre  = (float*) calloc(inst->nports_ctrl, sizeof(float));
	plugin_ports_post = (float*) calloc(inst->nports_ctrl, sizeof(float));

	atom_in = (LV2_Atom_Sequence*) malloc(inst->min_atom_bufsiz + sizeof(uint8_t));
	atom_out = (LV2_Atom_Sequence*) malloc(inst->min_atom_bufsiz + sizeof(uint8_t));

	rb_ctrl_to_ui = jack_ringbuffer_create((UPDATE_FREQ_RATIO) * inst->nports_ctrl * 2 * sizeof(float));
	rb_ctrl_from_ui = jack_ringbuffer_create((UPDATE_FREQ_RATIO) * inst->nports_ctrl * 2 * sizeof(float));

	rb_atom_to_ui = jack_ringbuffer_create((UPDATE_FREQ_RATIO) * inst->min_atom_bufsiz);
	rb_atom_from_ui = jack_ringbuffer_create((UPDATE_FREQ_RATIO) * inst->min_atom_bufsiz);

	/* reolve descriptors */
	plugin_dsp = inst->lv2_descriptor(inst->dsp_descriptor_id);
	plugin_gui = inst->lv2ui_descriptor(inst->gui_descriptor_id);

	if (!plugin_dsp) {
		fprintf(stderr, "cannot resolve LV2 descriptor\n");
		rv |= 2;
		goto out;
	}
	/* jack-open -> samlerate */
	if (init_jack(extui_host.plugin_human_id)) {
		fprintf(stderr, "cannot connect to JACK.\n");
#ifdef _WIN32
		MessageBox (NULL, TEXT(
					"Cannot connect to JACK.\n"
					"Please start the JACK Server first."
					), TEXT("Error"), MB_ICONERROR | MB_OK);
#elif __APPLE__
		rtk_osx_api_err (
					"Cannot connect to JACK.\n"
					"Please start the JACK Server first."
				);
#endif
		rv |= 4;
		goto out;
	}

	/* init plugin */
	plugin_instance = plugin_dsp->instantiate(plugin_dsp, j_samplerate, NULL, features);
	if (!plugin_instance) {
		fprintf(stderr, "instantiation failed\n");
		rv |= 2;
		goto out;
	}

	/* connect ports */
	for (uint32_t p=0; p < inst->nports_total; ++p) {
		portmap_ctrl[p] = -1;
		switch (inst->ports[p].porttype) {
			case CONTROL_IN:
				plugin_ports_pre[c_ctrl] = inst->ports[p].val_default;
			case CONTROL_OUT:
				portmap_ctrl[p] = c_ctrl;
				portmap_rctl[c_ctrl] = p;
				plugin_dsp->connect_port(plugin_instance, p , &plugin_ports_pre[c_ctrl++]);
				break;
			case AUDIO_IN:
				portmap_a_in[c_ain++] = p;
				break;
			case AUDIO_OUT:
				portmap_a_out[c_aout++] = p;
				break;
			case MIDI_IN:
			case ATOM_IN:
				portmap_atom_from_ui = p;
				plugin_dsp->connect_port(plugin_instance, p , atom_in);
				break;
			case MIDI_OUT:
			case ATOM_OUT:
				portmap_atom_to_ui = p;
				plugin_dsp->connect_port(plugin_instance, p , atom_out);
				break;
			default:
				fprintf(stderr, "yet unsupported port..\n");
				break;
		}
	}

	assert(c_ain == inst->nports_audio_in);
	assert(c_aout == inst->nports_audio_out);
	assert(c_ctrl == inst->nports_ctrl);

	if (inst->nports_atom_out > 0 || inst->nports_atom_in > 0 || inst->nports_midi_in > 0 || inst->nports_midi_out > 0) {
		uri_atom_Sequence       = uri_to_id(NULL, LV2_ATOM__Sequence);
		uri_atom_EventTransfer  = uri_to_id(NULL, LV2_ATOM__eventTransfer);
		uri_midi_MidiEvent      = uri_to_id(NULL, LV2_MIDI__MidiEvent);
		uri_time_Position       = uri_to_id(NULL, LV2_TIME__Position);
		uri_time_frame          = uri_to_id(NULL, LV2_TIME__frame);
		uri_time_speed          = uri_to_id(NULL, LV2_TIME__speed);
		uri_time_bar            = uri_to_id(NULL, LV2_TIME__bar);
		uri_time_barBeat        = uri_to_id(NULL, LV2_TIME__barBeat);
		uri_time_beatUnit       = uri_to_id(NULL, LV2_TIME__beatUnit);
		uri_time_beatsPerBar    = uri_to_id(NULL, LV2_TIME__beatsPerBar);
		uri_time_beatsPerMinute = uri_to_id(NULL, LV2_TIME__beatsPerMinute);
		lv2_atom_forge_init(&lv2_forge, &uri_map);
	}

	if (jack_portsetup()) {
		rv |= 12;
		goto out;
	}

	if (plugin_gui) {
	/* init plugin GUI */
	extui_host.ui_closed = on_external_ui_closed;
	instance_feature.data = plugin_instance;
	gui_instance = plugin_gui->instantiate(plugin_gui,
			plugin_dsp->URI, NULL,
			&write_function, controller,
			(void **)&extui, ui_features);

	}

#ifdef REQUIRE_UI
	if (!gui_instance || !extui) {
		fprintf(stderr, "Error: GUI was not initialized.\n");
		rv |= 2;
		goto out;
	}
#endif

#ifndef _WIN32
	if (mlockall (MCL_CURRENT | MCL_FUTURE)) {
		fprintf(stderr, "Warning: Can not lock memory.\n");
	}
#endif

	if (gui_instance) {
		for (uint32_t p = 0; p < inst->nports_ctrl; p++) {
			if (jack_ringbuffer_write_space(rb_ctrl_to_ui) >= sizeof(uint32_t) + sizeof(float)) {
				jack_ringbuffer_write(rb_ctrl_to_ui, (char *) &portmap_rctl[p], sizeof(uint32_t));
				jack_ringbuffer_write(rb_ctrl_to_ui, (char *) &plugin_ports_pre[p], sizeof(float));
			}
		}
	}

	if (plugin_dsp->activate) {
		plugin_dsp->activate(plugin_instance);
	}

	if (jack_activate (j_client)) {
		fprintf (stderr, "cannot activate client.\n");
		rv |= 20;
		goto out;
	}

#ifndef _WIN32
	signal (SIGHUP, catchsig);
	signal (SIGINT, catchsig);
#endif

	if (!gui_instance || !extui) {
		/* no GUI */
		while (client_state != Exit) {
			sleep (1);
		}
	} else {

		LV2_EXTERNAL_UI_SHOW(extui);

#ifdef __APPLE__
		LV2_Atom_Sequence *data = (LV2_Atom_Sequence*) malloc(inst->min_atom_bufsiz * sizeof(uint8_t));
		CFRunLoopRef runLoop = CFRunLoopGetCurrent();
		CFRunLoopTimerContext context = {0, data, NULL, NULL, NULL};
		CFRunLoopTimerRef timer = CFRunLoopTimerCreate(kCFAllocatorDefault, 0, 1.0/UI_UPDATE_FPS, 0, 0, &osx_loop, &context);
		CFRunLoopAddTimer(runLoop, timer, kCFRunLoopCommonModes);
		rtk_osx_api_run();
		free(data);
#else

		main_loop();
#endif

		LV2_EXTERNAL_UI_HIDE(extui);
	}

out:
	cleanup(0);
#if (defined _WIN32 && defined RTK_STATIC_INIT)
	pthread_win32_process_detach_np();
	glib_cleanup_static();
#endif
	return(rv);
}
/* vi:set ts=2 sts=2 sw=2: */
