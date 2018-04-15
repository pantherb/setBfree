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

/* ovt_biased.c --- 1/x transfer function for overmaker.c */
/* 27-mar-2004/FK The negative feedback appears positive. We are subtracting
 *                a negative value. Check that and make it positive if needed.
 */

#include <stdio.h>
#include <stdlib.h>

#include "overmaker.h"
#include "overmakerdefs.h"

static char buf[BUFSZ];

/*
 * Sets up static and global symbols in the overdrive.c file generated
 * by overmaker.c
 */

void
hdr_biased ()
{
	commentln ("Variables for the inverted and biased transfer function");
	codeln ("float biasBase;");
	commentln ("bias and norm are set in function cfg_biased()");
	codeln ("float bias;");
	codeln ("float norm;");

#ifdef ADWS_PRE_DIFF
	commentln ("ovt_biased : One sample memory");
	codeln ("float adwZ;");
	commentln ("ovt_biased : Positive feedback");
	codeln ("float adwFb;");
	codeln (buf);
#endif /* ADWS_PRE_DIFF */

#ifdef ADWS_POST_DIFF
	codeln ("float adwZ1;");
#if 0
  codeln ("float adwZ2;");
#endif
	codeln ("float adwFb2;");
#endif /* ADWS_POST_DIFF */

#ifdef ADWS_GFB
	codeln ("float adwGfb;");
	codeln ("float adwGfZ;");
#endif /* ADWS_GFB */

#ifdef SAG_EMULATION
	codeln ("float sagZgb;");
#endif
}

void
rst_biased ()
{
	sprintf (buf, "pp->biasBase = %g;", BIAS);
	codeln (buf);

#ifdef ADWS_PRE_DIFF
	codeln ("pp->adwZ = 0.0;");
	sprintf (buf, "pp->adwFb = %g;", ADWS_FB);
	codeln (buf);
#endif /* ADWS_PRE_DIFF */

#ifdef ADWS_POST_DIFF
	codeln ("pp->adwZ1 = 0.0;");
#if 0
  codeln ("static float adwZ2;");
#endif
	sprintf (buf, "pp->adwFb2 = %g;", ADWS_FB2);
	codeln (buf);
#endif /* ADWS_POST_DIFF */

#ifdef ADWS_GFB
	codeln ("pp->adwGfb = -0.6214;");
	codeln ("pp->adwGfZ = 0.0;");
#endif /* ADWS_GFB */

#ifdef SAG_EMULATION
	sprintf (buf, "pp->sagZgb = %g;", SAG_ZGB);
	codeln (buf);
#endif
}

void
cfg_biased ()
{
	commentln ("Computes the constants for transfer curve");
	codeln ("void cfg_biased (void *pa, float new_bias) {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");
	codeln ("if (0.0 < new_bias) {");
	pushIndent ();
	codeln ("pp->biasBase = new_bias;");
	commentln ("If power sag emulation is enabled bias is set there.");
	codeln ("pp->bias = pp->biasBase;");
	codeln ("pp->norm = 1.0 - (1.0 / (1.0 + (pp->bias * pp->bias)));");
#if 0
	codeln ("printf (\"\\rBIAS=%10.4fNORM=%10.4f\", bias, norm);");
	codeln ("fflush (stdout);");
#endif
	popIndent ();
	codeln ("}");
	popIndent ();
	codeln ("}");
}

void
ctl_biased ()
{
	codeln ("void fctl_biased (void *pa, float u) {");
	pushIndent ();
	sprintf (buf, "float v = %g + ((%g - %g) * (u * u));",
	         BIAS_LO,
	         BIAS_HI,
	         BIAS_LO);
	codeln (buf);
	codeln ("cfg_biased (pa, v);");
	popIndent ();
	codeln ("}");
	FLOATWRAP ("ctl_biased")

#ifdef ADWS_PRE_DIFF
	vspace (3);
	commentln ("ovt_biased:Sets the positive feedback");
	codeln ("void fctl_biased_fb (void *pa, float u) {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");
	sprintf (buf, "pp->adwFb = %g * u;", ADWS_FB_MAX);
	codeln (buf);
	codeln ("printf (\"\\rFbk=%10.4f\", pp->adwFb);");
	codeln ("fflush (stdout);");
	popIndent ();
	codeln ("}");

	FLOATWRAP ("ctl_biased_fb")
#endif /* ADWS_PRE_DIFF */

#ifdef SAG_EMULATION
	vspace (3);
	commentln ("ovt_biased: Sets sag impact");
	codeln ("void fctl_sagtoBias (void *pa, float u) {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");
	sprintf (buf, "pp->sagZgb = %g + ((%g - %g) * u);",
	         SAG_ZGB_LO,
	         SAG_ZGB_HI,
	         SAG_ZGB_LO);
	codeln (buf);
	codeln ("printf (\"\\rpp->ZGB=%10.4f\", pp->sagZgb);");
	codeln ("fflush (stdout);");
	popIndent ();
	codeln ("}");
	FLOATWRAP ("ctl_sagtoBias")
#endif /* SAG_EMULATION */

#ifdef ADWS_POST_DIFF
	vspace (3);
	commentln ("ovt_biased: Postdiff feedback control");
	codeln ("void fctl_biased_fb2 (void *pa, float u) {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");
	sprintf (buf, "pp->adwFb2 = %g * u;", ADWS_FB2_MAX);
	codeln (buf);
	codeln ("printf (\"\\rFb2=%10.4f\", pp->adwFb2);");
	codeln ("fflush (stdout);");
	popIndent ();
	codeln ("}");
	FLOATWRAP ("ctl_biased_fb2")
#endif /* ADWS_POST_DIFF */

#ifdef ADWS_GFB
	vspace (3);
	commentln ("ovt_biased: Global feedback control");
	codeln ("void fctl_biased_gfb (void *pa, float u) {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");
	sprintf (buf, "pp->adwGfb = %g * u;", ADWS_GFB_MAX);
	codeln (buf);
	codeln ("printf (\"\\rGfb=%10.4f\", pp->adwGfb);");
	codeln ("fflush (stdout);");
	popIndent ();
	codeln ("}");
	FLOATWRAP ("ctl_biased_gfb")
#endif /* ADWS_GFB */

#ifdef ADWS_FAT_CTRL
	vspace (3);
	commentln ("ovt_biased: Fat control");
	codeln ("void ctl_biased_fat (void *pa, unsigned char uc) {");
	pushIndent ();
	codeln ("struct b_preamp *pp = (struct b_preamp *) pa;");
	codeln ("if (uc < 64) {");
	pushIndent ();
	codeln ("if (uc < 32) {");
	pushIndent ();
	sprintf (buf, "pp->adwFb = %g;", ADWS_FB);
	codeln (buf);
	sprintf (buf, "pp->adwFb2 = %g + ((%g - %g) * (((float) uc) / 31.0));",
	         ADWS_FB2, ADWS_FB, ADWS_FB2);
	codeln (buf);
	popIndent ();
	codeln ("} else {");
	pushIndent ();
	sprintf (buf, "pp->adwFb = %g + ((%g - %g) * (((float) (uc - 32)) / 31.0));",
	         ADWS_FB, ADWS_FB_MAX, ADWS_FB);
	codeln (buf);
	sprintf (buf, "pp->adwFb2 = %g;", ADWS_FB);
	codeln (buf);
	popIndent ();
	codeln ("}");

	popIndent ();
	codeln ("} else {");
	pushIndent ();
	sprintf (buf, "pp->adwFb = %g;", ADWS_FB_MAX);
	codeln (buf);
	sprintf (buf, "pp->adwFb2 = %g + ((%g - %g) * (((float) (uc - 64)) / 63.0));",
	         ADWS_FB, ADWS_FB_MAX, ADWS_FB);
	codeln (buf);
	popIndent ();
	codeln ("}");

	popIndent ();
	codeln ("}");

	INTWRAP ("ctl_biased_fat")
#endif /* ADWS_FAT_CTRL */
}

void
configSection (int i, void* pa)
{
	sprintf (buf,
	         "if (getConfigParameter_fr (\"%s\", cfg, &pp->adwFb, %g, %g));",
	         "xov.ctl_biased_fb",
	         0.0,
	         ADWS_FB_MAX);
	codeln (buf);

	sprintf (buf,
	         "else if (getConfigParameter_fr (\"%s\", cfg, &pp->adwFb2, %g, %g));",
	         "xov.ctl_biased_fb2",
	         0.0,
	         ADWS_FB2_MAX);
	codeln (buf);

	sprintf (buf,
	         "else if (getConfigParameter_f (\"%s\", cfg, &pp->sagFb));",
	         "xov.ctl_sagtobias");
	codeln (buf);

	codeln ("else return 0;");
}

void
configDoc (int i, void* vp)
{
	codeln ("{\"xov.ctl_biased_fb\", CFG_FLOAT, \"0.5821\", \"This parameter behaves somewhat like an analogue tone control for bass mounted before the overdrive stage. Unity is somewhere around the value 0.6, lesser values takes away bass and lowers the volume while higher values gives more bass and more signal into the overdrive. Must be less than 1.0.\", INCOMPLETE_DOC},");
	codeln ("{\"xov.ctl_biased_fb2\", CFG_FLOAT, \"0.999\", \"The fb2 parameter has the same function as fb1 but controls the signal after the overdrive stage. Together the two parameters are useful in that they can reduce the amount of bass going into the overdrive and then recover it on the other side. Must be less than 1.0.\", INCOMPLETE_DOC},");
	codeln ("{\"xov.ctl_sagtobias\", CFG_FLOAT, \"0.1880\", \"This parameter is part of an attempt to recreate an artefact called 'power sag'. When a power amplifier is under heavy load the voltage drops and alters the operating parameters of the unit, usually towards more and other kinds of distortion. The sagfb parameter controls the rate of recovery from the sag effect when the load is lifted. Must be less than 1.0.\", INCOMPLETE_DOC},");
}

void
ini_biased ()
{
	sprintf (buf, "cfg_biased (pa, %g);", BIAS);
	codeln (buf);
#ifdef ADWS_PRE_DIFF
	/* This is redundant if we have initialized the static var correctly */
	sprintf (buf, "pp->adwFb = %g;", ADWS_FB);
	codeln (buf);
#endif /* ADWS_PRE_DIFF */
}

void
xfr_biased (char* xs, char* ys)
{
	commentln ("Adaptive linear-non-linear transfer function");

#ifdef ADWS_GFB
	commentln ("Global negative feedback");
	sprintf (buf, "%s -= (pp->adwGfb * pp->adwGfZ);", xs);
	codeln (buf);
#endif /* ADWS_GFB */

#ifdef ADWS_PRE_DIFF
	codeln ("{");
	pushIndent ();
	sprintf (buf, "float temp = %s - pp->adwZ;", xs);
	codeln (buf);
	sprintf (buf, "pp->adwZ = %s + (pp->adwZ * pp->adwFb);", xs);
	codeln (buf);
	sprintf (buf, "%s = temp;", xs);
	codeln (buf);
	popIndent ();
	codeln ("}");
#endif /* ADWS_PRE_DIFF */

	sprintf (buf, "if (%s < 0.0) {", xs);
	codeln (buf);
	pushIndent ();

	/* Temp var to hold value to be squared */
	sprintf (buf, "float x2 = %s - pp->bias;", xs);
	codeln (buf);

	sprintf (buf, "%s = (1.0 / (1.0 + (x2 * x2))) - 1.0 + pp->norm;", ys);
	codeln (buf);

	popIndent ();
	codeln ("} else {");
	pushIndent ();

	/* Temp var to hold value to be squared */
	sprintf (buf, "float x2 = %s + pp->bias;", xs);
	codeln (buf);

	sprintf (buf, "%s = 1.0 - pp->norm - (1.0 / (1.0 + (x2 * x2)));", ys);
	codeln (buf);

	popIndent ();
	codeln ("}");

#ifdef ADWS_POST_DIFF
	codeln ("{");
	pushIndent ();

	sprintf (buf, "float temp = %s + (pp->adwFb2 * pp->adwZ1);", ys);
	codeln (buf);
	sprintf (buf, "%s = temp - pp->adwZ1;", ys);
	codeln (buf);
	codeln ("pp->adwZ1 = temp;");

	popIndent ();
	codeln ("}");
#endif /* ADWS_POST_DIFF */

#ifdef ADWS_GFB
	commentln ("Global negative feedback");
	sprintf (buf, "pp->adwGfZ = %s;", ys);
	codeln (buf);
#endif /* ADWS_GFB */
}

void
bindCallbacks ()
{
	bindCallback (MOD_CONFIG, configSection, NULL);
	bindCallback (MOD_DOC, configDoc, NULL);
}
