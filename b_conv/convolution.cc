/* setBfree - DSP tonewheel organ
 *
 * Copyright (C) 2012,2018 Robin Gareus <robin@gareus.org>
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

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "convolution.h"
#include <sndfile.h>
#include <zita-convolver.h>

#if ZITA_CONVOLVER_MAJOR_VERSION != 3
#error "This programs requires zita-convolver 3.x.x"
#endif

#ifndef DFLT_IR_FILE
#define DFLT_IR_FILE IRPATH "/ir_leslie-%04d.wav"
#endif

#define DFLT_IR_STRING DFLT_IR_FILE

//#define PRINT_WARNINGS // not RT-safe
#define AUDIO_CHANNELS 2 // see src/main.c

extern int SampleRateI;

static char*        ir_fn                    = NULL;
static unsigned int ir_chan[AUDIO_CHANNELS]  = { 1, 2 };
static unsigned int ir_delay[AUDIO_CHANNELS] = { 0, 0 };
static float        ir_gain[AUDIO_CHANNELS]  = { 0.5, 0.5 };

static int sched_priority, sched_policy;

static float wet = 0.0; /* Note, values <0 disable the effect completely and irreversibly */
static float dry = 1.0;

/** read an audio-file completely into memory
 * allocated memory needs to be free()ed by caller
 */
int
audiofile_read (const char* fn, float** buf, unsigned int* n_ch, unsigned int* n_sp)
{
	SF_INFO  nfo;
	SNDFILE* sndfile;
	int      ok = -2;

	memset (&nfo, 0, sizeof (SF_INFO));

	if ((sndfile = sf_open (fn, SFM_READ, &nfo)) == 0)
		return -1;
	if (SampleRateI != nfo.samplerate) {
		fprintf (stderr, "\nb3_conv : samplerate mismatch file:%d synth:%d\n", nfo.samplerate, SampleRateI);
	}

	if (n_ch)
		*n_ch = (unsigned int)nfo.channels;
	if (n_sp)
		*n_sp = (unsigned int)nfo.frames;

	if (buf) {
		const size_t frames = nfo.channels * nfo.frames;
		*buf                = (float*)malloc (frames * sizeof (float));
		if (*buf) {
			sf_count_t rd;
			if (nfo.frames == (rd = sf_readf_float (sndfile, *buf, nfo.frames))) {
				ok = 0;
			} else {
				fprintf (stderr, "IR short read :%ld of %ld\n", (long int)rd, (long int)nfo.frames);
			}
		} else {
			fprintf (stderr, "FATAL: memory allocation failed for IR audio-file buffer.\n");
		}
	}
	sf_close (sndfile);
	return (ok);
}

/*
 * @param g  0.0 Dry ... 1.0 wet
 */
void
fsetConvolutionMix (float g)
{
	if (wet < 0)
		return; /* conv is disabled */
	if (g < 0 || g > 1)
		return;
	wet = g;
	dry = 1.0 - wet;
}

void
setConvolutionMix (void* d, unsigned char u)
{
	fsetConvolutionMix (u / 127.0);
}

int
convolutionConfig (ConfigContext* cfg)
{
	double d;
	int    n;
	if (strcasecmp (cfg->name, "convolution.ir.file") == 0) {
		free (ir_fn);
		ir_fn = strdup (cfg->value);
	} else if (!strncasecmp (cfg->name, "convolution.ir.channel.", 23)) {
		if (sscanf (cfg->name, "convolution.ir.channel.%d", &n) == 1) {
			if ((0 < n) && (n <= AUDIO_CHANNELS))
				ir_chan[n - 1] = atoi (cfg->value);
		}
	} else if (!strncasecmp (cfg->name, "convolution.ir.gain.", 20)) {
		if (sscanf (cfg->name, "convolution.ir.gain.%d", &n) == 1) {
			if ((0 < n) && (n <= AUDIO_CHANNELS))
				ir_gain[n - 1] = atof (cfg->value);
		}
	} else if (!strncasecmp (cfg->name, "convolution.ir.delay.", 21)) {
		if (sscanf (cfg->name, "convolution.ir.delay.%d", &n) == 1) {
			if ((0 < n) && (n <= AUDIO_CHANNELS))
				ir_delay[n - 1] = atoi (cfg->value);
		}
	} else if (getConfigParameter_d ("convolution.mix", cfg, &d) == 1) {
		fsetConvolutionMix (d);
	} else {
		return 0;
	}
	return 1; // OK
}

static const ConfigDoc doc[] = {
	{ "convolution.mix", CFG_DOUBLE, "0.0", "Note: modifies dry/wet. [0..1]", INCOMPLETE_DOC },
	{ "convolution.ir.file", CFG_TEXT, ("\"" DFLT_IR_FILE "\""), "convolution sample filename", INCOMPLETE_DOC },
	{ "convolution.ir.channel.<int>", CFG_INT, "-", "<int> 1:Left, 2:Right; value: channel-number in IR file to use, default: 1->1, 2->2", INCOMPLETE_DOC },
	{ "convolution.ir.gain.<int>", CFG_DOUBLE, "0.5", "gain-factor to apply to IR data on load. <int> 1:left-channel, 2:right-channel.", INCOMPLETE_DOC },
	{ "convolution.ir.delay.<int>", CFG_INT, "0", "delay IR in audio-samples.", INCOMPLETE_DOC },
	DOC_SENTINEL
};

const ConfigDoc*
convolutionDoc ()
{
	return doc;
}

static Convproc* convproc = 0;

void
initConvolution (
    void* clv, void* m,
    const unsigned int channels,
    const unsigned int buffersize,
    int                sched_pri,
    int                sched_pol)
{
	unsigned int       i, c;
	const float        dens    = 0;
	const unsigned int size    = 204800;
	const unsigned int options = 0;

	if (zita_convolver_major_version () != ZITA_CONVOLVER_MAJOR_VERSION) {
		fprintf (stderr, "\nZita-convolver version does not match.\n");
		exit (1);
	}

	convproc = new Convproc;
	convproc->set_options (options);
#if ZITA_CONVOLVER_MAJOR_VERSION == 3
	convproc->set_density (dens);
#endif

	if (convproc->configure (
	        /*in*/ channels,
	        /*out*/ channels,
	        size,
	        /*fragm*/ buffersize,
	        /*min-part*/ buffersize,
	        /*max-part*/ buffersize /*Convproc::MAXPART*/
#if ZITA_CONVOLVER_MAJOR_VERSION == 4
					, dens /*density*/
#endif
	   )) {
		fprintf (stderr, "\nConvolution: Cannot initialize convolution engine.\n");
		exit (1);
	}

	if (!ir_fn) {
		char* irf = getenv ("BXIRFILE");
		if (irf && strstr (irf, "%04d")) {
			const size_t len = strlen (irf) + 6;
			ir_fn            = (char*)malloc (len);
			snprintf (ir_fn, len, irf, SampleRateI);
			ir_fn[len - 1] = '\0';
		} else if (irf && strlen (irf) > 0) {
			ir_fn = strdup (getenv ("BXIRFILE"));
		}
	}

	if (!ir_fn) {
		const size_t len = strlen (DFLT_IR_STRING) + 6;
		ir_fn            = (char*)malloc (len);
		snprintf (ir_fn, len, DFLT_IR_STRING, SampleRateI);
		ir_fn[len - 1] = '\0';
	}

	if (access (ir_fn, R_OK) != 0) {
		fprintf (stderr, "\nConvolution: cannot stat IR: %s\n", ir_fn);
		wet = -1; /* disable */
		return;
	}

	unsigned int nchan = 0;
	unsigned int nfram = 0;
	float*       p     = NULL;

	if (audiofile_read (ir_fn, &p, &nchan, &nfram)) {
		fprintf (stderr, "\nConvolution: failed to read IR \n");
		exit (1);
	}
	free (ir_fn);
	ir_fn = NULL;

	float* gb = (float*)malloc (nfram * sizeof (float));
	if (!gb) {
		fprintf (stderr, "FATAL: memory allocation failed for convolution buffer.\n");
		exit (1);
	}

	for (c = 0; c < AUDIO_CHANNELS; c++) {
		if (ir_chan[c] > nchan || ir_chan[c] < 1) {
			fprintf (stderr, "\nConvolution: invalid channel in IR file. required: 1 <= %d <= %d\n", ir_chan[c], nchan);
			exit (1);
		}
		if (ir_delay[c] < 0) {
			fprintf (stderr, "\nConvolution: invalid delay. required: 0 <= %d\n", ir_delay[c]);
			exit (1);
		}
		for (i = 0; i < nfram; ++i) {
			gb[i] = p[i * nchan + ir_chan[c] - 1] * ir_gain[c];
		}
		convproc->impdata_create (c, c, 1, gb, ir_delay[c], ir_delay[c] + nfram);
	}

	free (gb);
	free (p);

#if 1 /* INFO */
	fprintf (stderr, "\n");
	convproc->print (stderr);
#endif

	sched_priority = sched_pri;
	sched_policy   = sched_pol;

	if (convproc->start_process (sched_priority, sched_policy)) {
		fprintf (stderr, "\nConvolution: Cannot start processing.\n");
		exit (1);
	}

	useMIDIControlFunction (m, "convolution.mix", setConvolutionMix, NULL);
}

void
freeConvolution ()
{
	convproc->stop_process ();
	delete (convproc);
}

void
copy_input_to_output (const float** inbuf, float** outbuf, size_t n_channels, size_t n_samples)
{
	unsigned int c;
	for (c = 0; c < n_channels; ++c)
		memcpy (outbuf[c], inbuf[c], n_samples * sizeof (float));
}

void
convolve (const float** inbuf, float** outbuf, size_t n_channels, size_t n_samples)
{
	unsigned int i, c;

	if (convproc->state () == Convproc::ST_WAIT)
		convproc->check_stop ();

	if (n_channels > AUDIO_CHANNELS) {
#ifdef PRINT_WARNINGS
		static int bpn = 1;
		if (bpn > 0) {
			bpn--;
			fprintf (stderr, "Convolution: requested too many channels.\n");
		}
#endif
		wet = -1; /* disable irreversibly */
	}

	if (wet <= 0) {
		copy_input_to_output (inbuf, outbuf, n_channels, n_samples);
		return;
	}

	if (convproc->state () != Convproc::ST_PROC) {
#ifdef PRINT_WARNINGS
		static int bpn = 5;
		if (bpn > 0) {
			bpn--;
			fprintf (stderr, "Convolution: failed - state != processing .\n");
		}
#endif
		copy_input_to_output (inbuf, outbuf, n_channels, n_samples);
		return;
	}

	for (c = 0; c < n_channels; ++c)
#if 0
		memcpy (convproc->inpdata (c), inbuf[c], n_samples * sizeof (float));
#else /* prevent denormals */
	{
		float* id = convproc->inpdata (c);
		for (i = 0; i < n_samples; ++i) {
			id[i] = inbuf[c][i] + DENORMAL_HACK;
		}
	}
#endif

		int f = convproc->process (false);

	if (f /*&Convproc::FL_LOAD)*/) {
#ifdef PRINT_WARNINGS
		/* Convproc::FL_LATE = 0x0000FFFF,
		 * Convproc::FL_LOAD = 0x01000000
		 */
		static int bpn = 20;
		if (bpn > 0) {
			bpn--;
			fprintf (stderr, "Convolution: failed (flags: %08x).\n", f);
		}
#endif
		copy_input_to_output (inbuf, outbuf, n_channels, n_samples);
		return;
	}

	for (c = 0; c < n_channels; ++c)
		memcpy (outbuf[c], convproc->outdata (c), n_samples * sizeof (float));

#if 1 /* dry/wet fade */
	if (wet != 1.0) {
		for (c = 0; c < n_channels; c++)
			for (i = 0; i < n_samples; ++i) {
				outbuf[c][i] = ((wet * outbuf[c][i]) + (dry * inbuf[c][i]));
			}
	}
#endif
}
