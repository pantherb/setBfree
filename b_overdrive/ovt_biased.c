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

void hdr_biased () {
  commentln ("Variables for the inverted and biased transfer function");
  sprintf (buf, "static float biasBase = %g;", BIAS);
  codeln (buf);
  commentln ("bias and norm are set in function cfg_biased()");
  codeln ("static float bias;");
  codeln ("static float norm;");

#ifdef ADWS_PRE_DIFF
  commentln ("ovt_biased : One sample memory");
  codeln ("static float adwZ = 0.0; // initialize to zero");
  commentln ("ovt_biased : Positive feedback");
  sprintf (buf, "static float adwFb = %g;", ADWS_FB);
  codeln (buf);
#endif /* ADWS_PRE_DIFF */

#ifdef ADWS_POST_DIFF
  codeln ("static float adwZ1 = 0.0; // initialize to zero");
#if 0
  codeln ("static float adwZ2;");
#endif
  sprintf (buf, "static float adwFb2 = %g;", ADWS_FB2);
  codeln (buf);
#endif /* ADWS_POST_DIFF */

#ifdef ADWS_GFB
  codeln ("static float adwGfb = -0.6214;");
  codeln ("static float adwGfZ = 0.0;");
#endif /* ADWS_GFB */

#ifdef SAG_EMULATION
  sprintf (buf, "static float sagZgb = %g;", SAG_ZGB);
  codeln (buf);
#endif /* SAG_EMULATION */

}

void cfg_biased () {
  commentln ("Computes the constants for transfer curve");
  codeln ("void cfg_biased (float new_bias) {");
  pushIndent ();
  codeln ("if (0.0 < new_bias) {");
  pushIndent ();
  codeln ("biasBase = new_bias;");
  commentln ("If power sag emulation is enabled bias is set there.");
  codeln ("bias = biasBase;");
  codeln ("norm = 1.0 - (1.0 / (1.0 + (bias * bias)));");
#if 0
  codeln ("printf (\"\\rBIAS=%10.4fNORM=%10.4f\", bias, norm);");
  codeln ("fflush (stdout);");
#endif
  popIndent ();
  codeln ("}");
  popIndent ();
  codeln ("}");
}

void ctl_biased () {
  codeln ("void fctl_biased (float u) {");
  pushIndent ();
  sprintf (buf, "float v = %g + ((%g - %g) * (u * u));",
	   BIAS_LO,
	   BIAS_HI,
	   BIAS_LO);
  codeln (buf);
  codeln ("cfg_biased (v);");
  popIndent ();
  codeln ("}");
	FLOATWRAP("ctl_biased")

#ifdef ADWS_PRE_DIFF
  vspace (3);
  commentln ("ovt_biased:Sets the positive feedback");
  codeln ("void fctl_biased_fb (float u) {");
  pushIndent ();
  sprintf (buf, "adwFb = %g * u;", ADWS_FB_MAX);
  codeln (buf);
  codeln ("printf (\"\\rFbk=%10.4f\", adwFb);");
  codeln ("fflush (stdout);");
  popIndent ();
  codeln ("}");

	FLOATWRAP("ctl_biased_fb")
#endif /* ADWS_PRE_DIFF */

#ifdef SAG_EMULATION
  vspace (3);
  commentln ("ovt_biased: Sets sag impact");
  codeln ("void fctl_sagtoBias (float u) {");
  pushIndent ();
  sprintf (buf, "sagZgb = %g + ((%g - %g) * u);",
	   SAG_ZGB_LO,
	   SAG_ZGB_HI,
	   SAG_ZGB_LO);
  codeln (buf);
  codeln ("printf (\"\\rZGB=%10.4f\", sagZgb);");
  codeln ("fflush (stdout);");
  popIndent ();
  codeln ("}");
	FLOATWRAP("ctl_sagtoBias")
#endif /* SAG_EMULATION */

#ifdef ADWS_POST_DIFF
  vspace (3);
  commentln ("ovt_biased: Postdiff feedback control");
  codeln ("void fctl_biased_fb2 (float u) {");
  pushIndent ();
  sprintf (buf, "adwFb2 = %g * u;", ADWS_FB2_MAX);
  codeln (buf);
  codeln ("printf (\"\\rFb2=%10.4f\", adwFb2);");
  codeln ("fflush (stdout);");
  popIndent ();
  codeln ("}");
	FLOATWRAP("ctl_biased_fb2")
#endif /* ADWS_POST_DIFF */

#ifdef ADWS_GFB
  vspace (3);
  commentln ("ovt_biased: Global feedback control");
  codeln ("void fctl_biased_gfb (float u) {");
  pushIndent ();
  sprintf (buf, "adwGfb = %g * u;", ADWS_GFB_MAX);
  codeln (buf);
  codeln ("printf (\"\\rGfb=%10.4f\", adwGfb);");
  codeln ("fflush (stdout);");
  popIndent ();
  codeln ("}");
	FLOATWRAP("ctl_biased_gfb")
#endif /* ADWS_GFB */

#ifdef ADWS_FAT_CTRL
  vspace (3);
  commentln ("ovt_biased: Fat control");
  codeln ("void ctl_biased_fat (unsigned char uc) {");
  pushIndent ();
  codeln ("if (uc < 64) {");
  pushIndent ();
  codeln ("if (uc < 32) {");
  pushIndent ();
  sprintf (buf, "adwFb = %g;", ADWS_FB);
  codeln (buf);
  sprintf (buf, "adwFb2 = %g + ((%g - %g) * (((float) uc) / 31.0));",
	   ADWS_FB2, ADWS_FB, ADWS_FB2);
  codeln (buf);
  popIndent ();
  codeln ("} else {");
  pushIndent ();
  sprintf (buf, "adwFb = %g + ((%g - %g) * (((float) (uc - 32)) / 31.0));",
	   ADWS_FB, ADWS_FB_MAX, ADWS_FB);
  codeln (buf);
  sprintf (buf, "adwFb2 = %g;", ADWS_FB);
  codeln (buf);
  popIndent ();
  codeln ("}");

  popIndent ();
  codeln ("} else {");
  pushIndent ();
  sprintf (buf, "adwFb = %g;", ADWS_FB_MAX);
  codeln (buf);
  sprintf (buf, "adwFb2 = %g + ((%g - %g) * (((float) (uc - 64)) / 63.0));",
	   ADWS_FB, ADWS_FB_MAX, ADWS_FB);
  codeln (buf);
  popIndent ();
  codeln ("}");

  popIndent ();
  codeln ("}");

	INTWRAP("ctl_biased_fat")
#endif /* ADWS_FAT_CTRL */
}

/*
 *
 */
void configSection (int i, void * vp) {
  sprintf (buf,
	   "if (getConfigParameter_fr (\"%s\", cfg, &adwFb, %g, %g));",
	   "xov.ctl_biased_fb",
	   0.0,
	   ADWS_FB_MAX);
  codeln (buf);

  sprintf (buf,
	   "else if (getConfigParameter_fr (\"%s\", cfg, &adwFb2, %g, %g));",
	   "xov.ctl_biased_fb2",
	   0.0,
	   ADWS_FB2_MAX);
  codeln (buf);

  sprintf (buf,
	   "else if (getConfigParameter_f (\"%s\", cfg, &sagFb));",
	   "xov.ctl_sagtobias");
  codeln (buf);

  codeln ("else return 0;");

}

void configDoc (int i, void * vp) {
  codeln ("{\"xov.ctl_biased_fb\", CFG_FLOAT, \"0.5821\", \"This parameter behaves somewhat like an analogue tone control for bass mounted before the overdrive stage. Unity is somewhere around the value 0.6, lesser values takes away bass and lowers the volume while higher values gives more bass and more signal into the overdrive. Must be less than 1.0.\"},");
  codeln ("{\"xov.ctl_biased_fb2\", CFG_FLOAT, \"0.999\", \"The fb2 parameter has the same function as fb1 but controls the signal after the overdrive stage. Together the two parameters are useful in that they can reduce the amount of bass going into the overdrive and then recover it on the other side. Must be less than 1.0.\"},");
  codeln ("{\"xov.ctl_sagtobias\", CFG_FLOAT, \"0.1880\", \"This parameter is part of an attempt to recreate an artefact called 'power sag'. When a power amplifier is under heavy load the voltage drops and alters the operating parameters of the unit, usually towards more and other kinds of distortion. The sagfb parameter controls the rate of recovery from the sag effect when the load is lifted. Must be less than 1.0.\"},");
}

void ini_biased () {
  sprintf (buf, "cfg_biased (%g);", BIAS);
  codeln (buf);
#ifdef ADWS_PRE_DIFF
  /* This is redundant if we have initialized the static var correctly */
  sprintf (buf, "adwFb = %g;", ADWS_FB);
  codeln (buf);
#endif /* ADWS_PRE_DIFF */
}

void xfr_biased (char * xs, char * ys) {
  commentln ("Adaptive linear-non-linear transfer function");

#ifdef ADWS_GFB
  commentln ("Global negative feedback");
  sprintf (buf, "%s -= (adwGfb * adwGfZ);", xs);
  codeln (buf);
#endif /* ADWS_GFB */

#ifdef ADWS_PRE_DIFF
  codeln ("{");
  pushIndent ();
  sprintf (buf, "float temp = %s - adwZ;", xs);
  codeln (buf);
  sprintf (buf, "adwZ = %s + (adwZ * adwFb);", xs);
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
  sprintf (buf, "float x2 = %s - bias;", xs);
  codeln (buf);

  sprintf (buf, "%s = (1.0 / (1.0 + (x2 * x2))) - 1.0 + norm;", ys);
  codeln (buf);

  popIndent ();
  codeln ("} else {");
  pushIndent ();

  /* Temp var to hold value to be squared */
  sprintf (buf, "float x2 = bias + %s;", xs);
  codeln (buf);

  sprintf (buf, "%s = 1.0 - norm - (1.0 / (1.0 + (x2 * x2)));", ys);
  codeln (buf);

  popIndent ();
  codeln ("}");


#ifdef ADWS_POST_DIFF
  codeln ("{");
  pushIndent ();

  sprintf (buf, "float temp = %s + (adwFb2 * adwZ1);", ys);
  codeln (buf);
  sprintf (buf, "%s = temp - adwZ1;", ys);
  codeln (buf);
  codeln ("adwZ1 = temp;");

  popIndent ();
  codeln ("}");
#endif /* ADWS_POST_DIFF */
 
#ifdef ADWS_GFB
  commentln ("Global negative feedback");
  sprintf (buf, "adwGfZ = %s;", ys);
  codeln (buf);
#endif /* ADWS_GFB */
}

/*
 *
 */
void bindCallbacks () {
  bindCallback (MOD_CONFIG, configSection, NULL);
  bindCallback (MOD_DOC, configDoc, NULL);
}
