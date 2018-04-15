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

#include "overdrive.h"
#include <getopt.h>
#include <jack/jack.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifndef _WIN32
#include <signal.h>
#endif

static jack_client_t* j_client = NULL;
static jack_port_t*   j_input_port;
static jack_port_t*   j_output_port;

static char* jack_inport_name  = NULL;
static char* jack_outport_name = NULL;

float p_bias      = 0.87399;
float p_feedback  = 0.5821;
float p_sagtobias = 0.1880;
float p_postfeed  = 1.0;
float p_globfeed  = 0.5826;
float p_gainin    = 0.3567;
float p_gainout   = 0.07873;

void
useMIDIControlFunction (void* m, const char* cfname, void (*f) (void*, unsigned char), void* d)
{
}
int
getConfigParameter_fr (const char* par, ConfigContext* cfg, float* fp, float lowInc, float highInc)
{
	return 0;
}
int
getConfigParameter_f (const char* par, ConfigContext* cfg, float* fp)
{
	return 0;
}

void
cleanup ()
{
	jack_deactivate (j_client);
	jack_client_close (j_client);
	j_client = NULL;
}

#ifdef HAVE_LIBLO
#include <lo/lo.h>
lo_server_thread osc_server = NULL;
int              silent     = 0;

static void
oscb_error (int num, const char* m, const char* path)
{
	fprintf (stderr, "liblo server error %d in path %s: %s\n", num, path, m);
}

int
oscb_quit (const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data)
{
	fprintf (stderr, "received OSC shutdown request.\n");
	cleanup ();
	return (0);
}

#define OSC_CALLBACK(PARAM, FN)                                                                                          \
	int oscb_##PARAM (const char* path, const char* types, lo_arg** argv, int argc, lo_message msg, void* user_data) \
	{                                                                                                                \
		float v = argv[0]->f;                                                                                    \
		if (v < 0 || v > 1.0)                                                                                    \
			return (0);                                                                                      \
		PARAM = v;                                                                                               \
		FN (NULL, PARAM);                                                                                        \
		return (0);                                                                                              \
	}

OSC_CALLBACK (p_bias, fctl_biased)
OSC_CALLBACK (p_feedback, fctl_biased_fb)
OSC_CALLBACK (p_sagtobias, fctl_sagtoBias)
OSC_CALLBACK (p_postfeed, fctl_biased_fb2)
OSC_CALLBACK (p_globfeed, fctl_biased_gfb)
OSC_CALLBACK (p_gainin, fsetInputGain)
OSC_CALLBACK (p_gainout, fsetOutputGain)

int
init_osc (int osc_port)
{
	char tmp[8];
	if (osc_port < 0)
		return (1);
	if (osc_server)
		return (1);
	uint32_t port = (osc_port > 100 && osc_port < 60000) ? osc_port : 8998;

	snprintf (tmp, sizeof (tmp), "%d", port);
	if (!silent)
		fprintf (stderr, "OSC trying port: %i\n", port);
	osc_server = lo_server_thread_new (tmp, oscb_error);

	if (!osc_server) {
		if (!silent)
			fprintf (stderr, "OSC start failed.");
		return (1);
	}

	if (!silent) {
		char* urlstr;
		urlstr = lo_server_thread_get_url (osc_server);
		fprintf (stderr, "OSC server name: %s\n", urlstr);
		free (urlstr);
	}

	lo_server_thread_add_method (osc_server, "/boverdrive/bias", "f", &oscb_p_bias, NULL);
	lo_server_thread_add_method (osc_server, "/boverdrive/feedback", "f", &oscb_p_feedback, NULL);
	lo_server_thread_add_method (osc_server, "/boverdrive/sagtobias", "f", &oscb_p_sagtobias, NULL);
	lo_server_thread_add_method (osc_server, "/boverdrive/postfeed", "f", &oscb_p_postfeed, NULL);
	lo_server_thread_add_method (osc_server, "/boverdrive/globfeed", "f", &oscb_p_globfeed, NULL);
	lo_server_thread_add_method (osc_server, "/boverdrive/gainin", "f", &oscb_p_gainin, NULL);
	lo_server_thread_add_method (osc_server, "/boverdrive/gainout", "f", &oscb_p_gainout, NULL);
	lo_server_thread_add_method (osc_server, "/boverdrive/quit", "", &oscb_quit, NULL);

	lo_server_thread_start (osc_server);
	if (!silent)
		fprintf (stderr, "OSC server started on port %i\n", port);
	return (0);
}

void
shutdown_osc (void)
{
	if (!osc_server)
		return;
	lo_server_thread_stop (osc_server);
	lo_server_thread_free (osc_server);
	if (!silent)
		fprintf (stderr, "OSC server shut down.\n");
	osc_server = NULL;
}
#endif

void
catchsig (int sig)
{
#ifndef _WIN32
	signal (SIGHUP, catchsig); /* reset signal */
#endif
	fprintf (stderr, "caught signal - shutting down.\n");
	cleanup ();
}

int
jack_audio_callback (jack_nframes_t nframes, void* arg)
{
	jack_default_audio_sample_t *in, *out;
	in  = (jack_default_audio_sample_t*)jack_port_get_buffer (j_input_port, nframes);
	out = (jack_default_audio_sample_t*)jack_port_get_buffer (j_output_port, nframes);
	overdrive (arg, in, out, nframes);
	return (0);
}

void
jack_shutdown_callback (void* arg)
{
	fprintf (stderr, "jack server shut us down.\n");
	cleanup ();
}

static void
connect_jack_ports ()
{
	if (jack_inport_name) {
		if (jack_connect (j_client, jack_inport_name, jack_port_name (j_input_port))) {
			fprintf (stderr, "cannot connect port %s to %s\n", jack_inport_name, jack_port_name (j_input_port));
		}
	}
	if (jack_outport_name) {
		if (jack_connect (j_client, jack_port_name (j_output_port), jack_outport_name)) {
			fprintf (stderr, "cannot connect port %s to %s\n", jack_port_name (j_output_port), jack_outport_name);
		}
	}
}

static void
usage (const char* name, int status)
{
	fprintf (status ? stderr : stdout,
	         "%s - the B Preamp/Overdrive Emulator\n\n", name);
	fprintf (status ? stderr : stdout,
	         "Utility to write audio peak data to stdout of file as plain text of JSON data.\n"
	         "\n");
	fprintf (status ? stderr : stdout,
	         "Usage: %s [ OPTIONS ]\n\n", name);
	fprintf (status ? stderr : stdout,
	         "Options:\n"
	         "  -h, --help                    Print help text\n"
	         "  -i, --input <portname>        Connect to JACK input port\n"
	         "  -o, --output <portname>       Connect output to given JACK port\n"
	         "  -p, --parameter <key=value>   Specify initial effect parameter\n"
	         "  -V, --version                 Print version information\n"
	         "\n"
	         "Effect parameters are all float values 0..1.\n"
	         "Available keys and their default values are as follows:\n"
	         "  gainin:     Input Gain          = 0.3567\n"
	         "  bias:       Bias                = 0.87399\n"
	         "  feedback:   Feedback            = 0.5821\n"
	         "  sagtobias:  Sag to Bias         = 0.1880\n"
	         "  postfeed:   Postdiff Feedback   = 1.0\n"
	         "  globfeed:   Global Feedback     = 0.5826\n"
	         "  gainout:    Output Gain         = 0.07873\n"
	         "\n"
	         "Examples:\n");
	fprintf (status ? stderr : stdout,
	         "%s -i system:capture_1 -o system:playback_1 -p bias=.5 -p feedback=0.9\n", name);
	fprintf (status ? stderr : stdout,
	         "\n"
	         "%s -i system:capture_1 -o system:playback_1 -O 1234\n",
	         name);
	fprintf (status ? stderr : stdout,
	         "\n"
	         "oscsend localhost 1234 /boverdrive/bias f 0.9\n\n"
	         "oscsend localhost 1234 /boverdrive/gainout f 0.3\n\n"
	         "oscsend localhost 1234 /boverdrive/quit\n\n"
	         "\n"
	         "Report bugs to <robin@gareus.org>.\n"
	         "Website and manual: <https://github.com/x42/setbfree>\n");
	exit (status);
}

float
pp (const char* ps)
{
	float p = atof (ps);
	if (p < 0)
		p = 0;
	if (p > 1.0)
		p = 1.0;
	return (p);
}

int
main (int argc, char** argv)
{
	int   osc_port = 0;
	void* pa       = allocPreamp ();
	initPreamp (pa, NULL);

	int           c;
	const char*   optstring      = "hi:o:O:p:V";
	struct option long_options[] = {
		{ "help", no_argument, 0, 'h' },
		{ "input", no_argument, 0, 'i' },
		{ "output", no_argument, 0, 'o' },
		{ "parameter", required_argument, 0, 'p' },
		{ "version", no_argument, 0, 'V' },
		{ 0, 0, 0, 0 }
	};

	while ((c = getopt_long (argc, argv, optstring, long_options, NULL)) != -1) {
		switch (c) {
			case 'h':
				usage ("jboverdrive", 0);
				return (0);
				break;
			case 'i':
				jack_inport_name = optarg;
				break;
			case 'o':
				jack_outport_name = optarg;
				break;
			case 'O':
				osc_port = atoi (optarg);
				break;
			case 'p': {
				int   ok = 0;
				char* t  = strchr (optarg, '=');
				if (t) {
					*t = '\0';
					if (!strcasecmp (optarg, "bias")) {
						ok     = 1;
						p_bias = pp (t + 1);
					}
					if (!strcasecmp (optarg, "feedback")) {
						ok         = 1;
						p_feedback = pp (t + 1);
					}
					if (!strcasecmp (optarg, "sagtobias")) {
						ok          = 1;
						p_sagtobias = pp (t + 1);
					}
					if (!strcasecmp (optarg, "postfeed")) {
						ok         = 1;
						p_postfeed = pp (t + 1);
					}
					if (!strcasecmp (optarg, "globfeed")) {
						ok         = 1;
						p_globfeed = pp (t + 1);
					}
					if (!strcasecmp (optarg, "gainin")) {
						ok       = 1;
						p_gainin = pp (t + 1);
					}
					if (!strcasecmp (optarg, "gainout")) {
						ok        = 1;
						p_gainout = pp (t + 1);
					}
					*t = '=';
				}
				if (!ok) {
					fprintf (stderr, "invalid parameter '%s' given.\n", optarg);
				}
			} break;
			case 'V':
				printf ("%s %s\n\n", "jboverdrive", VERSION);
				printf (
				    "Copyright (C) 2003-2004 Fredrik Kilander <fk@dsv.su.se>\n"
				    "Copyright (C) 2012-2018 Robin Gareus <robin@gareus.org>\n"
				    "\n"
				    "This is free software; see the source for copying conditions.  There is NO\n"
				    "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n");
				return (0);
			default:
				fprintf (stderr, "invalid argument.\n");
				usage ("jboverdrive", 1);
		}
	}

	if (osc_port) {
#ifdef HAVE_LIBLO
		if (init_osc (osc_port))
			osc_port = 0;
#else
		fprintf (stderr, "This version has not been compiled with liblo.\nOSC is not available.\n");
#endif
	}

	j_client = jack_client_open ("b_overdrive", JackNullOption, NULL);
	if (!j_client) {
		fprintf (stderr, "could not connect to jack.\n");
		return (1);
	}

	j_input_port  = jack_port_register (j_client, "in", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	j_output_port = jack_port_register (j_client, "out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

	if (!j_output_port || !j_input_port) {
		fprintf (stderr, "no more jack ports available.\n");
		jack_client_close (j_client);
		return (1);
	}

	jack_on_shutdown (j_client, jack_shutdown_callback, NULL);
	jack_set_process_callback (j_client, jack_audio_callback, pa);

#ifndef _WIN32
	signal (SIGHUP, catchsig);
	signal (SIGINT, catchsig);
#endif

	fctl_biased (pa, p_bias);
	fctl_biased_fb (pa, p_feedback);
	fctl_sagtoBias (pa, p_sagtobias);
	fctl_biased_fb2 (pa, p_postfeed);
	fctl_biased_gfb (pa, p_globfeed);
	fsetInputGain (pa, p_gainin);
	fsetOutputGain (pa, p_gainout);

	jack_activate (j_client);
	connect_jack_ports ();

	while (j_client) {
		// TODO interaction; allow to change parameters
		// via terminal I/O
		sleep (1);
	}

#ifdef HAVE_LIBLO
	if (osc_port) {
		shutdown_osc ();
	}
#endif

	freePreamp (pa);
	return (0);
}
