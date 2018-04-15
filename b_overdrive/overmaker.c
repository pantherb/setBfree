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

/* overmaker.c
 *
 * 21-jul-2004/FK Swell pedal function moved to tonegen.c.
 *
 * This program (together with the ovt_*.[ch] sources) creates the overdrive
 * module by writing the file overdrive.c (on standard output). That code
 * is then compiled and linked with the organ binary. This process is
 * automated by the Makefile.
 *
 * The reason for doing it like this (having one program write another) is
 * that it makes it possible to optimise certain elements (in particular,
 * the interpolation and decimation filters) and still have the freedom to
 * experiment with them without lengthy rewrites. The downside is of course
 * that a lot of straight-forward code is pressed into the unnatural shape
 * required by the overmaker framework. The result is far from beautiful,
 * but there are remedies for that which may be applied some day.
 *
 *
 * 19-May-2004/FK The creation of the config interface is a bit complicated,
 * but here how it works:
 *
 * This program is only provides a framework, it is only complete together with
 * an external module that creates the actual overdrive algorithm. That
 * external module must supply a number of different pieces of code: static
 * variables, configuration, initialization, and other snippets of code to be
 * inserted into the final source. Many of these snippets are found by
 * ifdef-fing in this source and calling an external function to insert code
 * in the appropriate place. Unfortunately that has become unwieldy and not
 * general, so starting with the configuration function, I have begun another
 * approach.
 *
 * In the new approach, the startup code in this program calls the function
 * void bindCallbacks() in the external module. The external module then calls
 * this code back on the function
 *
 * void bindCallBack (int slot, void (*func)(void *), void * handback)
 *
 * and registers a local function and an argument to it (the handback).
 * The registration key is the 'slot', an integer that identifies the kind
 * of code that is expected of the external module at that point in the
 * output. The handback pointer is also registered and will be given back
 * to the external module as the argument to the callback function. This
 * enables the external module to keep track of the call in any way it
 * wants.
 *
 * So currently, there is only a single slot, the symbol MOD_CONFIG (defined
 * in overmaker.h), which should be code that can pickup values from the
 * configuration system.
 * October 2003 /FK
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "filterTools.h"
#include "overmaker.h"
#include "overmakerdefs.h" /* Parameter definitions */

#ifdef TR_BIASED
#include "ovt_biased.h"
#endif /* TR_BIASED */

#define MAX(A, B) (((A) < (B)) ? (B) : (A))

#if defined(PRE_FILTER_TYPE) || defined(POST_FILTER_TYPE)
#include "../b3_whirl/eqcomp.h"
#endif
/*
 * Array of functions that the external module uses to tie in its
 * own code into the general output framework.
 */

static modCall modvec[MODVECSZ];

/* The symbol LOCAL_INCLUDES expands to
 * {"filename", "filename", ..., NULL}
 */

static char* localIncludes[] = LOCAL_INCLUDES;

/* The symbol SYSTEM_INCLUDES expands to
 * {"filename", "filename", ..., NULL}
 */

static char* systemIncludes[] = SYSTEM_INCLUDES;

/* IPOL_FIR expands to the interpolation filter weights. */

#ifdef IPOL_FC
static float ipwdef[IPOL_LEN];
#else
static float ipwdef[IPOL_LEN]   = IPOL_FIR;
#endif /* IPOL_FC */

/* AAL_FIR and AAL_LEN expands to anti-aliasing filter weights. */

#ifdef AAL_FC
static float aaldef[AAL_LEN];
#else
static float aaldef[AAL_LEN]    = AAL_FIR;
#endif /* AAL_FC */

/* XOVER_RATE expands a small integer: 1, 2, 3 ... which is the
 * oversampling rate */

static int R = XOVER_RATE;

#define DFQ (IPOL_LEN / XOVER_RATE)

/* XZB_SIZE expands to the nof samples in the input history buffer */

/* YZB_SIZE expands to the nof samples in the transfer-function output
 * history buffer. */

/* Interpolation filter descriptors for compilation */

typedef struct _ipoldesc {
	int terms;            /* The number of terms in this sample */
	int weightIndex[DFQ]; /* The weights */
	int xzIndex[DFQ];     /* The input sample history index */
} IpolDesc;

static IpolDesc ipold[XOVER_RATE];

static int wpw = 63;
static int wpp = 60;

static FILE* outstr     = NULL;
static int   indent     = 0;
static int   indentDone = 0;

#ifdef PRE_FILTER_TYPE
static int generatePreFilter = 1;
#else
static int   generatePreFilter  = 0;
#endif /* PRE_FILTER_TYPE */

#ifdef POST_FILTER_TYPE
static int generatePostFilter = 1;
#else
static int   generatePostFilter = 0;
#endif /* POST_FILTER_TYPE */

/* ***************************************************************************/

void
installFloats (double d[], float f[], size_t len)
{
	int i;
	for (i = 0; i < len; i++) {
		f[i] = (float)d[i];
	}
}

void
computeIpolFilter (double fc, int wdw)
{
	double D[IPOL_LEN];

	assert (IPOL_LEN % 2); /* The nof points must be odd */

	sincApply (fc, wdw, D, IPOL_LEN);
	installFloats (D, ipwdef, IPOL_LEN);
}

void
computeAalFilter (double fc, int wdw)
{
	double D[AAL_LEN];

	assert (AAL_LEN % 2); /* The nof points must be odd */

	sincApply (fc, wdw, D, AAL_LEN);
	installFloats (D, aaldef, AAL_LEN);
}

void
computeIpolWeights ()
{
	int row;
	/* For each overclocked sample there is one row of FIR weights. */
	for (row = 0; row < XOVER_RATE; row++) {
		int wix;
		int i = 0;
		/* For each row, only remember the expressions where the FIR
       weight index is non-negative and within the definition. */
		for (wix = -row; wix < IPOL_LEN; wix += XOVER_RATE) {
			if (0 <= wix) {
				ipold[row].weightIndex[i] = wix;
				ipold[row].xzIndex[i]     = -i;
				ipold[row].terms          = ++i;
			}
		}
	}
}

void
setOutputStream (FILE* fp)
{
	outstr = fp;
}

void
code (char* s)
{
	int i;

	if (outstr == NULL) {
		outstr = stdout;
	}

	if (!indentDone) {
		for (i = 0; i < indent; i++) {
			fprintf (outstr, "%s", " ");
		}
		indentDone = 1;
	}

	fprintf (outstr, "%s", s);
}

void
codeln (char* s)
{
	code (s);
	code ("\n");
	indentDone = 0;
}

void
vspace (int lines)
{
	int i;
	for (i = 0; i < lines; i++) {
		codeln ("");
	}
}

void
comment (char* s)
{
	code ("/* ");
	code (s);
	code (" */");
}

void
commentln (char* s)
{
	comment (s);
	codeln ("");
}

void
pushIndent ()
{
	indent += 2;
}

void
popIndent ()
{
	indent -= 2;
}

void
includeSystem (char* headerFile)
{
	code ("#include <");
	code (headerFile);
	codeln (">");
}

void
includeLocal (char* headerFile)
{
	code ("#include \"");
	code (headerFile);
	codeln ("\"");
}

void
moduleHeader ()
{
	int  i;
	char buf[BUFSZ];

	/* Includes */
	includeSystem ("stdio.h");
	includeSystem ("stdlib.h");
	includeSystem ("string.h");
	includeSystem ("math.h");

	for (i = 0; systemIncludes[i] != NULL; i++) {
		includeSystem (systemIncludes[i]);
	}

	for (i = 0; localIncludes[i] != NULL; i++) {
		includeLocal (localIncludes[i]);
	}
#if defined(PRE_FILTER_TYPE) || defined(POST_FILTER_TYPE)
	includeLocal ("../b3_whirl/eqcomp.h");
#endif

	/* -- all constants -- */

	vspace (2);

	/* Decimation filter for downsampling : definition */

	commentln ("Decimation filter definition");
	sprintf (buf, "static const float aaldef[%d] = {", AAL_LEN);
	codeln (buf);
	pushIndent ();
	for (i = 0; i < (AAL_LEN - 1); i++) {
		sprintf (buf, "%*.*f,", wpw, wpp, aaldef[i]);
		codeln (buf);
	}
	sprintf (buf, "%*.*f", wpw, wpp, aaldef[i]);
	codeln (buf);
	popIndent ();
	codeln ("};");

	vspace (1);

	/* The number of weights applied from each implementation filter
   * varies between rows. The wiLen vector gives the number. */

	vspace (1);

	commentln ("Weight count for wi[][] above.");
	sprintf (buf, "static const int wiLen[%d] = {", R);
	codeln (buf);
	pushIndent ();
	for (i = 0; i < (R - 1); i++) {
		sprintf (buf, "%d,", ipold[i].terms);
		codeln (buf);
	}
	sprintf (buf, "%d", ipold[i].terms);
	codeln (buf);
	popIndent ();
	codeln ("};");

	vspace (1);

	/* The interpolation filter definition */

	commentln ("Interpolation filter definition");

	sprintf (buf, "static const float ipwdef[%d] = {", IPOL_LEN);
	codeln (buf);

	pushIndent ();

	for (i = 0; i < (IPOL_LEN - 1); i++) {
		sprintf (buf, "% *.*f,", wpw, wpp, ipwdef[i]);
		codeln (buf);
	}
	sprintf (buf, "% *.*f", wpw, wpp, ipwdef[i]);
	codeln (buf);

	popIndent ();

	codeln ("};");

	vspace (1);

/* -- end constants -- */
#if 0
  commentln (" *** BEGIN STRUCT ***");

  /* Input history buffer */
  vspace (1);

  commentln ("Input history buffer");
  sprintf (buf, "static float xzb[%d];", XZB_SIZE);
  codeln (buf);

  vspace (1);

  commentln ("Input history writer");
  codeln ("static float * xzp = &(xzb[0]);");

  vspace (1);

  commentln ("Input history end sentinel");
  sprintf (buf, "static float * xzpe = &(xzb[%d]);", XZB_SIZE);
  codeln (buf);

  /* If the xzp pointer is far enough from the start of the input history
   * we can index it by negatives. Far enough depends on how many weights
   * we need to use. The longest we need to go is this: if the filter has
   * M points, and R is our oversampling rate (1, 2, 3, ... etc), then
   * the integer result of M/R is the farthest input sample. For example,
   * assume M = 33 and R = 8, then M div R = 4, ie xz[-4], which means
   * that if xzp < xz[4] we need wrap treatment.  */

  vspace (1);

  commentln ("Negative index access wrap sentinel");
  sprintf (buf, "static float * xzwp = &(xzb[%d]);", DFQ+1);
  codeln (buf);

  vspace (1);

  /* The transfer-function output history buffer */

  commentln ("Transfer-function output history buffer");
  sprintf (buf, "static float yzb[%d];", YZB_SIZE);
  codeln (buf);

  vspace (1);

  /* Transfer-function output writer */

  commentln ("Transfer-function output writer");
  codeln ("static float * yzp = &(yzb[0]);");

  vspace (1);

  /* Transfer-function output history end sentinel */

  commentln ("Transfer-function output history end sentinel");
  sprintf (buf, "static float * yzpe = &(yzb[%d]);", YZB_SIZE);
  codeln (buf);

  vspace (1);

  /* Transfer-function output history wrap sentinel */

  commentln ("Transfer-function output history wrap sentinel");
  sprintf (buf, "static float * yzwp = &(yzb[%d]);", AAL_LEN);
  codeln (buf);

  vspace (1);

  commentln ("Zero-filled filter of interpolation length");
  sprintf (buf, "float ipolZeros[%d];", IPOL_LEN);
  codeln (buf);

  /* The pre-emphasis filter definition */
  vspace (1);

#ifdef COMMENT
  commentln ("Pre-emphasis filter definition");

  sprintf (buf, "static float predef[%d] = {", IPOL_LEN);
  codeln (buf);

  pushIndent ();

  for (i = 0; i < (IPOL_LEN - 1); i++) {
    sprintf (buf, "%-*.*f,", wpw, wpp, predef[i]);
    codeln (buf);
  }
  sprintf (buf, "%-*.*f", wpw, wpp, predef[i]);
  codeln (buf);

  popIndent ();

  codeln ("};");
#endif /* COMMENT */

  /* We need R implementation filters, one for each oversampled sample.
   * They are not initialized here, but mixed with the pre-emphasis
   * filter at run-time by function preFilterCompile().
   */

  vspace (1);

  commentln ("Sample-specific runtime interpolation FIRs");
  sprintf (buf, "static float wi[%d][%d];", R, DFQ+1);
  codeln (buf);

  /* Decimation filter for downsampling : runtime */

  commentln ("Decimation filter runtime");
  sprintf (buf, "static float aal[%d];", AAL_LEN);
  codeln (buf);

  vspace (1);

  /* Decimation filter end sentinel */

  commentln ("Decimation filter end sentinel");
  sprintf (buf, "static float * aalEnd = &(aal[%d]);", AAL_LEN);
  codeln (buf);

  vspace (1);

  /* De-emphasis filter definition */
  /* Maybe we should settle for a size requirement here ... */
  /* And have the same for the emphasis */

  sprintf (buf, "size_t ipolFilterLength = %d;", IPOL_LEN);
  codeln (buf);
  sprintf (buf, "size_t aalFilterLength = %d;", AAL_LEN);
  codeln (buf);

  vspace (1);

  commentln ("Zero-filled filter of anti-aliasing length");
  sprintf (buf, "float aalZeros[%d];", AAL_LEN);
  codeln (buf);

  vspace (1);

  /* Clean/overdrive switch */

  commentln ("Clean/overdrive switch");
  codeln ("static int isClean = 1;");


  /* Static variables for the pre-emphasis filter */
  if (generatePreFilter) {
    vspace (1);
    commentln ("Static variables for the pre-emphasis filter");
    commentln ("IIR filter weights");
    codeln ("static float pr_a1;");
    codeln ("static float pr_a2;");
    codeln ("static float pr_b0;");
    codeln ("static float pr_b1;");
    codeln ("static float pr_b2;");
    codeln ("static float pr_z1;");
    codeln ("static float pr_z2;");
    commentln ("Pre-filter parameters");
    sprintf (buf, "static double pr_F = %g;", PRE_FILTER_HERTZ);
    codeln (buf);
    sprintf (buf, "static double pr_Q = %g;", PRE_FILTER_Q);
    codeln (buf);
    sprintf (buf, "static double pr_G = %g;", PRE_FILTER_G);
    codeln (buf);
  }

  if (generatePostFilter) {
    vspace (1);
    commentln ("Static variables for the post-filter");
    commentln ("IIR filter weights");
    codeln ("static float de_a1;");
    codeln ("static float de_a2;");
    codeln ("static float de_b0;");
    codeln ("static float de_b1;");
    codeln ("static float de_b2;");
    codeln ("static float de_z1;");
    codeln ("static float de_z2;");

    commentln ("Post-filter parameters");
    sprintf (buf, "static double de_F = %g;", POST_FILTER_HERTZ);
    codeln (buf);
    sprintf (buf, "static double de_Q = %g;", POST_FILTER_Q);
    codeln (buf);
    sprintf (buf, "static double de_G = %g;", POST_FILTER_G);
    codeln (buf);
  }

#ifdef COMMENT /* Replaced by hdr_inverted */
  vspace (1);

  commentln ("Static variables for the adaptive non-linear transfer curve");
  codeln ("static float Tx;");
  codeln ("static float K;");
  codeln ("static float Ylim;");
  codeln ("static float YlimInv;");
#endif         /* COMMENT */

#ifdef OUTPUT_GAIN
  vspace (1);
  commentln ("Output gain");
  sprintf (buf, "static float outputGain = %g;", OUTPUT_GAIN);
  codeln (buf);
#endif /* OUTPUT_GAIN */

#ifdef CLEAN_MIX
  vspace (1);
  commentln ("Clean mix");
  codeln ("static float mixClean = 0.0;");
  codeln ("static float mixFx = 1.0;");
#endif /* CLEAN_MIX */

#ifdef INPUT_GAIN
  vspace (1);
  commentln ("Input gain");
  sprintf (buf, "static float inputGain = %g;", INPUT_GAIN);
  codeln (buf);
#endif /* INPUT_GAIN */

#ifdef PRE_DC_OFFSET
  vspace (1);
  commentln ("Pre DC offset");
  codeln ("static float preDCOffset = 0.0;");
#endif /* PRE_DC_OFFSET */

#ifdef INPUT_COMPRESS
  vspace (1);
  commentln ("Input compressor gain");
  sprintf (buf, "static float ipcGain = %g;", IPC_GAIN_IDLE);
  codeln (buf);
  sprintf (buf, "static float ipcThreshold = %g;", IPC_THRESHOLD);
  codeln (buf);
  sprintf (buf, "static float ipcGainReduce = %g;", IPC_GAIN_REDUCE);
  codeln (buf);
  sprintf (buf, "static float ipcGainRecover = %g;", IPC_GAIN_RECOVER);
  codeln (buf);
#endif /* INPUT_COMPRESS */

#ifdef SAG_EMULATION
  vspace (3);
  codeln ("static float sagZ = 0.0;");
  sprintf (buf, "static float sagFb = %g;", SAG_FB);
  codeln (buf);
#endif /* SAG_EMULATION */
  vspace (2);

#endif

	vspace (2);
	codeln ("struct b_preamp {");
	pushIndent ();

	commentln ("Input history buffer");
	sprintf (buf, "float xzb[%d];", XZB_SIZE);
	codeln (buf);

	commentln ("Input history writer");
	codeln ("float * xzp;");
	commentln ("Input history end sentinel");
	codeln ("float * xzpe;");
	vspace (1);
	commentln ("Negative index access wrap sentinel");
	codeln ("float * xzwp;");

	commentln ("Transfer-function output history buffer");
	sprintf (buf, "float yzb[%d];", YZB_SIZE);
	codeln (buf);
	commentln ("Transfer-function output writer");
	codeln ("float * yzp;");
	commentln ("Transfer-function output history end sentinel");
	codeln ("float * yzpe;");
	commentln ("Transfer-function output history wrap sentinel");
	codeln ("float * yzwp;");

	commentln ("Zero-filled filter of interpolation length");
	sprintf (buf, "float ipolZeros[%d];", IPOL_LEN);
	codeln (buf);

	/* skipped */

	commentln ("Sample-specific runtime interpolation FIRs");
	sprintf (buf, "float wi[%d][%d];", R, DFQ + 1);
	codeln (buf);

	commentln ("Decimation filter runtime");
	sprintf (buf, "float aal[%d];", AAL_LEN);
	codeln (buf);

	commentln ("Decimation filter end sentinel");
	codeln ("float * aalEnd;");

	/* De-emphasis filter definition */
	/* Maybe we should settle for a size requirement here ... */
	/* And have the same for the emphasis */

	codeln ("size_t ipolFilterLength;");
	codeln ("size_t aalFilterLength;");

	commentln ("Zero-filled filter of anti-aliasing length");
	sprintf (buf, "float aalZeros[%d];", AAL_LEN);
	codeln (buf);

	commentln ("Clean/overdrive switch");
	codeln ("int isClean;");

/* skipped generatePreFilter */
/* skipped generatePostFilter */

#ifdef OUTPUT_GAIN
	codeln ("float outputGain;");
#endif /* OUTPUT_GAIN */

#ifdef CLEAN_MIX
	vspace (1);
	commentln ("Clean mix");
	codeln ("float mixClean;");
	codeln ("float mixFx;");
#endif /* CLEAN_MIX */

#ifdef INPUT_GAIN
	vspace (1);
	commentln ("Input gain");
	codeln ("float inputGain;");
#endif /* INPUT_GAIN */

#ifdef PRE_DC_OFFSET
	vspace (1);
	commentln ("Pre DC offset");
	codeln ("float preDCOffset = 0.0;");
#endif /* PRE_DC_OFFSET */

#ifdef INPUT_COMPRESS
	commentln ("Input compressor gain");
	codeln ("float ipcGain;");
	codeln ("float ipcThreshold;");
	codeln ("float ipcGainReduce;");
	codeln ("float ipcGainRecover;");
#endif /* INPUT_COMPRESS */

#ifdef SAG_EMULATION
	codeln ("float sagZ;");
	codeln ("float sagFb;");
#endif /* SAG_EMULATION */

#ifdef TR_BIASED
	hdr_biased ();
#endif /* TR_BIASED */

	popIndent ();
	codeln ("};");
	commentln (" *** END STRUCT ***");
	vspace (2);
}

/* This function outputs a routine which will mix and normalize
 * the interpolation filter and the pre-emphasis filter at runtime.
 * The routine then writes the result into the sample-specific
 * effective FIRs, used to calculate and emphasize the input signal.
 */
void
preIpolMixer ()
{
	int  row;
	int  column;
	char buf[BUFSZ];

	vspace (3);

	commentln ("Remember to call this first with the filter definitions");
	commentln ("ipolDef is the interpolation filter definition");
	commentln ("aalDef is the anti-aliasing filter definition");

	codeln ("static void mixFilterWeights (void *pa, const float * ipolDef, const float * aalDef) {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");
	codeln ("int i;");
	codeln ("float sum = 0.0;");
	vspace (1);
	sprintf (buf, "float mix[%d];", MAX (IPOL_LEN, AAL_LEN));
	codeln (buf);

	vspace (1);

	commentln ("Copy the interpolation filter weights");

	vspace (1);

	sprintf (buf, "for (i = 0; i < %d; i++) {", IPOL_LEN);
	codeln (buf);
	pushIndent ();
	codeln ("mix[i] = ipolDef[i];");
	codeln ("sum += fabs (mix[i]);");
	popIndent ();
	codeln ("}");

	vspace (1);

	commentln ("Normalize the copy");

	vspace (1);

	sprintf (buf, "for (i = 0; i < %d; i++) {", IPOL_LEN);
	codeln (buf);
	pushIndent ();
	codeln ("mix[i] /= sum;");
	popIndent ();
	codeln ("}");

	vspace (1);

	commentln ("Install in correct sequence in runtime array of weights");

	vspace (1);

	for (row = 0; row < XOVER_RATE; row++) {
		for (column = 0; column < ipold[row].terms; column++) {
			sprintf (buf,
			         "pp->wi[%d][%d] = mix[%d];",
			         row,
			         column,
			         ipold[row].weightIndex[column]);
			codeln (buf);
		}
	}

	vspace (1);

	commentln ("Copy the anti-aliasing filter definition");

	vspace (1);

	codeln ("sum = 0.0;");
	sprintf (buf, "for (i = 0; i < %d; i++) {", AAL_LEN);
	codeln (buf);
	pushIndent ();
	codeln ("mix[i] = aalDef[i];");
	codeln ("sum += fabs (mix[i]);");
	popIndent ();
	codeln ("}");

	vspace (1);

	commentln ("Normalize the weights to unit gain and install");

	vspace (1);

	sprintf (buf, "for (i = 0; i < %d; i++) {", AAL_LEN);
	codeln (buf);
	pushIndent ();
	codeln ("pp->aal[i] = mix[i] / sum;");
	popIndent ();
	codeln ("}");

	popIndent ();
	code ("}");
	code (" ");
	commentln ("preFilterCompile");
}

/*
 * Ejects the function declaration
 */
void
funcHeader (char* funcName)
{
	char buf[BUFSZ];

	vspace (3);

	sprintf (buf,
	         "float * %s (void *pa, const float * inBuf, float * outBuf, size_t buflen)",
	         funcName);
	codeln (buf);
	codeln ("{");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");
}

/*
 * Variable declaration code
 */
void
funcVarDef ()
{
	codeln ("const float * xp = inBuf;");
	codeln ("float * yp = outBuf;");
	codeln ("int i;");
	codeln ("size_t n;");
}

/*
 * The body of the processing function.
 */
void
funcBody (void (*transferdef) (char*, char*))
{
	char buf[BUFSZ];

	/* Iterate over the input buffer */
	vspace (1);
	codeln ("for (n = 0; n < buflen; n++) {");
	pushIndent ();
	codeln ("float xin;");
	codeln ("float u = 0.0;");
	codeln ("float v;");
	codeln ("float y = 0.0;");
#ifdef BASS_SIDECHAIN
	codeln ("float bb;");
	codeln ("float bb_1;");
#endif /* BASS_SIDECHAIN */

	if (generatePreFilter || generatePostFilter) {
		codeln ("float Z0;");
	}

	/* Put the next input sample in the input history */
	vspace (1);
	commentln ("Place the next input sample in the input history.");
	codeln ("if (++(pp->xzp) == pp->xzpe) {");
	pushIndent ();
	codeln ("pp->xzp = pp->xzb;");
	popIndent ();
	codeln ("}");

	vspace (1);

#ifdef INPUT_GAIN
	codeln ("xin = pp->inputGain * (*xp++);");
#else
	codeln ("xin = *xp++;");
#endif /* INPUT_GAIN */

#ifdef SAG_EMULATION
	codeln ("pp->sagZ = (pp->sagFb * pp->sagZ) + fabsf(xin);");
	codeln ("pp->bias = pp->biasBase - (pp->sagZgb * pp->sagZ);");
	codeln ("pp->norm = 1.0 - (1.0 / (1.0 + (pp->bias * pp->bias)));");
#endif /* SAG_EMULATION */

#ifdef INPUT_COMPRESS
	codeln ("xin *= pp->ipcGain;");
	codeln ("if ((xin < - pp->ipcThreshold) || (pp->ipcThreshold < xin)) {");
	pushIndent ();
	codeln ("pp->ipcGain *= pp->ipcGainReduce;");
	popIndent ();
	sprintf (buf, "} else if (pp->ipcGain < %g) {", IPC_GAIN_IDLE);
	codeln (buf);
	pushIndent ();
	popIndent ();
	codeln ("pp->ipcGain *= pp->ipcGainRecover;");
	codeln ("}");
#endif /* INPUT_COMPRESS */

	if (generatePreFilter) {
		codeln ("Z0 = xin - (pr_a1 * pr_z1) - (pr_a2 * pr_z2);");
		codeln ("*(pp->xzp) = (Z0 * pp->pr_b0) + (pp->pr_b1 * pp->pr_z1) + (pp->pr_b2 * pp->pr_z2);");
		codeln ("pp->pr_z2 = pp->pr_z1;");
		codeln ("pp->pr_z1 = Z0;");
	} else {
		codeln ("*(pp->xzp) = xin;");
	}

	vspace (1);

#ifdef BASS_SIDECHAIN
	codeln ("bb = *(pp->xzp);");
#endif /* BASS_SIDECHAIN */

	/* Select on wrapping code or not */

	commentln ("Check the input history wrap sentinel");
	codeln ("if (pp->xzwp <= pp->xzp) {");
	pushIndent ();

	/* Loop over oversampled samples */

	sprintf (buf, "for (i = 0; i < %d; i++) {", R); /* For IPOL */
	codeln (buf);
	pushIndent ();

	vspace (1);

	commentln ("wp is ptr to interpol. filter weights for this sample");
	codeln ("float * wp = &(pp->wi[i][0]);");

	vspace (1);

	commentln ("wpe is FIR weight end sentinel");
	codeln ("float * wpe = wp + wiLen[i];");

	vspace (1);

	commentln ("xr is ptr to samples in input history");
	codeln ("float * xr = pp->xzp;");

	vspace (1);

	commentln ("Apply convolution");

	codeln ("while (wp < wpe) {");
	pushIndent ();
	codeln ("u += ((*wp++) * (*xr--));");
	popIndent ();
	codeln ("}");

	popIndent ();
	codeln ("}"); /* end of for IPOL */

	popIndent ();
	codeln ("}");
	codeln ("else {");
	pushIndent ();
	commentln ("Wrapping code");

	/* ***************************************************************************
   * We need to put in a for i -loop here as well, and duplicate the
   * transfer and output history code for both branches.
   * We also need to know which input sample to start with, x0 or x-1!
   * ***************************************************************************/

	sprintf (buf, "for (i = 0; i < %d; i++) {", R);
	codeln (buf);
	pushIndent ();

	vspace (1);

	commentln ("Interpolation weights for this sample");
	codeln ("float * wp = &(pp->wi[i][0]);");

	commentln ("Weight end sentinel");
	codeln ("float * wpe = wp + wiLen[i];");

	commentln ("Input history read pointer");
	codeln ("float * xr = pp->xzp;");

	vspace (1);

	codeln ("while (pp->xzb <= xr) {");
	pushIndent ();
	codeln ("u += ((*wp++) * (*xr--));");
	popIndent ();
	codeln ("}");

	vspace (1);

	sprintf (buf, "xr = &(pp->xzb[%d]);", XZB_SIZE - 1);
	codeln (buf);

	vspace (1);

	codeln ("while (wp < wpe) {");
	pushIndent ();
	codeln ("u += ((*wp++) * (*xr--));");
	popIndent ();
	codeln ("}");

	popIndent ();
	codeln ("}");

	popIndent ();
	codeln ("}");

	/* Call transfer function */

	vspace (1);

	commentln ("Apply transfer function");
	commentln ("v = T (u);");

#ifdef PRE_DC_OFFSET
#if 0
  codeln ("if ((-preDCOffset < u) && (u < preDCOffset)) {");
  pushIndent ();
  codeln ("u *= 0.0;");
  popIndent ();
  codeln ("} else {");
  pushIndent ();
  codeln ("u -= preDCOffset;");
  popIndent ();
  codeln ("}");
#endif
#endif /* PRE_DC_OFFSET */

	(transferdef) ("u", "v");

#ifdef PRE_DC_OFFSET
#if 1
	codeln ("if ((-preDCOffset < v) && (v < preDCOffset)) {");
	pushIndent ();
	codeln ("v = 0.0;");
	popIndent ();
	codeln ("} else {");
	pushIndent ();
	codeln ("v -= preDCOffset;");
	popIndent ();
	codeln ("}");
#endif
#endif /* PRE_DC_OFFSET */

	vspace (1);

	commentln ("Put transferred sample in output history.");
	codeln ("if (++pp->yzp == pp->yzpe) {");
	pushIndent ();
	codeln ("pp->yzp = pp->yzb;");
	popIndent ();
	codeln ("}");
	codeln ("*(pp->yzp) = v;");

	/*
   * Here we do the downsampling. Do a convolution similar to the one
   * above.
   */

	vspace (1);

	commentln ("Decimation");
	codeln ("if (pp->yzwp <= pp->yzp) {");
	pushIndent ();

	commentln ("No-wrap code");
	commentln ("wp points to weights in the decimation FIR");
	codeln ("float * wp = pp->aal;");
	codeln ("float * yr = pp->yzp;");

	vspace (1);

	commentln ("Convolve with decimation filter.");
	codeln ("while (wp < pp->aalEnd) {");
	pushIndent ();
	codeln ("y += ((*wp++) * (*yr--));");
	popIndent ();
	codeln ("}");

	popIndent ();
	codeln ("}");
	codeln ("else {");
	pushIndent ();
	commentln ("Wrap code");

	/* Two pass operation : first is down to zero, second from top */
	codeln ("float * wp = pp->aal;");
	codeln ("float * yr = pp->yzp;");

	vspace (1);

	codeln ("while (pp->yzb <= yr) {");
	pushIndent ();
	codeln ("y += ((*wp++) * (*yr--));");
	popIndent ();
	codeln ("}");

	vspace (1);

	sprintf (buf, "yr = &(pp->yzb[%d]);", YZB_SIZE - 1);
	codeln (buf);

	vspace (1);

	codeln ("while (wp < pp->aalEnd) {");
	pushIndent ();
	codeln ("y += ((*wp++) * (*yr--));");
	popIndent ();
	codeln ("}");

	popIndent ();
	codeln ("}");

	vspace (1);

	if (generatePostFilter) {
		assert (0);
		codeln ("Z0 = y - (de_a1 * de_z1) - (de_a2 * de_z2);");
		codeln ("xin = (de_b0 * Z0) + (de_b1 * de_z1) + (de_b2 * de_z2);");
		codeln ("de_z2 = de_z1;");
		codeln ("de_z1 = Z0;");
	}
/*
  else {
    codeln ("xin = y;");
  }
  */

#ifdef BASS_SIDECHAIN
	assert (0);
	codeln ("y = (0.9 * bb_1) + (0.1 * bb) + xin;");
	codeln ("bb_1 = bb;");
#else

#ifdef CLEAN_MIX
	codeln ("y = (mixClean * xin) + (mixFx * y);");
#endif /* CLEAN_MIX */

#endif /* BASS_SIDECHAIN */

#ifdef OUTPUT_GAIN
	codeln ("*yp++ = pp->outputGain * y;");
#else
	codeln ("*yp++ = y;");
#endif /* OUTPUT_GAIN */

	/* End of for-loop over input buffer */

	popIndent ();
	codeln ("}");
	commentln ("End of for-loop over input buffer");
	codeln ("return outBuf;");
}

/*
 * Ejects the function trailer.
 */
void
funcTrailer (char* funcName)
{
	popIndent ();
	code ("} ");
	commentln (funcName);
}

/*
 * Generates the unity transfer function
 */
void
t_unity (char* xs, char* ys)
{
	char buf[BUFSZ];

	assert (xs != NULL);
	assert (ys != NULL);

	commentln ("Unity function");
	sprintf (buf, "%s = %s;", ys, xs);
	codeln (buf);
}

/*
 * Generates the -x2+2x
 */
void
t_cubic1 (char* xs, char* ys)
{
	char buf[BUFSZ];
	commentln ("Cubic function");
	codeln ("{");
	pushIndent ();
	sprintf (buf, "float e = 6.0 * %s;", xs);
	codeln (buf);
	codeln ("if (e < 0.0) {");
	pushIndent ();
	codeln ("e = -e;");
	sprintf (buf, "%s = (e + e) - (e * e);", ys);
	codeln (buf);
	sprintf (buf, "%s = -%s;", ys, ys);
	codeln (buf);
	popIndent ();
	codeln ("} else {");
	pushIndent ();
	sprintf (buf, "%s = (e + e) - (e * e);", ys);
	codeln (buf);
	popIndent ();
	codeln ("}");
	popIndent ();
	codeln ("}");
}

void
adapterPreamp (char* funcName)
{
	int  xbufSize = 256;
	int  ybufSize = 256;
	char buf[BUFSZ];

	vspace (3);
	commentln ("Adapter function");
	codeln ("float * preamp (void * pa,");
	codeln ("                float * inBuf,");
	codeln ("                float * outBuf,");
	codeln ("                size_t bufLengthSamples) {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");

	codeln ("if (pp->isClean) {");
	pushIndent ();
	codeln ("memcpy(outBuf, inBuf, bufLengthSamples*sizeof(float));");
	popIndent ();
	codeln ("}");
	codeln ("else {");
	pushIndent ();
	codeln ("overdrive (pa, inBuf, outBuf, bufLengthSamples);");
	popIndent ();
	codeln ("}");

	vspace (1);

	codeln ("return outBuf;");

	popIndent ();
	codeln ("}");

	vspace (2);
	codeln ("void * allocPreamp () {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) calloc(1, sizeof(struct b_preamp));");

	codeln ("pp->xzp = &(pp->xzb[0]);");
	sprintf (buf, "pp->xzpe = &(pp->xzb[%d]);", XZB_SIZE);
	codeln (buf);
	sprintf (buf, "pp->xzwp = &(pp->xzb[%d]);", DFQ + 1);
	codeln (buf);
	codeln ("pp->yzp = &(pp->yzb[0]);");
	sprintf (buf, "pp->yzpe = &(pp->yzb[%d]);", YZB_SIZE);
	codeln (buf);
	sprintf (buf, "pp->yzwp = &(pp->yzb[%d]);", AAL_LEN);
	codeln (buf);
	sprintf (buf, "pp->aalEnd = &(pp->aal[%d]);", AAL_LEN);
	codeln (buf);

	sprintf (buf, "pp->ipolFilterLength = %d;", IPOL_LEN);
	codeln (buf);
	sprintf (buf, "pp->aalFilterLength = %d;", AAL_LEN);
	codeln (buf);
	codeln ("pp->isClean = 1;");

/* generatePreFilter */
/* generatePostFilter */

#ifdef OUTPUT_GAIN
	sprintf (buf, "pp->outputGain = %g;", OUTPUT_GAIN);
	codeln (buf);
#endif /* OUTPUT_GAIN */

#ifdef CLEAN_MIX
	codeln ("pp->mixClean = 0.0;");
	codeln ("pp->mixFx = 1.0;");
#endif /* CLEAN_MIX */

#ifdef INPUT_GAIN
	sprintf (buf, "pp->inputGain = %g;", INPUT_GAIN);
	codeln (buf);
#endif /* INPUT_GAIN */

#ifdef PRE_DC_OFFSET
	codeln ("pp->preDCOffset = 0.0;");
#endif /* PRE_DC_OFFSET */

#ifdef INPUT_COMPRESS
	sprintf (buf, "pp->ipcGain = %g;", IPC_GAIN_IDLE);
	codeln (buf);
	sprintf (buf, "pp->ipcThreshold = %g;", IPC_THRESHOLD);
	codeln (buf);
	sprintf (buf, "pp->ipcGainReduce = %g;", IPC_GAIN_REDUCE);
	codeln (buf);
	sprintf (buf, "pp->ipcGainRecover = %g;", IPC_GAIN_RECOVER);
	codeln (buf);
#endif /* INPUT_COMPRESS */

#ifdef SAG_EMULATION
	vspace (3);
	codeln ("pp->sagZ = 0.0;");
	sprintf (buf, "pp->sagFb = %g;", SAG_FB);
	codeln (buf);
#endif /* SAG_EMULATION */

	codeln (buf);
#ifdef TR_BIASED
	rst_biased ();
#endif

	codeln ("return pp;");
	popIndent ();
	codeln ("}");
	vspace (2);
	codeln ("void freePreamp (void * pa) {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");
	codeln ("free(pp);");
	popIndent ();
	codeln ("}");
}

void
legacyInit ()
{
	char buf[BUFSZ];
	vspace (3);
	commentln ("Legacy function");
	codeln ("void initPreamp (void *pa, void *m) {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");
	codeln ("mixFilterWeights (pa, ipwdef, aaldef);");

#ifdef PRE_FILTER_TYPE
	if (generatePreFilter) {
		codeln ("useMIDIControlFunction (m, \"xov.prefilter.hz\", setPreFilterHz, pa);");
		codeln ("useMIDIControlFunction (m, \"xov.prefilter.q\", setPreFilterQ, pa);");
		codeln ("useMIDIControlFunction(m, \"xov.prefilter.gain\", setPreFilterG, pa);");
		sprintf (buf, "preFilterDefine (%d, pr_F, pr_Q, pr_G);", PRE_FILTER_TYPE);
		codeln (buf);
	}
#endif /* PRE_FILTER_TYPE */

#ifdef POST_FILTER_TYPE
	if (generatePostFilter) {
		codeln ("useMIDIControlFunction (m, \"xov.postfilter.hz\", setPostFilterHz, pa);");
		codeln ("useMIDIControlFunction (m, \"xov.postfilter.q\", setPostFilterQ, pa);");
		codeln ("useMIDIControlFunction (m, \"xov.postfilter.gain\", setPostFilterG, pa);");
		sprintf (buf, "postFilterDefine (%d, de_F, de_Q, de_G);", POST_FILTER_TYPE);
		codeln (buf);
	}
#endif /* POST_FILTER_TYPE */

/* ***************************************************************************/

#ifdef TR_BIASED
	sprintf (buf, "useMIDIControlFunction (m, \"xov.ctl_biased\", ctl_biased, pa);");
	codeln (buf);

#ifdef ADWS_PRE_DIFF
	sprintf (buf,
	         "useMIDIControlFunction (m, \"xov.ctl_biased_fb\", ctl_biased_fb, pa);");
	codeln (buf);
#endif /* ADWS_PRE_DIFF */

#ifdef ADWS_POST_DIFF
	sprintf (buf,
	         "useMIDIControlFunction (m, \"xov.ctl_biased_fb2\", ctl_biased_fb2, pa);");
	codeln (buf);
#endif /* ADWS_POST_DIFF */

#ifdef ADWS_GFB
	sprintf (buf,
	         "useMIDIControlFunction (m, \"xov.ctl_biased_gfb\", ctl_biased_gfb, pa);");
	codeln (buf);
#endif /* ADWS_GFB */

#ifdef SAG_EMULATION
	sprintf (buf,
	         "useMIDIControlFunction (m, \"xov.ctl_sagtobias\", ctl_sagtoBias, pa);");
	codeln (buf);
#endif /* SAG_EMULATION */

#ifdef ADWS_FAT_CTRL
	sprintf (
	    buf,
	    "useMIDIControlFunction (m, \"overdrive.character\", ctl_biased_fat, pa);");
	codeln (buf);
#endif /* ADWS_FAT_CTRL */

	ini_biased ();
#endif /* TR_BIASED */

	/* ***************************************************************************/

	sprintf (buf, "useMIDIControlFunction (m, \"overdrive.enable\", setCleanCC, pa);");
	codeln (buf);

#ifdef INPUT_GAIN
	sprintf (buf, "useMIDIControlFunction (m, \"overdrive.inputgain\", setInputGain, pa);");
	codeln (buf);
#endif /* INPUT_GAIN */

#ifdef OUTPUT_GAIN
	sprintf (buf, "useMIDIControlFunction (m, \"overdrive.outputgain\", setOutputGain, pa);");
	codeln (buf);
#endif /* OUTPUT_GAIN */

#ifdef PRE_DC_OFFSET
	codeln ("useMIDIControlFunction (m, \"xov.pre_dc_offset\", setPreDCOffset, pa);");
#endif /* PRE_DC_OFFSET */

#ifdef CLEAN_MIX
	sprintf (buf, "useMIDIControlFunction (m, \"xov.mix.dry\", setCleanMix, pa);");
	codeln (buf);
#endif /* CLEAN_MIX */

#ifdef INPUT_COMPRESS
	codeln ("useMIDIControlFunction (m, \"xov.compressor.threshold\", setIpcThreshold, pa);");
	codeln ("useMIDIControlFunction (m, \"xov.compressor.attack\", setIpcAttack, pa);");
	codeln ("useMIDIControlFunction (m, \"xov.compressor.release\", setIpcRelease, pa);");
#endif /* INPUT_COMPRESS */
	popIndent ();
	codeln ("}");
}

void
legacyConfig ()
{
	char buf[BUFSZ];
	vspace (3);
	commentln ("Legacy function");
	codeln ("int ampConfig (void *pa, ConfigContext * cfg) {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");
	codeln ("int rtn = 1;");
	codeln ("float v = 0;");

	vspace (1);
	commentln ("Config generated by overmaker");
	vspace (1);

	sprintf (buf,
	         "if (getConfigParameter_f (\"%s\", cfg, &%s)) return 1;",
	         "overdrive.inputgain", "pp->inputGain");
	codeln (buf);

	sprintf (buf,
	         "else if (getConfigParameter_f (\"%s\", cfg, &%s)) return 1;",
	         "overdrive.outputgain", "pp->outputGain");
	codeln (buf);

#ifdef ADWS_GFB
	sprintf (buf,
	         "else if (getConfigParameter_f (\"%s\", cfg, &v)) { %s(pp, v); return 1; }",
	         "xov.ctl_biased_gfb", "fctl_biased_gfb");
	codeln (buf);
#endif

#ifdef TR_BIASED
	sprintf (buf,
	         "else if (getConfigParameter_f (\"%s\", cfg, &v)) { %s(pp, v); return 1; }",
	         "xov.ctl_biased", "fctl_biased");
	codeln (buf);
#endif

#ifdef ADWS_FAT_CTRL
	sprintf (buf,
	         "else if (getConfigParameter_f (\"%s\", cfg, &v)) { %s(pp, v); return 1; }",
	         "overdrive.character", "fctl_biased_fat");
	codeln (buf);
#endif /* ADWS_FAT_CTRL */

	vspace (1);
	commentln ("Config generated by external module");
	vspace (2);

	/* Invoke external callback for configuration. */
	if (modvec[MOD_CONFIG].fn != NULL) {
		(modvec[MOD_CONFIG].fn) (MOD_CONFIG, modvec[MOD_CONFIG].handback);
	}
	codeln ("return rtn;");
	popIndent ();
	codeln ("}");
	vspace (1);
}

void
writeDocumentation ()
{
	codeln ("#else // no CONFIGDOCONLY");
	codeln ("# include \"cfgParser.h\"");
	codeln ("#endif");

	vspace (2);
	codeln ("static const ConfigDoc doc[] = {");
	pushIndent ();
	codeln ("{\"overdrive.inputgain\", CFG_FLOAT, \"0.3567\", \"This is how much the input signal is scaled as it enters the overdrive effect. The default value is quite hot, but you can of course try it in anyway you like; range [0..1]\", INCOMPLETE_DOC},");
	codeln ("{\"overdrive.outputgain\", CFG_FLOAT, \"0.07873\", \"This is how much the signal is scaled as it leaves the overdrive effect. Essentially this value should be as high as possible without clipping (and you *will* notice when it does - Test with a bass-chord on 88 8888 000 with percussion enabled and full swell, but do turn down the amplifier/headphone volume first!); range [0..1]\", INCOMPLETE_DOC},");
#ifdef TR_BIASED
	codeln ("{\"xov.ctl_biased\", CFG_FLOAT, \"0.5347\", \"bias base; range [0..1]\", INCOMPLETE_DOC},");
#endif
#ifdef ADWS_GFB
	codeln ("{\"xov.ctl_biased_gfb\", CFG_FLOAT, \"0.6214\", \"Global [negative] feedback control; range [0..1]\", INCOMPLETE_DOC},");
#endif
#ifdef CLEAN_MIX
	codeln ("{\"xov...\", CFG_FLOAT, \"\", \"\", INCOMPLETE_DOC},"); /* TODO - unused in overmakerdefs.h */
#endif
#ifdef PRE_DC_OFFSET
	codeln ("{\"xov...\", CFG_FLOAT, \"\", \"\", INCOMPLETE_DOC},"); /* TODO - unused in overmakerdefs.h */
#endif
#ifdef INPUT_COMPRESS
	codeln ("{\"xov...\", CFG_FLOAT, \"\", \"\", INCOMPLETE_DOC},"); /* TODO - unused in overmakerdefs.h */
#endif
#ifdef PRE_FILTER_TYPE
	codeln ("{\"xov...\", CFG_FLOAT, \"\", \"\", INCOMPLETE_DOC},"); /* TODO - unused in overmakerdefs.h */
#endif

	codeln ("{\"overdrive.character\", CFG_FLOAT, \"-\", \"Abstraction to set xov.ctl_biased_fb and xov.ctl_biased_fb2\", INCOMPLETE_DOC},");
	if (modvec[MOD_DOC].fn != NULL) {
		(modvec[MOD_DOC].fn) (0, NULL);
	}
	codeln ("DOC_SENTINEL");
	popIndent ();
	codeln ("};");
	vspace (2);
	codeln ("const ConfigDoc *ampDoc () {");
	pushIndent ();
	codeln ("return doc;");
	popIndent ();
	codeln ("}");
}

void
legacySwell ()
{
	vspace (3);
	commentln ("Legacy function");
	codeln ("void setSwell (double d) {");
	pushIndent ();
#if 0
  codeln ("gain = (d < 0.0) ? 0.0 : (1.0 < d) ? 1.0 : d;");
#endif
	popIndent ();
	codeln ("}");
}

void
legacyGain ()
{
	vspace (3);
	commentln ("Legacy function");
	codeln ("void set7bitGain (unsigned char b) {");
	pushIndent ();
#if 0
  codeln ("gain = ((double) b) / 127.0;");
#endif
	popIndent ();
	codeln ("}");
}

void
legacyClean ()
{
	vspace (3);
	codeln ("void setClean (void *pa, int useClean) {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");
	codeln ("pp->isClean = useClean ? 1: 0;");
	popIndent ();
	codeln ("}");
	codeln ("void setCleanCC (void *pa, unsigned char uc) {");
	pushIndent ();
	codeln ("setClean(pa, uc > 63 ? 0 : 1);");
	popIndent ();
	codeln ("}");
}

/*
 * This function generates a function that can be called at target runtime
 * to set the pre-filter coefficients and type.
 */
void
preFilterDefine ()
{
	vspace (3);
	commentln ("Prefilter runtime definition");
	codeln ("void preFilterDefine (int type, double fq, double Q, double G)");
	codeln ("{");
	pushIndent ();
	codeln ("double C[6];");
	codeln ("eqCompute (type, fq, Q, G, C);");
	codeln ("pr_b0 = (float) C[EQC_B0];");
	codeln ("pr_b1 = (float) C[EQC_B1];");
	codeln ("pr_b2 = (float) C[EQC_B2];");
	codeln ("pr_a1 = (float) C[EQC_A1];");
	codeln ("pr_a2 = (float) C[EQC_A2];");

	codeln ("printf(\"\\rPRE:Hz:%10.4lf:Q:%10.4lf:G:%10.4lf\",fq,Q,G);");
	codeln ("fflush (stdout);");

	popIndent ();
	codeln ("}");
}

/*
 * This function generates a function that can be called at target runtime
 * to set the post-filter coefficients and type.
 */
void
postFilterDefine ()
{
	vspace (3);
	commentln ("Postfilter runtime definition");
	codeln ("void postFilterDefine (int type, double fq, double Q, double G)");
	codeln ("{");
	pushIndent ();
	codeln ("double C[6];");
	codeln ("eqCompute (type, fq, Q, G, C);");
	codeln ("de_b0 = (float) C[EQC_B0];");
	codeln ("de_b1 = (float) C[EQC_B1];");
	codeln ("de_b2 = (float) C[EQC_B2];");
	codeln ("de_a1 = (float) C[EQC_A1];");
	codeln ("de_a2 = (float) C[EQC_A2];");

	codeln ("printf(\"\\rPST:Hz:%10.4lf:Q:%10.4lf:G:%10.4lf\",fq,Q,G);");
	codeln ("fflush (stdout);");

	popIndent ();
	codeln ("}");
}

/* ***************************************************************************
 * This function generates MIDI controller functions for the pre-filter.
 */
#ifdef PRE_FILTER_TYPE
void
preFilterControl ()
{
	char buf[BUFSZ];
	vspace (3);
	commentln ("Pre-filter runtime control");
	codeln ("void setPreFilterHz (void *d, unsigned char uc) {");
	pushIndent ();

	codeln ("double u = (double) uc;");
	sprintf (buf,
	         "pr_F = %g + ((%g - %g) * ((u * u) / 16129.0));",
	         PRE_FILTER_HERTZ_LO,
	         PRE_FILTER_HERTZ_HI,
	         PRE_FILTER_HERTZ_LO);
	codeln (buf);
	sprintf (buf, "preFilterDefine (%d, pr_F, pr_Q, pr_G);", PRE_FILTER_TYPE);
	codeln (buf);

	popIndent ();
	codeln ("}");

	vspace (3);

	codeln ("void setPreFilterQ (void *d, unsigned char uc) {");
	pushIndent ();
	sprintf (buf,
	         "pr_Q = %g + ((%g - %g) * (((double) uc) / 127.0));",
	         PRE_FILTER_Q_LO,
	         PRE_FILTER_Q_HI,
	         PRE_FILTER_Q_LO);
	codeln (buf);
	sprintf (buf, "preFilterDefine (%d, pr_F, pr_Q, pr_G);", PRE_FILTER_TYPE);
	codeln (buf);
	popIndent ();
	codeln ("}");

	vspace (3);

	codeln ("void setPreFilterG (void *d, unsigned char uc) {");
	pushIndent ();
	sprintf (buf,
	         "pr_G = %g + ((%g - %g) * (((double) uc) / 127.0));",
	         PRE_FILTER_G_LO,
	         PRE_FILTER_G_HI,
	         PRE_FILTER_G_LO);
	codeln (buf);
	sprintf (buf, "preFilterDefine (%d, pr_F, pr_Q, pr_G);", PRE_FILTER_TYPE);
	codeln (buf);
	popIndent ();
	codeln ("}");
}
#endif /* PRE_FILTER_TYPE */

/* ***************************************************************************
 * This function generates MIDI controller functions for the post-filter.
 */
#ifdef POST_FILTER_TYPE
void
postFilterControl ()
{
	char buf[BUFSZ];
	vspace (3);
	commentln ("Post-filter runtime control");
	codeln ("void setPostFilterHz (void *d, unsigned char uc) {");
	pushIndent ();

	codeln ("double u = (double) uc;");
	sprintf (buf,
	         "de_F = %g + ((%g - %g) * ((u * u) / 16129.0));",
	         POST_FILTER_HERTZ_LO,
	         POST_FILTER_HERTZ_HI,
	         POST_FILTER_HERTZ_LO);
	codeln (buf);
	sprintf (buf, "postFilterDefine(%d, de_F, de_Q, de_G);", POST_FILTER_TYPE);
	codeln (buf);

	popIndent ();
	codeln ("}");

	vspace (3);

	codeln ("void setPostFilterQ (void *d, unsigned char uc) {");
	pushIndent ();
	sprintf (buf,
	         "de_Q = %g + ((%g - %g) * (((double) uc) / 127.0));",
	         POST_FILTER_Q_LO,
	         POST_FILTER_Q_HI,
	         POST_FILTER_Q_LO);
	codeln (buf);
	sprintf (buf, "postFilterDefine(%d, de_F, de_Q, de_G);", POST_FILTER_TYPE);
	codeln (buf);
	popIndent ();
	codeln ("}");

	vspace (3);

	codeln ("void setPostFilterG (void *d, unsigned char uc) {");
	pushIndent ();
	sprintf (buf,
	         "de_G = %g + ((%g - %g) * (((double) uc) / 127.0));",
	         POST_FILTER_G_LO,
	         POST_FILTER_G_HI,
	         POST_FILTER_G_LO);
	codeln (buf);
	sprintf (buf, "postFilterDefine(%d, de_F, de_Q, de_G);", POST_FILTER_TYPE);
	codeln (buf);
	popIndent ();
	codeln ("}");
}
#endif /* POST_FILTER_TYPE */

/* ***************************************************************************/

#ifdef OUTPUT_GAIN
void
outputGainControl ()
{
	char buf[BUFSZ];
	vspace (3);
	codeln ("void setOutputGain (void *pa, unsigned char uc) {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");
	sprintf (buf, "pp->outputGain = %g + ((%g - %g) * (((float) uc) / 127.0));",
	         OUTPUT_GAIN_LO,
	         OUTPUT_GAIN_HI,
	         OUTPUT_GAIN_LO);
	codeln (buf);
	codeln ("printf (\"\\rOUT:%10.4lf\", pp->outputGain);");
	codeln ("fflush (stdout);");
	popIndent ();
	codeln ("}");
	INTWRAP ("setOutputGain")
}
#endif /* OUTPUT_GAIN */

#ifdef INPUT_GAIN
void
inputGainControl ()
{
	char buf[BUFSZ];
	vspace (3);
	codeln ("void setInputGain (void *pa, unsigned char uc) {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");
	sprintf (buf, "pp->inputGain = %g + ((%g - %g) * (((float) uc) / 127.0));",
	         INPUT_GAIN_LO,
	         INPUT_GAIN_HI,
	         INPUT_GAIN_LO);
	codeln (buf);
	codeln ("printf (\"\\rINP:%10.4lf\", pp->inputGain);");
	codeln ("fflush (stdout);");
	popIndent ();
	codeln ("}");
	INTWRAP ("setInputGain")
}
#endif /* INPUT_GAIN */

#ifdef PRE_DC_OFFSET
void
preDCOffsetControl ()
{
	codeln ("void setPreDCOffset (void *d, unsigned char uc) {");
	pushIndent ();
	/*   codeln ("preDCOffset = 1.0 + ((((float) uc) / 127.0) * -2.0);"); */
	codeln ("preDCOffset = (((float) uc) / 511.0);");
	codeln ("printf (\"\\rPDC=%10.4f\", preDCOffset);");
	codeln ("fflush (stdout);");
	popIndent ();
	codeln ("}");
}
#endif /* PRE_DC_OFFSET */

#ifdef CLEAN_MIX
void
cleanMixControl ()
{
	codeln ("void setCleanMix (void *d, unsigned char uc) {");
	pushIndent ();
	codeln ("float v = ((float) uc) / 127.0;");
	codeln ("mixClean = v;");
	codeln ("mixFx = 1.0 - v;");
	popIndent ();
	codeln ("}");
}
#endif /* CLEAN_MIX */

#ifdef INPUT_COMPRESS
void
inputCompressControl ()
{
	char buf[BUFSZ];

	codeln ("void setIpcThreshold (void *d, unsigned char uc) {");
	pushIndent ();
	codeln ("float ratio = ((float) uc) / 127.0;");
	sprintf (buf, "ipcThreshold = %g + ((%g - %g) * ratio);",
	         IPC_THR_LO,
	         IPC_THR_HI,
	         IPC_THR_LO);
	codeln (buf);
	codeln ("printf (\"\\rCTH=%10.4f\", ipcThreshold);");
	codeln ("fflush (stdout);");
	popIndent ();
	codeln ("}");

	vspace (3);

	codeln ("void setIpcAttack (void *d, unsigned char uc) {");
	pushIndent ();
	codeln ("float ratio = ((float) uc) / 127.0;");
	sprintf (buf, "ipcGainReduce = %g + ((%g - %g) * ratio);",
	         IPC_ATK_LO, IPC_ATK_HI, IPC_ATK_LO);
	codeln (buf);
	codeln ("printf (\"\\rCAT=%10.4f\", ipcGainReduce);");
	codeln ("fflush (stdout);");
	popIndent ();
	codeln ("}");

	vspace (3);

	codeln ("void setIpcRelease (void *d, unsigned char uc) {");
	pushIndent ();
	codeln ("float ratio = ((float) uc) / 127.0;");
	sprintf (buf, "ipcGainRecover = %g + ((%g - %g) * ratio);",
	         IPC_RLS_LO, IPC_RLS_HI, IPC_RLS_LO);
	codeln (buf);
	codeln ("printf (\"\\rCRL=%10.4f\", ipcGainRecover);");
	codeln ("fflush (stdout);");
	popIndent ();
	codeln ("}");
}
#endif /* INPUT_COMPRESS */

/*
 * The render function.
 */
void
render (FILE* fp, char* funcName)
{
	char buf[BUFSZ];

	setOutputStream (fp);

	codeln ("#ifndef CONFIGDOCONLY");

#ifdef IPOL_FC
	computeIpolFilter (IPOL_FC, IPOL_WDW);
	sprintf (buf, "Interpolation filter at digital frequency %g", IPOL_FC);
	commentln (buf);
#endif /* IPOL_FC */

#ifdef AAL_FC
	computeAalFilter (AAL_FC, AAL_WDW);
	sprintf (buf, "Decimation filter at digital frequency %g", AAL_FC);
	commentln (buf);
#endif /* AAL_FC */

	computeIpolWeights ();

	moduleHeader ();

	preIpolMixer ();

	funcHeader (funcName);
	funcVarDef ();

#ifdef TR_BIASED
	funcBody (xfr_biased);
#endif /* TR_BIASED */

	funcTrailer (funcName);

	adapterPreamp (funcName);

	legacyClean ();
#if 1
	legacyConfig ();
#endif
	vspace (3);

#ifdef TR_BIASED
	cfg_biased ();
#endif /* TR_BIASED */

#ifdef PRE_FILTER_TYPE
	preFilterDefine ();
#endif /* PRE_FILTER_TYPE */

#ifdef POST_FILTER_TYPE
	postFilterDefine ();
#endif /* POST_FILTER_TYPE */

#ifdef PRE_FILTER_TYPE
	preFilterControl ();
#endif /* PRE_FILTER_TYPE */

#ifdef POST_FILTER_TYPE
	postFilterControl ();
#endif /* POST_FILTER_TYPE */

#ifdef TR_BIASED
	ctl_biased ();
#endif /* TR_BIASED */

#ifdef INPUT_GAIN
	inputGainControl ();
#endif /* INPUT_GAIN */

#ifdef OUTPUT_GAIN
	outputGainControl ();
#endif /* OUTPUT_GAIN */

#ifdef PRE_DC_OFFSET
	preDCOffsetControl ();
#endif /* PRE_DC_OFFSET */

#ifdef CLEAN_MIX
	cleanMixControl ();
#endif /* CLEAN_MIX */

#ifdef INPUT_COMPRESS
	inputCompressControl ();
#endif /* INPUT_COMPRESS */

	legacyInit ();

	writeDocumentation ();
}

/*
 * This routine is available to external modules that want to register
 * callbacks.
 */
void
bindCallback (int i, void (*func) (int, void*), void* hbk)
{
	assert ((0 <= i) && (i < MODVECSZ));
	modvec[i].fn       = func;
	modvec[i].handback = hbk;
}

/*
 * MAIN
 */
int
main (int argc, char* argv[])
{
#ifdef HAS_CALLBACKS
	bindCallbacks (); /* Implemented by external module */
#endif                    /* HAS_CALLBACKS */

	render (stdout, "overdrive");

	return 0;
}
