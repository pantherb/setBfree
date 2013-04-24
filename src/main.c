/* setBfree - DSP tonewheel organ
 *
 * Copyright (C) 2003-2004 Fredrik Kilander <fk@dsv.su.se>
 * Copyright (C) 2008-2012 Robin Gareus <robin@gareus.org>
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <ctype.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>
#include <getopt.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <sys/mman.h>

#ifndef _WIN32
#include <signal.h>
#endif
#include "global_inst.h"
#include "vibrato.h"
#include "main.h"
#include "state.h"
#include "pgmParser.h"
#include "program.h"

#ifdef HAVE_ASEQ
#include "midi_aseq.h"
#endif

#ifdef HAVE_ZITACONVOLVE
#include "convolution.h"
#endif

#ifdef HAVE_SRC
#include<samplerate.h>
#include<math.h>
#endif

#define NOF_CFG_OVERS (32)	/* Max command-line config commands */

#ifndef SHAREDIR
#define SHAREDIR "."
#endif

static const char * templateConfigFile = SHAREDIR "/cfg/default.cfg";
static const char * templateProgrammeFile = SHAREDIR "/pgm/default.pgm";

static char * defaultConfigFile = NULL;
static char * defaultProgrammeFile = NULL;

#define AUDIO_CHANNELS (2)
#define CHN_LEFT (0)
#define CHN_RIGHT (1)

#ifdef HAVE_ASEQ
int aseq_stop = 0;
#endif
static int use_jack_midi = 1;

static char *midi_port = NULL;
static char *jack_ports = NULL;
static char *jack_port[AUDIO_CHANNELS];

static const char *portnames[AUDIO_CHANNELS] = {
  "out_left", "out_right"
};

static const char *midiport = "midi_in";
static const char *jack_client_name = "setBfree";

static float bufA [BUFFER_SIZE_SAMPLES];
static float bufB [BUFFER_SIZE_SAMPLES];
static float bufC [BUFFER_SIZE_SAMPLES];

double SampleRateD = 48000.0;
int    SampleRateI = 48000;

static jack_client_t *j_client = NULL;
static jack_port_t **j_output_port;
static jack_port_t  *jack_midi_port;
static jack_default_audio_sample_t **j_output_bufferptrs;
static jack_default_audio_sample_t bufJ [2][BUFFER_SIZE_SAMPLES];
#ifdef HAVE_ZITACONVOLVE
static jack_default_audio_sample_t bufH [2][BUFFER_SIZE_SAMPLES];
static jack_default_audio_sample_t bufD [2][BUFFER_SIZE_SAMPLES];
#endif
static int synth_ready = 0;

b_instance inst;

void mixdown (float **inout, const float **in2, int nchannels, int nsamples) {
  int c,i;
  for (c=0; c < nchannels; c++)
    for (i=0; i < nsamples; i++)
      inout[c][i] += in2[c][i];
}

void cleanup() {
  if (j_client) {
    jack_deactivate(j_client);
    jack_client_close (j_client);
  }
  j_client=NULL;
}

/* when jack shuts down... */
void jack_shutdown_callback(void *arg) {
  fprintf(stderr,"jack server shut us down.\n");
  j_client=NULL;
  //cleanup();
}

int jack_srate_callback(jack_nframes_t nframes, void *arg) {
  SampleRateI = nframes;
  SampleRateD = (double) SampleRateI;
  return(0);
}

#ifndef MIN
#define MIN(A,B) (((A)<(B))?(A):(B))
#endif

int jack_audio_callback (jack_nframes_t nframes, void *arg) {
  jack_default_audio_sample_t **out = j_output_bufferptrs;
  int i;

  void *jack_midi_buf = NULL;
  int midi_events = 0;
  jack_nframes_t midi_tme_proc = 0;

  if (!synth_ready) {
    for (i=0;i<AUDIO_CHANNELS;i++) {
      out[i] = jack_port_get_buffer (j_output_port[i], nframes);
      memset(out[i], 0, nframes*sizeof(jack_default_audio_sample_t));
    }
    return (0);
  }

  if (use_jack_midi) {
    jack_midi_buf = jack_port_get_buffer(jack_midi_port, nframes);
    midi_events = jack_midi_get_event_count(jack_midi_buf);
  }

  for (i=0;i<AUDIO_CHANNELS;i++) {
    out[i] = jack_port_get_buffer (j_output_port[i], nframes);
  }

  static int boffset = BUFFER_SIZE_SAMPLES;

  jack_nframes_t written = 0;

  while (written < nframes) {
    int nremain = nframes - written;

    if (boffset >= BUFFER_SIZE_SAMPLES)  {
      if (use_jack_midi) {
	for (i=0; i<midi_events; i++) {
	  jack_midi_event_t ev;
	  jack_midi_event_get(&ev, jack_midi_buf, i);
	  if (ev.time >= written && ev.time < (written + BUFFER_SIZE_SAMPLES))
	    parse_raw_midi_data(arg, ev.buffer, ev.size);
	}
	midi_tme_proc = written + BUFFER_SIZE_SAMPLES;
      }
      boffset = 0;
      oscGenerateFragment (inst.synth, bufA, BUFFER_SIZE_SAMPLES);
      preamp (inst.preamp, bufA, bufB, BUFFER_SIZE_SAMPLES);
      reverb (inst.reverb, bufB, bufC, BUFFER_SIZE_SAMPLES);

#ifdef HAVE_ZITACONVOLVE
      whirlProc2(inst.whirl,
	  bufC,
	  NULL, NULL,
	  bufH[0], bufH[1],
	  bufD[0], bufD[1],
	  BUFFER_SIZE_SAMPLES);

      const float *horn[2] = { bufH[0], bufH[1] };
      const float *drum[2] = { bufD[0], bufD[1] };
      float *out[2] = { bufJ[0], bufJ[1] };

      convolve(horn, out, 2, BUFFER_SIZE_SAMPLES);
      mixdown(out, drum, AUDIO_CHANNELS, BUFFER_SIZE_SAMPLES);
#else
      whirlProc(inst.whirl, bufC, bufJ[0], bufJ[1], BUFFER_SIZE_SAMPLES);
#endif
    }

    int nread = MIN(nremain, (BUFFER_SIZE_SAMPLES - boffset));

    for (i=0;i< AUDIO_CHANNELS; i++) {
      memcpy(&out[i][written], &bufJ[i][boffset], nread*sizeof(float));
    }
    written+=nread;
    boffset+=nread;
  }

  if (use_jack_midi) {
    /* process remaining MIDI events
     * IF nframes < BUFFER_SIZE_SAMPLES OR nframes != N * BUFFER_SIZE_SAMPLES
     */
    for (i=0; i<midi_events; i++) {
      jack_midi_event_t ev;
      jack_midi_event_get(&ev, jack_midi_buf, i);
      if (ev.time >= midi_tme_proc)
	parse_raw_midi_data(arg, ev.buffer, ev.size);
    }
  }

  return(0);
}

int open_jack(void) {
  int i;
  j_client = jack_client_open (jack_client_name, JackNullOption, NULL);

  if (!j_client) {
    fprintf(stderr, "could not connect to jack.\n");
    return(1);
  }

  jack_on_shutdown (j_client, jack_shutdown_callback, NULL);
  jack_set_process_callback(j_client,jack_audio_callback, &inst);
  jack_set_sample_rate_callback (j_client, jack_srate_callback, NULL);

  j_output_port= calloc(AUDIO_CHANNELS,sizeof(jack_port_t*));
  j_output_bufferptrs = calloc(AUDIO_CHANNELS,sizeof(jack_default_audio_sample_t*));

  for (i=0;i<AUDIO_CHANNELS;i++) {
#if 0
    char channelid[16];
    snprintf(channelid,16,"output-%i",i);
#else
    const char *channelid = portnames[i];
#endif
    j_output_port[i] = jack_port_register (j_client, channelid, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (!j_output_port[i]) {
      fprintf(stderr, "no more jack ports available.\n");
      jack_client_close (j_client);
      return(1);
    }
  }

  if (use_jack_midi) { // use JACK-midi
    jack_midi_port = jack_port_register(j_client, midiport, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput , 0);
    if (jack_midi_port == NULL) {
      fprintf(stderr, "can't register jack-midi-port\n");
	jack_client_close (j_client);
	return(1);
    }
  }


  jack_srate_callback(jack_get_sample_rate(j_client),NULL); // force geting samplerate

  return jack_activate(j_client);
}

static void connect_jack_ports() {
  int i;
  int c=0;
  for (i = 0; i < AUDIO_CHANNELS; i++) {
    if (!jack_port[i]) continue;
    if (jack_connect(j_client, jack_port_name(j_output_port[i]), jack_port[i] )) {
      fprintf(stderr, "cannot connect port %s to %s\n", jack_port_name(j_output_port[i]), jack_port[i]);
    }
    c++;
  }

  if (c==0 && jack_ports && strlen(jack_ports)>0) {
    /* all of jack_port[] were NULL, try regexp */
    const char **found_ports = jack_get_ports(j_client, jack_ports, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
    if (found_ports) {
      for (i = 0; found_ports[i] && i < AUDIO_CHANNELS; ++i) {
	if (jack_connect(j_client, jack_port_name(j_output_port[i]), found_ports[i])) {
	  fprintf(stderr, "JACK: cannot connect port %s to %s\n", jack_port_name(j_output_port[i]), found_ports[i]);
	}
      }
      jack_free(found_ports);
    }
  }

  /* Midi Port */
  if (use_jack_midi && midi_port && strlen(midi_port)>0) {
    const char **found_ports = jack_get_ports(j_client, midi_port, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput);
    if (found_ports) {
      for (i = 0; found_ports[i]; ++i) {
	if (jack_connect(j_client, found_ports[i], jack_port_name(jack_midi_port))) {
	  fprintf(stderr, "JACK: cannot connect port %s to %s\n", found_ports[i], jack_port_name(jack_midi_port));
	}
      }
      jack_free(found_ports);
    }
  }
}

/*
 * Instantiate /classes/.
 */
static void allocAll () {
  if (! (inst.state = allocRunningConfig())) {
    fprintf (stderr, "FATAL: memory allocation failed for state memory.\n");
    exit(1);
  }
  if (! (inst.progs = allocProgs())) {
    fprintf (stderr, "FATAL: memory allocation failed for program config.\n");
    exit(1);
  }
  if (! (inst.reverb = allocReverb())) {
    fprintf (stderr, "FATAL: memory allocation failed for reverb.\n");
    exit(1);
  }
  if (! (inst.whirl = allocWhirl())) {
    fprintf (stderr, "FATAL: memory allocation failed for whirl.\n");
    exit(1);
  }
  if (! (inst.synth = allocTonegen())) {
    fprintf (stderr, "FATAL: memory allocation failed for tonegen.\n");
    exit(1);
  }
  if (! (inst.midicfg = allocMidiCfg(inst.state))) {
    fprintf (stderr, "FATAL: memory allocation failed for midi config.\n");
    exit(1);
  }
  if (! (inst.preamp = allocPreamp())) {
    fprintf (stderr, "FATAL: memory allocation failed for midi config.\n");
    exit(1);
  }
}

/*
 * delete /class/ instances
 */
static void freeAll () {
  freeReverb(inst.reverb);
  freeWhirl(inst.whirl);

  freeToneGenerator(inst.synth);
  freeMidiCfg(inst.midicfg);
  freePreamp(inst.preamp);
  freeProgs(inst.progs);
  freeRunningConfig(inst.state);
#ifdef HAVE_ZITACONVOLVE
  freeConvolution();
#endif
}

/*
 * Ask each module to initialize itself.
 */
static void initAll () {
  fprintf (stderr, "init.. ");
  fprintf (stderr, "Audio : ");
  fflush (stderr);

  /* Open the JACK connection and get samplerate. */
  if (open_jack()) {
    perror ("could not connect to JACK.");
    exit(1);
  }

  fprintf (stderr, "Oscillators : ");
  fflush (stderr);
  initToneGenerator (inst.synth, inst.midicfg);

  fprintf (stderr, "Scanner : ");
  fflush (stderr);
  initVibrato (inst.synth, inst.midicfg);

  fprintf (stderr, "Overdrive : ");
  fflush (stderr);
  initPreamp (inst.preamp, inst.midicfg);

  fprintf (stderr, "Reverb : ");
  fflush (stderr);
  initReverb (inst.reverb, inst.midicfg, SampleRateD);

  fprintf (stderr, "Whirl : ");
  fflush (stderr);
  initWhirl (inst.whirl, inst.midicfg, SampleRateD);

#ifdef HAVE_ZITACONVOLVE
  fprintf (stderr, "Convolve : ");
  fflush (stderr);
  int s_policy= 0;
  struct sched_param s_param;

  memset (&s_param, 0, sizeof(struct sched_param));
  if (jack_client_thread_id(j_client)) {
    pthread_getschedparam(jack_client_thread_id(j_client), &s_policy, &s_param);
  } else {
    fprintf(stderr, "zita-convolver: not using RT scheduling\n");
  }
  initConvolution(NULL, inst.midicfg, AUDIO_CHANNELS, BUFFER_SIZE_SAMPLES, s_param.sched_priority, s_policy);
#endif

  fprintf (stderr, "RC : ");
  fflush (stderr);
  initRunningConfig(inst.state, inst.midicfg);

  fprintf (stderr, "..done.\n");
}


/*
 * Print rudimentary commandline syntax.
 */
static void Usage (int configdoc) {
  const char *name ="setBfree";
  printf (
  "%s - DSP tonewheel organ\n\n"
  "setBfree is a MIDI-controlled, software synthesizer designed to imitate\n"
  "the sound and properties of the electromechanical organs and sound\n"
  "modification devices that brought world-wide fame to the names and\n"
  "products of Laurens Hammond and Don Leslie.\n\n", name);
  printf (
  "Usage: %s [ OPTIONS ] [ property=value ... ]\n\n", name);
  printf (
  "Options:\n"
  "  -c <filename>, --config <filename>\n"
  "                    Load alternate config file over default\n"
  "  -C, --noconfig    Do not read the default configuration file\n"
  "                    the equivalent built-in defaults are still set\n"
  "  -d, --dumpcc      Print a list of MIDI-CC mappings on startup\n"
  "  -D, --noCC        do not load default CC map on startup\n"
  "  -h                Print short help text\n"
  "  -H, --help        Print complete help text with parameter list\n"
  "  -M <filename>, --midnam <filename>\n"
  "                    export current controller mapping to .midnam file\n"
  "  -p <filename>, --program <filename>\n"
  "                    Load alternate program file over default\n"
  "  -P, --noprogram   Do not read the default program file\n"
  "                    the built-in programs are cleared as well\n"
  "  -r, --randomize   Randomize initial preset (whacky but true)\n"
  "  -V, --version     Print version information\n"
  "\n");
  if (configdoc) {
    printf (
  "General Information:\n"
  "  The configuration consists of two parts: program (pgm) and config (cfg).\n"
  "  \n"
  "  The static configuration of the properties of the instrument is defined in\n"
  "  a .cfg file. There is no need to specify a config-file, as all configurable\n"
  "  parameters have built-in default values. They can be overridden on startup\n"
  "  using 'property=value' pairs or by loading a specific .cfg file.\n"
  "  As the name /static/ implies, the properties can only be set on application\n"
  "  start. Yet many of the properties merely define the initial value of\n"
  "  settings which can later be modified during playback. The ones which can be\n"
  "  dynamically modified are marked with an asterisk (*)\n"
  "  \n"
  "  Properties are modified by sending MIDI Control-Commands (CC) to the synth.\n"
  "  The mapping of CCs to function can be modified by setting the\n"
  "  \"midi.controller.{upper|lower|pedal}.<CC>=<function>\" property.\n"
  "  Function-names are equivalent to property-names.\n"
  "  e.g. \"midi.controller.upper.22=overdrive.outputgain\"\n"
  "  assigns the overdrive-gain to MIDI-CC 22 on MIDI-channel 1 (upper)\n"
  "  (Note: each function can assigned only once, however MIDI-CC can be re-used\n"
  "  and trigger multiple function at the same time.)\n"
  "  \n"
  "  The program basically defines 'shortcuts'. Loading a program is usually\n"
  "  equivalent to sending a series of CC. Programs are commonly used to define\n"
  "  instruments (e.g. draw-bar settings to mimic a flute) or provide scale-\n"
  "  points (e.g. reverb=64).  There are a few special commands which are\n"
  "  only available by recalling a program (randomize settings, split-manuals,\n"
  "  enable overdrive).\n"
  "  \n"
  "  Programs are defined in a .pgm file and are fixed after starting\n"
  "  the application. They are activated by sending MIDI-program-change\n"
  "  messages (also known as 'presets') MIDI-banks are ignored. So at most\n"
  "  127 programs can be specified.\n"
  "\n"
  "  At startup 'default.cfg' and 'default.pgm' in $XDG_CONFIG_HOME/setBfree/\n"
  "  (default: $HOME/.config/setBfree/) are are evaluated if the files exist,\n"
  "  unless '--noconfig' or '--noprogram' options are given.\n"
  "  An additional config or program file can be loaded using the '-c' and '-p'\n"
  "  option respectively.\n"
  "\n"
  );
    printf ("  Example config: \"%s\"\n\n  Example program: \"%s\"\n\n",
	templateConfigFile, templateProgrammeFile);
    dumpConfigDoc();
  }
  printf (
  "Examples:\n");
  printf (
  "%s\n", name);
  printf (
  "%s -p pgm/default.pgm midi.port=\"a2j:[AV]\" midi.driver=jack\n", name);
  printf (
  "%s midi.port=129 midi.driver=alsa jack.connect=jack_rack:in_\n", name);
  printf (
  "%s jack.out.left=system:playback_7 jack.out.right=system:playback_8\n", name);
  printf (
  "\n"
  "Report bugs at <http://github.com/pantherb/setBfree/issues>.\n"
  "Website and manual: <http://setbfree.org>\n"
  );
}
static void PrintVersion () {
  const char *name ="setBfree";
  printf ("%s %s\n\n", name, VERSION);
  printf(
"Copyright (C) 2003-2004 Fredrik Kilander <fk@dsv.su.se>\n"
"Copyright (C) 2008-2012 Robin Gareus <robin@gareus.org>\n"
"Copyright (C) 2010 Ken Restivo <ken@restivo.org>\n"
"Copyright (C) 2012 Will Panther <pantherb@setbfree.org>\n"
"\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n"
);
}

static const ConfigDoc doc[] = {
  {"midi.driver", CFG_TEXT, "\"jack\"", "The midi driver to use, 'jack' or 'alsa'"},
  {"midi.port", CFG_TEXT, "\"\"", "The midi port(s) to auto-connect to. With alsa it's a single port-name or number, jack accepts regular expressions."},
  {"jack.connect", CFG_TEXT, "\"system:playback_\"", "Auto connect both audio-ports to a given regular-expression. This setting is ignored if either of jack.out.{left|right} is specified."},
  {"jack.out.left", CFG_TEXT, "\"\"", "Connect left-output to this jack-port (exact name)"},
  {"jack.out.right", CFG_TEXT, "\"\"", "Connect right-output to this jack-port (exact name)"},
  {NULL}
};

int mainConfig (ConfigContext * cfg) {
  int ack = 0;
  if (strcasecmp (cfg->name, "midi.driver") == 0) {
    if (strcasecmp (cfg->value, "jack") == 0) {
      use_jack_midi=1; ack++;
    }
    else if (strcasecmp (cfg->value, "alsa") == 0) {
#ifdef HAVE_ASEQ
      use_jack_midi=0;
#endif
      ack++;
    }
  }
  else if (strcasecmp (cfg->name, "midi.port") == 0) {
    ack++;
    free(midi_port);
    midi_port=strdup(cfg->value);
  }
  else if (strcasecmp (cfg->name, "jack.connect") == 0) {
    ack++;
    free(jack_ports);
    jack_ports=strdup(cfg->value);
  }
  else if (strcasecmp (cfg->name, "jack.out.left") == 0) {
    ack++;
    free(jack_port[CHN_LEFT]);
    jack_port[CHN_LEFT]=strdup(cfg->value);
  }
  else if (strcasecmp (cfg->name, "jack.out.right") == 0) {
    ack++;
    free(jack_port[CHN_RIGHT]);
    jack_port[CHN_RIGHT]=strdup(cfg->value);
  }
  return ack;
}

void catchsig (int sig) {
#ifndef _WIN32
  signal(SIGHUP, catchsig); /* reset signal */
#endif
  fprintf(stderr,"caught signal - shutting down.\n");
  cleanup();
}

const ConfigDoc *mainDoc () {
  return doc;
}

/*
 * Main program.
 */
int main (int argc, char * argv []) {
  int i,c,k;
  int doDefaultConfig = TRUE;
  int doDefaultProgram = TRUE;
  int printCCTable = FALSE;
  int doDefaultCC = TRUE;
  char * configOverride [NOF_CFG_OVERS];
  size_t configOverEnd = 0;
  unsigned int randomPreset[9];
  unsigned int defaultPreset[9] = {8,8,8, 0,0,0,0, 0,0};
  unsigned int * presetSelect = defaultPreset;
  char *midnam = NULL;

  char * alternateProgrammeFile = NULL;
  char * alternateConfigFile = NULL;

  memset(&inst, 0, sizeof(b_instance));

  srand ((unsigned int) time (NULL));

  for (i=0;i<AUDIO_CHANNELS; i++)
    jack_port[i] = NULL;
  jack_ports = strdup("system:playback_");

  const char *optstring = "c:CdDhHM:p:PrV";
  struct option long_options[] = {
    { "help",       no_argument,       0, 'H' },
    { "program",    required_argument, 0, 'p' },
    { "config",     required_argument, 0, 'c' },
    { "noconfig",   no_argument,       0, 'C' },
    { "dumpcc",     no_argument,       0, 'd' },
    { "noCC",       no_argument,       0, 'D' },
    { "midnam",     required_argument, 0, 'M' },
    { "noprogram",  no_argument,       0, 'P' },
    { "randomize",  no_argument,       0, 'r' },
    { "version",    no_argument,       0, 'V' },
    { 0, 0, 0, 0 }
  };

  while ((c = getopt_long(argc, argv, optstring, long_options, NULL)) != -1) {
    switch (c) {
      case 'c':
	alternateConfigFile = optarg;
	break;
      case 'C':
	doDefaultConfig = FALSE;
	break;
      case 'd':
	printCCTable = TRUE;
	break;
      case 'D':
	doDefaultCC = FALSE;
	break;
      case 'h':
	Usage(0);
	return (0);
	break;
      case 'H':
	Usage(1);
	return (0);
	break;
      case 'M':
	midnam = optarg;
	break;
      case 'r':
	for (k = 0; k < 9; k++)
	  randomPreset[k] = rand () % 9;
	fprintf (stderr,
	    "Random Preset: "
	     "%d%d %d%d%d%d %d%d%d\n",
	     randomPreset[0],
	     randomPreset[1],
	     randomPreset[2],
	     randomPreset[3],
	     randomPreset[4],
	     randomPreset[5],
	     randomPreset[6],
	     randomPreset[7],
	     randomPreset[8]);
	presetSelect = randomPreset;
	break;
      case 'p':
	alternateProgrammeFile = optarg;
	break;
      case 'P':
	doDefaultProgram = FALSE;
	break;
      case 'V':
	PrintVersion();
	return(0);
      default:
	fprintf(stderr, "invalid argument.\n");
	Usage(0);
	return(1);
    }
  }

  for (i = optind; i < argc; i++) {
    char * av = argv[i];
    if (strchr (av, '=') != NULL) {
      /* Remember this as a config parameter */
      if (configOverEnd < NOF_CFG_OVERS) {
	configOverride[configOverEnd++] = av;
      } else {
	fprintf (stderr,
	    "Too many configuration parameters (%d), please consider using a\n"
	    "configuration file instead of the commandline.\n",
	    NOF_CFG_OVERS);
	return(1);
      }
    }
  }

  /*
   * allocate data structures, instances.
   */
  allocAll();

  /*
   * evaluate configuration
   */

  if (getenv("XDG_CONFIG_HOME")) {
    size_t hl = strlen(getenv("XDG_CONFIG_HOME"));
    defaultConfigFile=(char*) malloc(hl+32);
    defaultProgrammeFile=(char*) malloc(hl+32);
    sprintf(defaultConfigFile, "%s/setBfree/default.cfg", getenv("XDG_CONFIG_HOME"));
    sprintf(defaultProgrammeFile, "%s/setBfree/default.pgm", getenv("XDG_CONFIG_HOME"));
  } else if (getenv("HOME")) {
    size_t hl = strlen(getenv("HOME"));
    defaultConfigFile=(char*) malloc(hl+30);
    defaultProgrammeFile=(char*) malloc(hl+30);
    sprintf(defaultConfigFile, "%s/.config/setBfree/default.cfg", getenv("HOME"));
    sprintf(defaultProgrammeFile, "%s/.config/setBfree/default.pgm", getenv("HOME"));
  }

  /*
   * Here we call modules that need to execute code in order to arrange
   * static initializations that is not practical to achieve in source code.
   */

  initControllerTable (inst.midicfg);
  if (doDefaultCC) {
    midiPrimeControllerMapping (inst.midicfg);
  }

  /*
   * Commandline arguments are parsed. If we are of a mind to try the
   * default configuration file we do that now.
   */

  if (doDefaultConfig == TRUE && defaultConfigFile) {
    if (access (defaultConfigFile, R_OK) == 0) {
      fprintf(stderr, "loading cfg: %s\n", defaultConfigFile);
      parseConfigurationFile (&inst, defaultConfigFile);
    }
  }

  if (alternateConfigFile) {
    if (access (alternateConfigFile, R_OK) == 0) {
      fprintf(stderr, "loading cfg: %s\n", alternateConfigFile);
      parseConfigurationFile (&inst, alternateConfigFile);
    }
  }

  /*
   * Then apply any configuration parameters collected from the commandline.
   * These must be applied last so that they can override the parameters
   * read from the files (if any).
   */

  for (i = 0; i < configOverEnd; i++) {
    parseConfigurationLine (&inst, "commandline argument",
			    0,
			    configOverride[i]);
  }

  /*
   * Having configured the initialization phase we can now actually do it.
   */

  if (mlockall (MCL_CURRENT | MCL_FUTURE)) {
    fprintf(stderr, "Warning: Can not lock memory.\n");
  }

  initAll ();

  /*
   * We are initialized and now load the programme file.
   */

  if (doDefaultProgram == TRUE && defaultProgrammeFile) {
    if (access (defaultProgrammeFile, R_OK) == 0)
      loadProgrammeFile (inst.progs, defaultProgrammeFile);
  } else {
    walkProgrammes(inst.progs, 1); // clear built-in default program
  }
  if (alternateProgrammeFile != NULL)
    loadProgrammeFile (inst.progs, alternateProgrammeFile);

  if (walkProgrammes(inst.progs, 0)) {
    listProgrammes (inst.progs, stderr);
  }

  initMidiTables(inst.midicfg);

  if (printCCTable) {
    listCCAssignments(inst.midicfg, stderr);
  }

  if (midnam) {
    FILE *fp = fopen(midnam, "w");
    if (fp) {
      save_midname(&inst, fp);
      fclose(fp);
    } else {
      fprintf(stderr, "failed to write midnam to '%s'\n", midnam);
    }
  }

  /*
   * With the programmes eager and ready to go, we spawn off the MIDI
   * listener thread. The thread will initialize the MIDI device.
   */
#ifdef HAVE_ASEQ
  pthread_t t_midi;
  if (!use_jack_midi) {
    if (!aseq_open(midi_port))
      k= pthread_create(&t_midi, NULL, aseq_run, &inst);

    if (k != 0) {
      fprintf (stderr, "%d : %s\n", k, "pthread_create : MIDIInReader thread");
      return (1);
    }
  }
#endif

  setMIDINoteShift (inst.midicfg, 0);

  setDrawBars (&inst, 0, presetSelect);
#if 0 // initial values are assigned in tonegen.c initToneGenerator()
  setDrawBars (&inst, 1, presetSelect); /* 838 000 000 */
  setDrawBars (&inst, 2, presetSelect); /* 86 - */
#endif

#ifndef _WIN32
  signal (SIGHUP, catchsig);
  signal (SIGINT, catchsig);
#endif

  connect_jack_ports();

  synth_ready = 1;

  fprintf(stderr,"All systems go. press CTRL-C, or send SIGINT or SIGHUP to terminate\n");

  while (j_client)
    sleep (1); /* jack callback is doing this the work now */

  /* shutdown and cleanup */
#ifdef HAVE_ASEQ
  if (!use_jack_midi) {
    aseq_stop=1;
    pthread_join(t_midi, NULL);
    aseq_close();
  }
#endif

  free(defaultConfigFile);
  free(defaultProgrammeFile);

  free(j_output_bufferptrs);
  free(j_output_port);
  free(midi_port);
  free(jack_ports);
  for (i=0;i<AUDIO_CHANNELS; i++)
    free(jack_port[i]);

  freeAll();
  munlockall();

  fprintf(stderr, "bye\n");
  return 0;
}

/* vi:set ts=8 sts=2 sw=2: */
