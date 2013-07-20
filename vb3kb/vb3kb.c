/* Virtual Tiny Keyboard -- setBfree organ variant
 *
 * Copyright (c) 1997-2000 by Takashi Iwai
 * Copyright (C) 2008-2012 Robin Gareus <robin@gareus.org>
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
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <tk.h>

#ifndef Tk_Main
#define Tk_Main(argc, argv, proc) \
    Tk_MainEx(argc, argv, proc, Tcl_CreateInterp())
#endif

static int printversion(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]);
static int usage(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]);
static int seq_on(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]);
static int seq_off(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]);
static int seq_start_note(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]);
static int seq_stop_note(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]);
static int seq_control(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]);
static int seq_program(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]);
static int vkb_app_init(Tcl_Interp *interp);

/*
 * local common variables
 */
static void *private;
static int seq_opened = 0;
static vkb_oper_t *oper = NULL;

/* backend-drivers oper_*.c */
extern vkb_devinfo_t jack_devinfo;
#ifdef HAVE_ASEQ
extern vkb_devinfo_t alsa_devinfo;
const int vkb_num_devices=2;
#else
const int vkb_num_devices=1;
#endif


vkb_devinfo_t *vkb_device[] = {
  &jack_devinfo,
#ifdef HAVE_ASEQ
  &alsa_devinfo,
#endif
};

/*
 * main routine
 */

int main(int argc, char **argv) {
  char **nargv;
  int c, nargc;

  nargc = argc + 1;
  if ((nargv = (char**)malloc(sizeof(char*) * nargc)) == NULL) {
    fprintf(stderr, "vkeybd: can't malloc\n");
    exit(1);
  }

  char * tclfile=getenv("VB3KBTCL");

  nargv[0] = "-f";
  nargv[1] = tclfile?tclfile:VKB_TCLFILE;
  for (c = 1; c < argc; c++)
    nargv[c+1] = argv[c];

  /* call Tk main routine */
  Tk_Main(nargc, nargv, vkb_app_init);

  return 0;
}


/*
 * print usage
 */
static int usage(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]) {
  FILE *of = stderr;

  fprintf(of, "vb3kb -- virtual keyboard for the setBfree DSP tonewheel Organ\n\n");
  fprintf(of, "Usage: vb3kb [-options]\n\n");
  fprintf(of, "General options:\n");
  fprintf(of, "  --driver <driver>        specify MIDI driver (default = %s)\n", vkb_device[0]->name);

  int i;
  for (i = 0; i < vkb_num_devices; i++) {
    vkb_optarg_t *p;
    fprintf(of, "\n%s (--driver %s) Device options:\n", vkb_device[i]->desc, vkb_device[i]->name);
    if (! (p = vkb_device[i]->opts))
      continue;
    for (; p->name; p++)
      fprintf(of, "  %s\n%27s(default = %s)\n", p->desc, "", p->defval);
  }
  fprintf(of, "\nEnvironment:\n");
  fprintf(of, "VB3KBTCL specifies the path to the vb3kb.tcl file.");
  return TCL_OK;
}

static int printversion(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]) {
  const char *name ="vb3kb";
  FILE *of = stderr;

  fprintf (of, "%s %s\n\n", name, VERSION);
  fprintf(of,
    "Copyright (c) 2012 Robin Gareus\n"
    "Copyright (c) 1997-2000 Takashi Iwai\n"
    "\n"
    "This is free software; see the source for copying conditions.  There is NO\n"
    "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n"
    );
  return TCL_OK;
}


static int seq_on(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]) {
  int i;
  const char *var;
  if (seq_opened) return TCL_OK;

  if (! (var = Tcl_GetVar2(interp, "optvar", "driver", TCL_GLOBAL_ONLY))) {
    vkb_error(interp, "no output driver defined");
    return TCL_ERROR;
  }
  oper = NULL;
  for (i = 0; i < vkb_num_devices && vkb_device[i]; i++) {
    if (strcmp(vkb_device[i]->name, var) == 0) {
      oper = vkb_device[i]->oper;
      break;
  }
}

  if (!oper) {
    vkb_error(interp, "no output driver found");
    return TCL_ERROR;
  }

  if (oper->open(interp, &private)) {
    seq_opened = 1;
    Tcl_SetVar(interp, "seqswitch", "1", TCL_GLOBAL_ONLY);
  }

  return TCL_OK;
}

static int seq_off(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]) {
  if (seq_opened) {
    oper->close(interp, private);
    seq_opened = 0;
    Tcl_SetVar(interp, "seqswitch", "0", TCL_GLOBAL_ONLY);
  }
  return TCL_OK;
}

static int seq_start_note(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]) {
  int chan, note, vel;
  if (argc < 4)
    return TCL_ERROR;
  if (! seq_opened)
    return TCL_OK;
  chan = atoi(argv[1]);
  note = atoi(argv[2]);
  vel = atoi(argv[3]);
  if (oper->noteon)
    oper->noteon(interp, private, chan, note, vel);
  return TCL_OK;
}

static int seq_stop_note(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]) {
  int chan, note, vel;
  if (argc < 4)
    return TCL_ERROR;
  if (! seq_opened)
    return TCL_OK;
  chan = atoi(argv[1]);
  note = atoi(argv[2]);
  vel = atoi(argv[3]);
  if (oper->noteoff)
	  oper->noteoff(interp, private, chan, note, vel);
  return TCL_OK;
}

static int seq_control(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]) {
  int chan, type, val;
  if (argc < 4)
    return TCL_ERROR;
  if (! seq_opened)
    return TCL_OK;
  chan = atoi(argv[1]);
  type = atoi(argv[2]);
  val = atoi(argv[3]);
  if (oper->control)
    oper->control(interp, private, chan, type, val);
  return TCL_OK;
}

static int seq_program(ClientData clientData, Tcl_Interp *interp, int argc, const char *argv[]) {
  int chan, bank, preset;
  if (argc < 4)
    return TCL_ERROR;
  chan = atoi(argv[1]);
  bank = atoi(argv[2]);
  preset = atoi(argv[3]);
  if (! seq_opened)
    return TCL_OK;
  if (oper->program)
    oper->program(interp, private, chan, bank, preset-1);
  return TCL_OK;
}

/*
 * Misc. functions
 */

void vkb_error(Tcl_Interp *ip, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fputs("ERROR: ", stderr);
  vfprintf(stderr, fmt, ap);
  putc('\n', stderr);
  va_end(ap);
}

/*
 * Initialize Tcl/Tk components
 */
static int vkb_app_init(Tcl_Interp *interp) {
  if (Tcl_Init(interp) == TCL_ERROR)
    return TCL_ERROR;
  if (Tk_Init(interp) == TCL_ERROR)
    return TCL_ERROR;

  Tcl_CreateCommand(interp, "version", printversion,
      (ClientData)NULL, (Tcl_CmdDeleteProc*)NULL);
  Tcl_CreateCommand(interp, "usage", usage,
      (ClientData)NULL, (Tcl_CmdDeleteProc*)NULL);
  Tcl_CreateCommand(interp, "SeqOn", seq_on,
      (ClientData)NULL, (Tcl_CmdDeleteProc*)NULL);
  Tcl_CreateCommand(interp, "SeqOff", seq_off,
      (ClientData)NULL, (Tcl_CmdDeleteProc*)NULL);
  Tcl_CreateCommand(interp, "SeqStartNote", seq_start_note,
      (ClientData)NULL, (Tcl_CmdDeleteProc*)NULL);
  Tcl_CreateCommand(interp, "SeqStopNote", seq_stop_note,
      (ClientData)NULL, (Tcl_CmdDeleteProc*)NULL);
  Tcl_CreateCommand(interp, "SeqControl", seq_control,
      (ClientData)NULL, (Tcl_CmdDeleteProc*)NULL);
  Tcl_CreateCommand(interp, "SeqProgram", seq_program,
      (ClientData)NULL, (Tcl_CmdDeleteProc*)NULL);

  int i;
  for (i = 0; i < vkb_num_devices; i++) {
    vkb_optarg_t *p;
    if (! (p = vkb_device[i]->opts))
      continue;
    for (; p->name; p++) {
      Tcl_SetVar2(interp, "optvar", p->name, p->defval, TCL_GLOBAL_ONLY);
    }
  }

  Tcl_SetVar2(interp, "optvar", "driver", vkb_device[0]->name, TCL_GLOBAL_ONLY);
  return TCL_OK;
}
/* vi:set ts=8 sts=2 sw=2: */
