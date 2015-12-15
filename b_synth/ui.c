/* setBfree - LV2 GUI
 *
 * Copyright 2011-2012 David Robillard <d@drobilla.net>
 * Copyright (C) 2013-2015 Robin Gareus <robin@gareus.org>
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define ANIMSTEPS (35)

#ifdef ANIMSTEPS
#define ANDNOANIM && ui->openanim == 0
#define RESETANIM ui->openanim = 0;
#else
#define ANDNOANIM
#define RESETANIM
#endif

#define GL_GLEXT_PROTOTYPES

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <assert.h>

#include <locale.h>
#include "global_inst.h"

#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "pugl/pugl.h"


#ifdef XTERNAL_UI
#undef OLD_SUIL
#include "../robtk/gl/xternalui.h"
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <libgen.h>
#include "OpenGL/glu.h"
# ifdef JACK_DESCRIPT
#include <libproc.h>
# endif
#else
#include <GL/glu.h>
#endif

#ifdef _WIN32
#include <GL/glext.h>


static int wgl_discovered = -1;
static void (__stdcall *XWglGenerateMipmapEXT)(GLenum) = NULL;
static void (__stdcall *XWglBindBuffer)(GLenum, GLuint) = NULL;
static void (__stdcall *XWglGenBuffers)(GLsizei, GLuint*) = NULL;
static void (__stdcall *XWglBufferData)(GLenum, GLsizeiptr, const GLvoid*, GLenum) = NULL;

static int glext_func() {
  if (wgl_discovered != -1) return wgl_discovered;
  wgl_discovered = 0;
  XWglGenerateMipmapEXT = (__stdcall void (*)(GLenum)) wglGetProcAddress("glGenerateMipmapEXT");
  XWglBindBuffer = (__stdcall void (*)(GLenum, GLuint)) wglGetProcAddress("glBindBuffer");
  XWglGenBuffers = (__stdcall void (*)(GLsizei, GLuint*)) wglGetProcAddress("glGenBuffers");
  XWglBufferData = (__stdcall void (*)(GLenum, GLsizeiptr, const GLvoid*, GLenum)) wglGetProcAddress("glBufferData");

  if (!XWglGenerateMipmapEXT || !XWglBindBuffer || !XWglGenBuffers || !XWglBufferData) {
    wgl_discovered = 1;
  }
  return wgl_discovered;
}

static inline void MYglGenerateMipmapEXT(GLenum a) {
  XWglGenerateMipmapEXT(a);
}
static inline void MYglBindBuffer(GLenum a, GLuint b) {
  XWglBindBuffer(a,b);
}
static inline void MYglGenBuffers(GLsizei a, GLuint* b) {
  XWglGenBuffers(a,b);
}
static inline void MYglBufferData(GLenum a, GLsizeiptr b, const GLvoid *c, GLenum d) {
  XWglBufferData(a,b,c,d);
}

#define glGenerateMipmapEXT MYglGenerateMipmapEXT
#define glBindBuffer MYglBindBuffer
#define glGenBuffers MYglGenBuffers
#define glBufferData MYglBufferData

#endif

#include <FTGL/ftgl.h>
#ifdef __cplusplus
using namespace FTGL;
#define FTGL_RENDER_ALL RENDER_ALL
#endif

#ifdef BUILTINFONT
#  include "verabd.h"
#else
#  ifndef FONTFILE
#    define FONTFILE "/usr/share/fonts/truetype/ttf-bitstream-vera/VeraBd.ttf"
#  endif
#endif

#include "cfgParser.h" // ConfigDoc
#include "midi.h" // midi flags

extern const ConfigDoc *ampDoc ();
extern const ConfigDoc *reverbDoc ();
extern const ConfigDoc *whirlDoc ();
extern const ConfigDoc *midiDoc ();
extern const ConfigDoc *pgmDoc ();
extern const ConfigDoc *oscDoc ();
extern const ConfigDoc *scannerDoc ();

#include "uris.h"
#include "ui_model.h"

/* ui-model scale -- on screen we use [-1..+1] orthogonal projection */
#define SCALE (0.04f)

#define BTNLOC_OK   -.05, .05, .55, .70
#define BTNLOC_NO   -.75,-.65, .55, .70
#define BTNLOC_YES   .65, .75, .55, .70
#define BTNLOC_CANC  .75, .95, .80, .95
#define BTNLOC_SAVE  .45, .70, .80, .95
#define BTNLOC_DFLT  .05, .40, .80, .95
#define BTNLOC_RSRC  .30, .60, .78, .93
#define BTNLOC_CANC2 .68, .96, .73, .88
#define BTNLOC_CANC3 .68, .96, .78, .93
#define SCROLLBAR    -.8, .8, .625, .70

enum {
  HOVER_OK = 1,
  HOVER_NO = 2,
  HOVER_YES = 4,
  HOVER_SAVE = 8,
  HOVER_CANC = 16,
  HOVER_CANC2 = 32,
  HOVER_CANC3 = 64,
  HOVER_SCROLLBAR = 128,
  HOVER_DFLT = 256,
  HOVER_RSRC = 512
};

#define GPX(x) ((x) / 480.0)
#define GPY(y) ((y) / 160.0)

#define APX(x) (-1.0 + GPX(x))
#define APY(y) (-1.0 + GPY(y))

#define MENU_SAVEP APX(521.0), APX(521.0 + 133.0), APY(212.0), APY(212.0 + 78.0)
#define MENU_SAVEC APX(667.0), APX(667.0 + 113.0), APY(228.0), APY(228.0 + 66.0)

#define MENU_LOAD APX(32.0), APX(32.0 + 315.0), -1.0, APY(77.0)
#define MENU_PGML APX(694.0), APX(694.0 + 140.0), -1.0 , APY(197.0)
#define MENU_PGMS 1.0-GPX(122.0), 1.0, -1.0 , APY(195.0)

#define MENU_ACFG APX(164.0), APX(163.0 + 138.0), APY(120.0), APY(120.0 + 177.0)

#define MENU_CANC  .8, .98, .82, .95

enum {
  HOVER_MLOAD = 1,
  HOVER_MSAVEC = 2,
  HOVER_MSAVEP = 4,
  HOVER_MPGMS = 8,
  HOVER_MPGML = 16,
  HOVER_MACFG = 32,
  HOVER_MCANC = 64,
};

#define NOSCROLL -1000
#define SIGNUM(a) (a < 0 ? -1 : 1)
#define CTRLWIDTH2(ctrl) (SCALE * (ctrl).w / 2.0)
#define CTRLHEIGHT2(ctrl) (SCALE * (ctrl).h / 2.0)

#define MOUSEOVER(ctrl, mousex, mousey) \
  (   (mousex) >= (ctrl).x * SCALE - CTRLWIDTH2(ctrl) \
   && (mousex) <= (ctrl).x * SCALE + CTRLWIDTH2(ctrl) \
   && (mousey) >= (ctrl).y * SCALE - CTRLHEIGHT2(ctrl) \
   && (mousey) <= (ctrl).y * SCALE + CTRLHEIGHT2(ctrl) )


typedef enum {
  TA_LEFT_TOP,
  TA_LEFT_MIDDLE,
  TA_LEFT_BOTTOM,
  TA_RIGHT_TOP,
  TA_RIGHT_BOTTOM,
  TA_CENTER_TOP,
  TA_CENTER_MIDDLE,
  TA_CENTER_BOTTOM,
} B3TextAlign;

typedef enum {
  FS_LARGE,
  FS_MEDIUM,
  FS_SMALL
} B3FontSize;

static inline int MOUSEIN(
    const float X0, const float X1,
    const float Y0, const float Y1,
    const float mousex, const float mousey) {
  return (
      (mousex) >= (X0)
   && (mousex) <= (X1)
   && (mousey) >= (Y0)
   && (mousey) <= (Y1)
   );
}

/* total number of interactive objects */
#define TOTAL_OBJ (33)

/* names from src/midi.c -  mapped to object IDs */
static const char *obj_control[] = {
  "upper.drawbar16", // 0
  "upper.drawbar513",
  "upper.drawbar8",
  "upper.drawbar4",
  "upper.drawbar223",
  "upper.drawbar2",
  "upper.drawbar135",
  "upper.drawbar113",
  "upper.drawbar1", // 8

  "lower.drawbar16",
  "lower.drawbar513",
  "lower.drawbar8",
  "lower.drawbar4",
  "lower.drawbar223",
  "lower.drawbar2",
  "lower.drawbar135",
  "lower.drawbar113",
  "lower.drawbar1", // 17

  "pedal.drawbar16",
  "pedal.drawbar8",

  "percussion.enable",// 20
  "percussion.volume",
  "percussion.decay",
  "percussion.harmonic",
  "vibrato.lower",  // 24  SPECIAL -- lower
  "vibrato.upper",  // 25  SPECIAL -- upper
  "overdrive.enable", // 26
  "overdrive.character",
  "vibrato.knob", // 28
  "swellpedal1",
  "reverb.mix", // 30
  "rotary.speed-select", // SPECIAL leslie horn  // rotary.speed-select 2^3
  "rotary.speed-preset"  // SPECIAL leslie baffle
};

typedef struct {
  int type; // type ID from ui_model.h
  float min, max, cur;  // value range and current value
  float x,y; // matrix position
  float w,h; // bounding box
  int texID; // texture ID
  char midinfo[1024];
} b3widget;

typedef enum {
  CF_NUMBER,
  CF_INTEGER,
  CF_DECIBEL,
  CF_PERCENT,
  CF_DEGREE,
  CF_LISTLUT
} B3ConfigFormat;

typedef struct {
  float val;
  const char *label;
} b3scalepoint;

typedef struct {
  float cur;  // current value
  float dflt; // default value, parsed float from textual ConfigDoc, if applicable
  ConfigDoc const * d;
  const char *title;  // human readable short text
  B3ConfigFormat format;
  const b3scalepoint *lut;
} b3config;


#define MAXTAB 5
#define MAXCFG 120 // 24 * MAXTAB

typedef struct {
  LV2_Atom_Forge forge;

  LV2_URID_Map* map;
  setBfreeURIs  uris;

  LV2UI_Write_Function write;
  LV2UI_Controller     controller;

  PuglView*            view;
  int                  width;
  int                  height;
  int                  initialized;

#ifdef OLD_SUIL
  pthread_t            thread;
  int                  exit;
#endif

  /* OpenGL */
  GLuint * vbo;
  GLuint * vinx;
  GLuint texID[24]; // textures
  GLdouble matrix[16]; // used for mouse mapping
  double rot[3], off[3], scale; // global projection
#ifdef ANIMSTEPS
  int openanim;
  int animdirection;;
#endif

  /* displaymode
   * 0: main/organ
   * 1: help-screen
   * 2: MIDI PGM list - set/load
   * 3: MIDI PGM list - store/save
   * 4: File-index - load .cfg, load .pgm)
   * 5: File-index save cfg
   * 6: File-index save pgm
   * 7: /menu/
   * 8: /configedit/
   * 9: keyboard-help screen
   */
  int displaymode;
  int pgm_sel;
  int show_mm;
  int uiccbind;
  int uiccflag;
  int reinit;

  int textentry_active;
  int keyboard_control;
  char textentry_text[1024];
  char textentry_title[128];

  /* interactive control objexts */
  b3widget ctrls[TOTAL_OBJ];

  /* mouse drag status */
  int dndid;
  float dndval;
  float dndx, dndy;

  FTGLfont *font_big;
  FTGLfont *font_medium;
  FTGLfont *font_small;

  char *popupmsg;
  int   queuepopup;
  char *pendingdata;
  int   pendingmode;

  char midipgm[128][32];
  char mididsc[128][256];

  char *curdir;
  char **dirlist;
  int dirlistlen;
  int dir_sel;
  float dir_scroll;
  float dir_scrollgrab;
  int dir_hidedotfiles;

  int mouseover;
  int cfgtriover;
  int cfgdrag;
  int cfgdrag_x, cfgdrag_y;
  float dragval, dragmult;
  int cfgtab;

  b3config cfgvar[MAXCFG];

  int upper_key;
  int lower_key;
  int pedal_key;

  unsigned int active_keys [5]; // MAX_KEYS/32;
  bool highlight_keys;

  char lv2nfo [128];

#ifdef XTERNAL_UI
  struct lv2_external_ui_host *extui;
  struct lv2_external_ui xternal_ui;
  void (* ui_closed)(void* controller);
  bool close_ui; // used by xternalui
#endif

#ifdef JACK_DESCRIPT
  char * defaultConfigFile;
  char * defaultProgrammeFile;
  char * bundlePath;
#endif
} B3ui;


#define IS_FILEBROWSER(UI) (UI->displaymode == 4 || UI->displaymode == 5 || UI->displaymode == 6)


static int show_message(PuglView* view, const char *msg) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  if (ui->popupmsg) {
    fprintf(stderr, "B3Lv2UI: modal message overload\n");
    return -1;
  }
  if (ui->popupmsg) free (ui->popupmsg);
  ui->popupmsg = strdup(msg);
  ui->queuepopup = 1;
  puglPostRedisplay(view);
  return 0;
}

/******************************************************************************
 * file-name helper function
 */
#ifdef _WIN32
#define DIRSEP "\\"
#else
#define DIRSEP "/"
#endif

static void free_dirlist(B3ui* ui) {
  int i;
  if (!ui->dirlist) return;
  for (i=0; i < ui->dirlistlen; ++i) {
    free(ui->dirlist[i]);
  }
  free(ui->dirlist);
  ui->dirlistlen = 0;
  ui->dirlist = NULL;
  ui->dir_scroll = 0;
}

static char * absfilepath(const char *dir, const char *file) {
  if (!dir || !file) return NULL;
  char *fn = (char*) malloc((strlen(dir) + strlen(file) + 2)*sizeof(char));
  strcpy(fn, dir);
  strcat(fn, DIRSEP);
  strcat(fn, file);
#ifdef _WIN32
  char buf[PATH_MAX];
  char * rfn = _fullpath(buf, fn, PATH_MAX);
  if (rfn) {
    free(fn);
    return strdup(buf);
  }
#else
  char * rfn = realpath(fn, NULL);
  if (rfn) {
    free(fn);
    return rfn;
  }
#endif
  else {
    return fn;
  }
}

static int check_extension(const char *fn, const char *ext) {
  if (!fn || !ext) return -1;
  const int fnl = strlen(fn);
  const int exl = strlen(ext);
  if (fnl > exl && !strcmp(&fn[fnl-exl], ext)) {
    return 0; // OK
  }
  return -1;
}

static int cmpstringp(const void *p1, const void *p2) {
  return strcmp(* (char * const *) p1, * (char * const *) p2);
}

static int dirlist(PuglView* view, const char *dir) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  DIR  *D;
  struct dirent *dd;
  char **filelist = NULL;
  int filelistlen = 0;

  free_dirlist(ui);

  if (!(D = opendir (dir)))  {
    ui->dir_sel = -1;
    free(ui->curdir);
#ifdef _WIN32
    ui->curdir = strdup("C:\\");
#else
    ui->curdir = strdup("/");
#endif
    // XXX re-try?, print error/close...
    return -1;
  }

  while ((dd = readdir (D))) {
    struct stat fs;
    char * rfn = absfilepath(dir, dd->d_name);
    if (!rfn) continue;
    if(stat(rfn, &fs)) {
      free(rfn);
      continue;
    }

    if (S_ISREG(fs.st_mode)) {
      int fnl = strlen(rfn);
      if (fnl <= 4) {
	free(rfn);
	continue;
      }
      if ((strcmp(&rfn[fnl-4], ".pgm") && strcmp(&rfn[fnl-4], ".cfg"))
	  || (ui->dir_hidedotfiles && dd->d_name[0] == '.')
	 ) {
	free(rfn);
	continue;
      }
      filelist = (char**) realloc(filelist, (filelistlen+1) * sizeof(char*));
      filelist[filelistlen] = (char*) malloc(1024*sizeof(char));
      strncpy(filelist[filelistlen], dd->d_name, 1024);
      filelistlen++;
      free(rfn);
      continue;
    }
    free(rfn);

    const int delen = strlen(dd->d_name);
    if (delen == 1 && dd->d_name[0] == '.') continue; // '.'
    else if (delen == 2 && dd->d_name[0] == '.' && dd->d_name[1] == '.') ; // '..' -> OK
    else if (ui->dir_hidedotfiles && dd->d_name[0] == '.') continue;

    ui->dirlist = (char**) realloc(ui->dirlist, (ui->dirlistlen+1) * sizeof(char*));
    ui->dirlist[ui->dirlistlen] = (char*) malloc(1024*sizeof(char));
    strncpy(ui->dirlist[ui->dirlistlen], dd->d_name, 1022);
    ui->dirlist[ui->dirlistlen][1022] = '\0';
    strcat(ui->dirlist[ui->dirlistlen], DIRSEP);
    ui->dirlistlen++;
  }

  if (ui->dirlistlen > 0) {
    qsort(ui->dirlist, ui->dirlistlen, sizeof(ui->dirlist[0]), cmpstringp);
  }
  if (filelistlen > 0) {
    qsort(filelist, filelistlen, sizeof(filelist[0]), cmpstringp);
  }

  if (ui->dirlistlen + filelistlen == 0) {
    free(filelist);
    free_dirlist(ui);
    ui->dirlist = NULL;
    ui->dir_sel = -1;
    return -1;
  }

  ui->dirlist = (char**) realloc(ui->dirlist, (ui->dirlistlen + filelistlen) * sizeof(char*));
  int i;
  for (i = 0; i < filelistlen; i++) {
    ui->dirlist[ui->dirlistlen + i] = filelist[i];
  }
  ui->dirlistlen += filelistlen;
  free(filelist);
  if (ui->dir_sel >= ui->dirlistlen) {
	  ui->dir_sel = -1;
  }
  return 0;
}

/******************************************************************************
 * Value mapping, MIDI <> internal min/max <> mouse
 */

static void vmap_midi_to_val(PuglView* view, int elem, int mval) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  ui->ctrls[elem].cur = ui->ctrls[elem].min + rint((ui->ctrls[elem].max - ui->ctrls[elem].min) * mval / 127.0);
}

static unsigned char vmap_val_to_midi(PuglView* view, int elem) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  const int v = rint( rint(ui->ctrls[elem].cur - ui->ctrls[elem].min) * 127.0 / (ui->ctrls[elem].max - ui->ctrls[elem].min));
  return (v&0x7f);
}

/* call lv2 plugin if value has changed */
#define OBJ_BUF_SIZE 256

static void b3_forge_message(B3ui* ui, const char *key, int32_t val) {
  uint8_t obj_buf[OBJ_BUF_SIZE];
  lv2_atom_forge_set_buffer(&ui->forge, obj_buf, OBJ_BUF_SIZE);
  LV2_Atom* msg = forge_kvcontrolmessage(&ui->forge, &ui->uris, key, val);
  if (msg)
    ui->write(ui->controller, 0, lv2_atom_total_size(msg), ui->uris.atom_eventTransfer, msg);
}

static void notifyPlugin(PuglView* view, int elem) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  int32_t val;

  /* special cases */
  if (elem == 24 || elem == 25) {
    // two in one
    val = ((ui->ctrls[24].cur ? 1 : 0) | (ui->ctrls[25].cur ? 2 : 0) ) << 5;
  } else if (elem == 31 || elem == 32) {
    // map: tremolo/fast 2 << off:1 >> chorale/slow:0  ->  off:0, slow:1, fast:2
    int hr = rint(ui->ctrls[32].cur);
    int bf = rint(ui->ctrls[31].cur);
    if (hr != 2) hr = (hr == 1) ? 0 : 1;
    if (bf != 2) bf = (bf == 1) ? 0 : 1;
    val = bf * 15 + hr * 45;
    elem = 31; //  force to use  2^3 rotary.speed-select
  } else {
    // default MIDI-CC range 0..127
    val = vmap_val_to_midi(view, elem);
  }

  b3_forge_message(ui, obj_control[elem], val);
}

static void forge_message_kv(B3ui* ui, LV2_URID uri, int key, const char *val) {
  uint8_t obj_buf[256];
  if (!val || strlen(val) > 32) { return; }

  lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 256);
  LV2_Atom_Forge_Frame set_frame;
  LV2_Atom* msg = (LV2_Atom*)x_forge_object(&ui->forge, &set_frame, 1, uri);
  lv2_atom_forge_property_head(&ui->forge, ui->uris.sb3_cckey, 0);
  lv2_atom_forge_int(&ui->forge, key);
  lv2_atom_forge_property_head(&ui->forge, ui->uris.sb3_ccval, 0);
  lv2_atom_forge_string(&ui->forge, val, strlen(val));
  lv2_atom_forge_pop(&ui->forge, &set_frame);
  if (msg)
    ui->write(ui->controller, 0, lv2_atom_total_size(msg), ui->uris.atom_eventTransfer, msg);
}

// TODO consolidate with uris.h
static void forge_message_str(B3ui* ui, LV2_URID uri, const char *key) {
  uint8_t obj_buf[1024];
  lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 1024);

  LV2_Atom_Forge_Frame set_frame;
  LV2_Atom* msg = (LV2_Atom*)x_forge_object(&ui->forge, &set_frame, 1, uri);
  if (key) {
    lv2_atom_forge_property_head(&ui->forge, ui->uris.sb3_cckey, 0);
    lv2_atom_forge_string(&ui->forge, key, strlen(key));
  }
  lv2_atom_forge_pop(&ui->forge, &set_frame);
  ui->write(ui->controller, 0, lv2_atom_total_size(msg), ui->uris.atom_eventTransfer, msg);
}

static void forge_message_int(B3ui* ui, LV2_URID uri, const int val) {
  uint8_t obj_buf[64];
  lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 64);

  LV2_Atom_Forge_Frame set_frame;
  LV2_Atom* msg = (LV2_Atom*)x_forge_object(&ui->forge, &set_frame, 1, uri);
  lv2_atom_forge_property_head(&ui->forge, ui->uris.sb3_cckey, 0);
  lv2_atom_forge_int(&ui->forge, val);
  lv2_atom_forge_pop(&ui->forge, &set_frame);
  ui->write(ui->controller, 0, lv2_atom_total_size(msg), ui->uris.atom_eventTransfer, msg);
}

static void forge_note(B3ui* ui, const int manual, const int note, const bool onoff) {
  uint8_t obj_buf[16];
  lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 16);

  // TODO translate manual to channel (IFF user re-assigned channels,..)
  uint8_t buffer[3];
  buffer[0] = (onoff ? 0x90 : 0x80) | (manual & 0xf);
  buffer[1] = note & 0x7f;
  buffer[2] = (onoff ? 0x7f : 0x00);

  // mmh -> use uris.h  forge_midimessage() ??
  LV2_Atom midiatom;
  midiatom.type = ui->uris.midi_MidiEvent;
  midiatom.size = 3;

  lv2_atom_forge_raw(&ui->forge, &midiatom, sizeof(LV2_Atom));
  lv2_atom_forge_raw(&ui->forge, buffer, 3);
  lv2_atom_forge_pad(&ui->forge, sizeof(LV2_Atom) + 3);
  ui->write(ui->controller, 0, lv2_atom_total_size(&midiatom), ui->uris.atom_eventTransfer, obj_buf);
}

/* called from port_event -- plugin tells GUI a new value */
static void processCCevent(B3ui* ui, const char *k, int v) {
  int i;
  /* special cases */
  if (!strcmp("vibrato.routing", k)) {
    ui->ctrls[24].cur = ((v>>5) & 1 ) ? 1 : 0;
    ui->ctrls[25].cur = ((v>>5) & 2 ) ? 1 : 0;
    puglPostRedisplay(ui->view);
    return;
  } else
  if (!strcmp("rotary.speed-select", k)) {
    // see setRevOption() -- value 0..8
    // map: off:0, slow:1, fast:2  ->  tremolo/fast 2 << off:1 >> chorale/slow:0
    int hr = (v / 45) % 3; // horn 0:off, 1:chorale  2:tremolo
    int bf = (v / 15) % 3; // drum 0:off, 1:chorale  2:tremolo
    if (hr != 2) hr = (hr == 1) ? 0 : 1;
    if (bf != 2) bf = (bf == 1) ? 0 : 1;
    ui->ctrls[32].cur = hr; // horn 0:chorale, 1:off, 2:tremolo
    ui->ctrls[31].cur = bf; // drum 0:chorale, 1:off, 2:tremolo
    puglPostRedisplay(ui->view);
    return;
  }
  if (!strcmp("rotary.speed-preset", k)) {
    return;
  }

  for (i = 0; i < TOTAL_OBJ; ++i) {
    if (!strcmp(obj_control[i], k)) {
      /* override drags/modifications of current object */
      if (ui->dndid == i) {
	ui->dndid = -1;
      }
      vmap_midi_to_val(ui->view, i, v);
      puglPostRedisplay(ui->view);
      break;
    }
  }
}


/* process mouse motion, update value */
static void processMotion(PuglView* view, int elem, float dx, float dy) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  if (elem < 0 || elem >= TOTAL_OBJ) return;

  float dist = ui->ctrls[elem].type == OBJ_LEVER ? (-5 * dx) : (dx - dy);
  const unsigned char oldval = vmap_val_to_midi(view, elem);

  switch (ui->ctrls[elem].type) {
    case OBJ_DIAL:
      ui->ctrls[elem].cur = ui->dndval + dist * (ui->ctrls[elem].max - ui->ctrls[elem].min);
      if (ui->ctrls[elem].max == 0) {
	assert(ui->ctrls[elem].min < 0);
	if (ui->ctrls[elem].cur > ui->ctrls[elem].max || ui->ctrls[elem].cur < ui->ctrls[elem].min) {
	  const float r = 1 - ui->ctrls[elem].min;
	  ui->ctrls[elem].cur -= ceil(ui->ctrls[elem].cur / r) * r;
	}
      } else {
	if (ui->ctrls[elem].cur > ui->ctrls[elem].max) ui->ctrls[elem].cur = ui->ctrls[elem].max;
	if (ui->ctrls[elem].cur < ui->ctrls[elem].min) ui->ctrls[elem].cur = ui->ctrls[elem].min;
      }
      break;
    case OBJ_LEVER:
    case OBJ_DRAWBAR:
      ui->ctrls[elem].cur = ui->dndval + dist * 2.5 * (ui->ctrls[elem].max - ui->ctrls[elem].min);
      if (ui->ctrls[elem].cur > ui->ctrls[elem].max) ui->ctrls[elem].cur = ui->ctrls[elem].max;
      if (ui->ctrls[elem].cur < ui->ctrls[elem].min) ui->ctrls[elem].cur = ui->ctrls[elem].min;
      break;
    default:
      break;
  }

  if (vmap_val_to_midi(view, elem) != oldval) {
    puglPostRedisplay(view);
    notifyPlugin(view, elem);
  }
}


/******************************************************************************
 * 3D projection
 */

/* invert projection matrix -- code from GLU */
static bool invertMatrix(const double m[16], double invOut[16]) {
  double inv[16], det;
  int i;

  inv[0] = m[5]  * m[10] * m[15] -
	   m[5]  * m[11] * m[14] -
	   m[9]  * m[6]  * m[15] +
	   m[9]  * m[7]  * m[14] +
	   m[13] * m[6]  * m[11] -
	   m[13] * m[7]  * m[10];

  inv[4] = -m[4]  * m[10] * m[15] +
	    m[4]  * m[11] * m[14] +
	    m[8]  * m[6]  * m[15] -
	    m[8]  * m[7]  * m[14] -
	    m[12] * m[6]  * m[11] +
	    m[12] * m[7]  * m[10];

  inv[8] = m[4]  * m[9]  * m[15] -
	   m[4]  * m[11] * m[13] -
	   m[8]  * m[5]  * m[15] +
	   m[8]  * m[7]  * m[13] +
	   m[12] * m[5]  * m[11] -
	   m[12] * m[7]  * m[9];

  inv[12] = -m[4]  * m[9]  * m[14] +
	     m[4]  * m[10] * m[13] +
	     m[8]  * m[5]  * m[14] -
	     m[8]  * m[6]  * m[13] -
	     m[12] * m[5]  * m[10] +
	     m[12] * m[6]  * m[9];

  inv[1] = -m[1]  * m[10] * m[15] +
	    m[1]  * m[11] * m[14] +
	    m[9]  * m[2]  * m[15] -
	    m[9]  * m[3]  * m[14] -
	    m[13] * m[2]  * m[11] +
	    m[13] * m[3]  * m[10];

  inv[5] = m[0]  * m[10] * m[15] -
	   m[0]  * m[11] * m[14] -
	   m[8]  * m[2]  * m[15] +
	   m[8]  * m[3]  * m[14] +
	   m[12] * m[2]  * m[11] -
	   m[12] * m[3]  * m[10];

  inv[9] = -m[0]  * m[9]  * m[15] +
	    m[0]  * m[11] * m[13] +
	    m[8]  * m[1]  * m[15] -
	    m[8]  * m[3]  * m[13] -
	    m[12] * m[1]  * m[11] +
	    m[12] * m[3]  * m[9];

  inv[13] = m[0]  * m[9]  * m[14] -
	    m[0]  * m[10] * m[13] -
	    m[8]  * m[1]  * m[14] +
	    m[8]  * m[2]  * m[13] +
	    m[12] * m[1]  * m[10] -
	    m[12] * m[2]  * m[9];

  inv[2] = m[1]  * m[6] * m[15] -
	   m[1]  * m[7] * m[14] -
	   m[5]  * m[2] * m[15] +
	   m[5]  * m[3] * m[14] +
	   m[13] * m[2] * m[7] -
	   m[13] * m[3] * m[6];

  inv[6] = -m[0]  * m[6] * m[15] +
	    m[0]  * m[7] * m[14] +
	    m[4]  * m[2] * m[15] -
	    m[4]  * m[3] * m[14] -
	    m[12] * m[2] * m[7] +
	    m[12] * m[3] * m[6];

  inv[10] = m[0]  * m[5] * m[15] -
	    m[0]  * m[7] * m[13] -
	    m[4]  * m[1] * m[15] +
	    m[4]  * m[3] * m[13] +
	    m[12] * m[1] * m[7] -
	    m[12] * m[3] * m[5];

  inv[14] = -m[0]  * m[5] * m[14] +
	     m[0]  * m[6] * m[13] +
	     m[4]  * m[1] * m[14] -
	     m[4]  * m[2] * m[13] -
	     m[12] * m[1] * m[6] +
	     m[12] * m[2] * m[5];

  inv[3] = -m[1] * m[6] * m[11] +
	    m[1] * m[7] * m[10] +
	    m[5] * m[2] * m[11] -
	    m[5] * m[3] * m[10] -
	    m[9] * m[2] * m[7] +
	    m[9] * m[3] * m[6];

  inv[7] = m[0] * m[6] * m[11] -
	   m[0] * m[7] * m[10] -
	   m[4] * m[2] * m[11] +
	   m[4] * m[3] * m[10] +
	   m[8] * m[2] * m[7] -
	   m[8] * m[3] * m[6];

  inv[11] = -m[0] * m[5] * m[11] +
	     m[0] * m[7] * m[9] +
	     m[4] * m[1] * m[11] -
	     m[4] * m[3] * m[9] -
	     m[8] * m[1] * m[7] +
	     m[8] * m[3] * m[5];

  inv[15] = m[0] * m[5] * m[10] -
	    m[0] * m[6] * m[9] -
	    m[4] * m[1] * m[10] +
	    m[4] * m[2] * m[9] +
	    m[8] * m[1] * m[6] -
	    m[8] * m[2] * m[5];

  det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

  if (det == 0) return false;

  det = 1.0 / det;

  for (i = 0; i < 16; i++)
    invOut[i] = inv[i] * det;

  return true;
}

#ifdef DEBUG_ROTATION_MATRIX
static void print4x4(GLdouble *m) {
  fprintf(stderr,
      "%+0.3lf %+0.3lf %+0.3lf %+0.3lf\n"
      "%+0.3lf %+0.3lf %+0.3lf %+0.3lf\n"
      "%+0.3lf %+0.3lf %+0.3lf %+0.3lf\n"
      "%+0.3lf %+0.3lf %+0.3lf %+0.3lf;\n\n"
      , m[0] , m[1] , m[2] , m[3]
      , m[4] , m[5] , m[6] , m[7]
      , m[8] , m[9] , m[10] , m[11]
      , m[12] , m[13] , m[14] , m[15]
      );
}
#endif

/* apply reverse projection to mouse-pointer, project Z-axis to screen. */
static void project_mouse(PuglView* view, int mx, int my, float zo, float *x, float *y) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  const double fx =  2.0 * (float)mx / ui->width  - 1.0;
  const double fy = -2.0 * (float)my / ui->height + 1.0;
  const double fz = - ui->matrix[14] -(fx * ui->matrix[2] + fy * ui->matrix[6] - SCALE * zo) / ui->matrix[10];

  *x = fx * ui->matrix[0] + fy * ui->matrix[4] + fz * ui->matrix[8] + ui->matrix[12];
  *y = fx * ui->matrix[1] + fy * ui->matrix[5] + fz * ui->matrix[9] + ui->matrix[13];
}


/******************************************************************************
 * 3D model loading
 * see http://ksolek.fm.interia.pl/Blender/
 */

static void initMesh(PuglView* view) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  int i;

  glGenBuffers(OBJECTS_COUNT, ui->vbo);

  for (i = 0; i < OBJECTS_COUNT; i++) {
    glBindBuffer(GL_ARRAY_BUFFER, ui->vbo[i]);
    glBufferData(GL_ARRAY_BUFFER, sizeof (struct vertex_struct) * vertex_count[i], &vertices[vertex_offset_table[i]], GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  glGenBuffers(OBJECTS_COUNT, ui->vinx);
  for (i = 0; i < OBJECTS_COUNT; i++) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui->vinx[i]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof (indexes[0]) * faces_count[i] * 3, &indexes[indices_offset_table[i]], GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  }
}

#define BUFFER_OFFSET(x)((char *)NULL+(x))

static void drawMesh(PuglView* view, unsigned int index, int apply_transformations) {
  B3ui* ui = (B3ui*)puglGetHandle(view);

  if (apply_transformations) {
    glPushMatrix();
    glMultMatrixf(transformations[index]);
  }

  glBindBuffer(GL_ARRAY_BUFFER, ui->vbo[index]);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui->vinx[index]);

  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(3, GL_FLOAT, sizeof (struct vertex_struct), BUFFER_OFFSET(0));

  glEnableClientState(GL_NORMAL_ARRAY);
  glNormalPointer(GL_FLOAT, sizeof (struct vertex_struct), BUFFER_OFFSET(3 * sizeof (float)));

  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glTexCoordPointer(2, GL_FLOAT, sizeof (struct vertex_struct), BUFFER_OFFSET(6 * sizeof (float)));

  glDrawElements(GL_TRIANGLES, faces_count[index] * 3, INX_TYPE, BUFFER_OFFSET(0));

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisableClientState(GL_NORMAL_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);

  if (apply_transformations) {
    glPopMatrix();
  }
}

/******************************************************************************
 * OpenGL textures
 */

#include "textures/wood.c"
#include "textures/dial.c"
#include "textures/drawbar.c"

#include "textures/btn_vibl.c"
#include "textures/btn_vibu.c"
#include "textures/btn_perc.c"
#include "textures/btn_perc_decay.c"
#include "textures/btn_perc_harmonic.c"
#include "textures/btn_perc_volume.c"
#include "textures/btn_overdrive.c"

#include "textures/bg_right_ctrl.c"
#include "textures/bg_left_ctrl.c"
#include "textures/bg_leslie_drum.c"
#include "textures/bg_leslie_horn.c"

#include "textures/help_screen_image.c"

#include "textures/ui_button_image.c"
#include "textures/ui_proc_image.c"

#include "textures/uim_background.c"
#include "textures/uim_cable1.c"
#include "textures/uim_cable2.c"
#include "textures/uim_caps.c"
#include "textures/uim_tube1.c"
#include "textures/uim_tube2.c"
#include "textures/uim_transformer.c"

#define CIMAGE(ID, VARNAME) \
  glGenTextures(1, &ui->texID[ID]); \
  glBindTexture(GL_TEXTURE_2D, ui->texID[ID]); \
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); \
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); \
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE); \
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); \
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); \
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VARNAME.width, VARNAME.height, 0, \
      (VARNAME.bytes_per_pixel == 3 ? GL_RGB : GL_RGBA), \
      GL_UNSIGNED_BYTE, VARNAME.pixel_data); \
  if (atihack) { \
    glEnable(GL_TEXTURE_2D); \
    glGenerateMipmapEXT(GL_TEXTURE_2D); \
    glDisable(GL_TEXTURE_2D); \
  } else { \
    glGenerateMipmapEXT(GL_TEXTURE_2D); \
  }


static void initTextures(PuglView* view) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  const int atihack = 1; // TODO detect card

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  glGenTextures(1, &ui->texID[0]);
  glBindTexture(GL_TEXTURE_2D, ui->texID[0]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, wood_image.width, wood_image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, wood_image.pixel_data);
  if (atihack) {
    glEnable(GL_TEXTURE_2D);
    glGenerateMipmapEXT(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_2D);
  } else {
    glGenerateMipmapEXT(GL_TEXTURE_2D);
  }

  CIMAGE(1, drawbar_image);
  CIMAGE(2, dial_image);

  CIMAGE(3, btn_vibl_image);
  CIMAGE(4, btn_vibu_image);
  CIMAGE(5, btn_overdrive_image);

  CIMAGE(6, btn_perc_image);
  CIMAGE(7, btn_perc_vol_image);
  CIMAGE(8, btn_perc_decay_image);
  CIMAGE(9, btn_perc_harm_image);

  CIMAGE(10, bg_right_ctrl_image);
  CIMAGE(11, bg_left_ctrl_image);
  CIMAGE(12, bg_leslie_drum_image);
  CIMAGE(13, bg_leslie_horn_image);
  CIMAGE(14, help_screen_image);

  CIMAGE(15, ui_button_image);
  CIMAGE(16, ui_proc_image);

  CIMAGE(17, uim_background_image);
  CIMAGE(18, uim_cable1_image);
  CIMAGE(19, uim_cable2_image);
  CIMAGE(20, uim_caps_image);
  CIMAGE(21, uim_tube1_image);
  CIMAGE(22, uim_tube2_image);
  CIMAGE(23, uim_transformer_image);
}


/******************************************************************************
 * OpenGL settings
 */

static void setupOpenGL() {
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glFrontFace(GL_CCW);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DITHER);
  glEnable(GL_MULTISAMPLE);
  glEnable(GL_RESCALE_NORMAL);

  glDisable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_SRC_ALPHA_SATURATE);

  glEnable(GL_POLYGON_SMOOTH);
  glEnable (GL_LINE_SMOOTH);
  glShadeModel(GL_SMOOTH);
#if 0 // OpenGL 3.1
  glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
#endif

  glEnable(GL_MULTISAMPLE_ARB);

  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
  glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
  glHint(GL_FOG_HINT, GL_NICEST);

  //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // test & debug
}

static void setupLight() {
  const GLfloat light0_ambient[]  = { 0.2, 0.15, 0.1, 1.0 };
  const GLfloat light0_diffuse[]  = { 1.0, 1.0, 1.0, 1.0 };
  const GLfloat light0_specular[] = { 0.4, 0.4, 0.5, 1.0 };
  const GLfloat light0_position[] = {  3.0,  2.5, -10.0, 0 };
  const GLfloat spot_direction[]  = { -2.5, -2.5,  9.0 };

  glLightfv(GL_LIGHT0, GL_AMBIENT, light0_ambient);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, light0_diffuse);
  glLightfv(GL_LIGHT0, GL_SPECULAR, light0_specular);
  glLightfv(GL_LIGHT0, GL_POSITION, light0_position);
  glLightf(GL_LIGHT0,  GL_SPOT_CUTOFF, 10.0f);
  glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, spot_direction);
#if 0
  glLightf(GL_LIGHT0,  GL_SPOT_EXPONENT, 120.0);
  glLightf(GL_LIGHT0,  GL_CONSTANT_ATTENUATION, 1.5);
  glLightf(GL_LIGHT0,  GL_LINEAR_ATTENUATION, 0.5);
  glLightf(GL_LIGHT0,  GL_QUADRATIC_ATTENUATION, 0.2);

  const GLfloat global_ambient[]  = { 0.2, 0.2, 0.2, 1.0 };
  glLightModelfv(GL_LIGHT_MODEL_AMBIENT, global_ambient);
  glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
  glEnable(GL_COLOR_MATERIAL);
#endif

  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
}

/******************************************************************************
 * puGL callbacks
 */

static void
onReshape(PuglView* view, int width, int height)
{
  B3ui* ui = (B3ui*)puglGetHandle(view);
  const float invaspect = (float) height / (float) width;
  ui->width = width; ui->height = height;

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(-1.0, 1.0, invaspect, -invaspect, 5.0, -5.0);

  if (ui->displaymode || ui->textentry_active || ui->popupmsg) {
    glViewport(0, 0, width, height);
    glMatrixMode(GL_MODELVIEW);
    return;
  }

#ifdef ANIMSTEPS
  if (ui->openanim > 0) {
    float rot[3];
    float off[3];
    const float scale = ui->openanim * (1.65 - ui->scale) / (float)ANIMSTEPS;

    rot[0] = ui->openanim * (  0.0 - ui->rot[0]) / (float)ANIMSTEPS;
    rot[1] = ui->openanim * (-90.0 - ui->rot[1]) / (float)ANIMSTEPS;
    if (ui->rot[2] > 0)
      rot[2] = ui->openanim * (180.0 - ui->rot[2]) / (float)ANIMSTEPS;
    else
      rot[2] = ui->openanim * (-180.0 - ui->rot[2]) / (float)ANIMSTEPS;

    off[0] = ui->openanim * (  0.0  - ui->off[0]) / (float)ANIMSTEPS;
    off[1] = ui->openanim * (  0.0  - ui->off[1]) / (float)ANIMSTEPS;
    off[2] = ui->openanim * ( -0.18 - ui->off[2]) / (float)ANIMSTEPS;

    glRotatef(ui->rot[0] + rot[0], 0, 1, 0);
    glRotatef(ui->rot[1] + rot[1], 1, 0, 0);
    glRotatef(ui->rot[2] + rot[2], 0, 0, 1);
    glScalef(ui->scale + scale, ui->scale + scale, ui->scale + scale);
    glTranslatef(ui->off[0] + off[0], ui->off[1] + off[1], ui->off[2] + off[2]);

  } else
#endif
  {
    glRotatef(ui->rot[0], 0, 1, 0);
    glRotatef(ui->rot[1], 1, 0, 0);
    glRotatef(ui->rot[2], 0, 0, 1);
    glScalef(ui->scale, ui->scale, ui->scale);
    glTranslatef(ui->off[0], ui->off[1], ui->off[2]);
  }

  GLdouble matrix[16];
  glGetDoublev(GL_PROJECTION_MATRIX, matrix);
  invertMatrix(matrix, ui->matrix);

#ifdef DEBUG_ROTATION_MATRIX
  print4x4(matrix);
  print4x4(ui->matrix);
#endif

  glViewport(0, 0, width, height);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

static void
render_gl_text(PuglView* view, const char *text, float x, float y, float z, const GLfloat color[4], B3TextAlign align, B3FontSize fs, int blend)
{
  B3ui* ui = (B3ui*)puglGetHandle(view);
  const GLfloat mat_b[] = {0.0, 0.0, 0.0, 1.0};
  float bb[6];
  FTGLfont * font;

  switch (fs) {
    case FS_LARGE:
      font = ui->font_big;
      break;
    case FS_MEDIUM:
      font = ui->font_medium;
      break;
    default:
      font = ui->font_small;
      break;
  }

  glPushMatrix();
  glLoadIdentity();

  if (blend) {
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_b);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_b);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, color);
    glEnable(GL_BLEND);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
  } else {
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, color);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, color);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, color);
  }

  glScalef(0.001,0.001,1.00);
  glRotatef(180, 1, 0, 0);

  ftglGetFontBBox(font, text, -1, bb);
#if 0
  printf("%.2f %.2f %.2f  %.2f %.2f %.2f\n",
      bb[0], bb[1], bb[2], bb[3], bb[4], bb[5]);
#endif
  switch(align) {
    case TA_CENTER_TOP:
      glTranslatef(
	  (bb[3] - bb[0])/-2.0,
	  (bb[4] - bb[1])/-1.0,
	  0);
      break;
    case TA_CENTER_MIDDLE:
      glTranslatef(
	  (bb[3] - bb[0])/-2.0,
	  (bb[4] - bb[1])/-2.0,
	  0);
      break;
    case TA_RIGHT_TOP:
      glTranslatef(
	  (bb[3] - bb[0])/-1.0,
	  (bb[4] - bb[1])/-1.0,
	  0);
      break;
    case TA_LEFT_BOTTOM:
      break;
    case TA_RIGHT_BOTTOM:
      glTranslatef(
	  (bb[3] - bb[0])/-1.0,
	  0,
	  0);
      break;
    case TA_CENTER_BOTTOM:
      glTranslatef(
	  (bb[3] - bb[0])/-2.0,
	  0,
	  0);
      break;
    case TA_LEFT_MIDDLE:
      glTranslatef(
	  0,
	  (bb[4] - bb[1])/-2.0,
	  0);
      break;
    case TA_LEFT_TOP:
      glTranslatef(
	  0,
	  (bb[4] - bb[1])/-1.0,
	  0);
      break;
  }
  glTranslatef(x * (1000.0*SCALE) , -y * (1000.0*SCALE), z * SCALE);
  ftglRenderFont(font, text, FTGL_RENDER_ALL);
  glPopMatrix();
  if (blend) {
    glDisable(GL_BLEND);
  }
}

static void
render_title(PuglView* view, const char *text, float x, float y, float z, const GLfloat color[4], B3TextAlign align)
{
  render_gl_text(view, text, x, y, z / SCALE, color, align, FS_LARGE, 0);
}

static void
render_small_text(PuglView* view, const char *text, float x, float y, float z, const GLfloat color[4], B3TextAlign align)
{
  render_gl_text(view, text, x, y, z, color, align, FS_SMALL, 1);
}

static void
render_text(PuglView* view, const char *text, float x, float y, float z, B3TextAlign align)
{
  const GLfloat mat[] = {0.1, 0.95, 0.15, 1.0};
  render_small_text(view, text, x, y, z, mat, align);
}

static void
unity_box(PuglView* view,
    const float x0, const float x1,
    const float y0, const float y1,
    const GLfloat color[4])
{
  assert(x0 < x1);
  assert(y0 < y1);
  const float invaspect = 320. / 960.;
  glPushMatrix();
  glLoadIdentity();
  glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, color);
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, color);
  glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, color);
  glBegin(GL_QUADS);
  glVertex3f(x0, y0 * invaspect, .1);
  glVertex3f(x0, y1 * invaspect, .1);
  glVertex3f(x1, y1 * invaspect, .1);
  glVertex3f(x1, y0 * invaspect, .1);
  glEnd();
  glPopMatrix();
}

static void
unity_button_color(PuglView* view,
    const float x0, const float x1,
    const float y0, const float y1,
    GLfloat btncol[3]
    )
{
  B3ui* ui = (B3ui*)puglGetHandle(view);
  const float invaspect = 320. / 960.;
  float x0A, x0B;

  /* button texture: x/3 (left-circle + x/3 (box) + x/3 (right-circle) ;; texture aspect: 4/3
   * ->  x/3 == (y * 4/3) / 3
   */
  const float tx = (y1-y0) * invaspect * 4.0 / 9.0;

  if (2.0 * tx > (x1-x0)) {
    /* this should be avoided, button aspect ratio should be >= 1.33 */
    x0A = x0B = (x1-x0) / 2.0;
  } else {
    x0A = x0 + tx;
    x0B = x1 - tx;
  }

  glPushMatrix();
  glLoadIdentity();
  glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, btncol);
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, btncol);
  glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, btncol);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_SRC_ALPHA);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glBindTexture(GL_TEXTURE_2D, ui->texID[15]);
  glBegin(GL_QUADS);
  glTexCoord2f (0.0,  0.0); glVertex3f(x0, y0 * invaspect, 0);
  glTexCoord2f (0.0,  1.0); glVertex3f(x0, y1 * invaspect, 0);
  glTexCoord2f (0.33, 1.0); glVertex3f(x0A, y1 * invaspect, 0);
  glTexCoord2f (0.33, 0.0); glVertex3f(x0A, y0 * invaspect, 0);

  glTexCoord2f (0.33, 0.0); glVertex3f(x0A, y0 * invaspect, 0);
  glTexCoord2f (0.33, 1.0); glVertex3f(x0A, y1 * invaspect, 0);
  glTexCoord2f (0.66, 1.0); glVertex3f(x0B, y1 * invaspect, 0);
  glTexCoord2f (0.66, 0.0); glVertex3f(x0B, y0 * invaspect, 0);

  glTexCoord2f (0.66, 0.0); glVertex3f(x0B, y0 * invaspect, 0);
  glTexCoord2f (0.66, 1.0); glVertex3f(x0B, y1 * invaspect, 0);
  glTexCoord2f (1.0,  1.0); glVertex3f(x1, y1 * invaspect, 0);
  glTexCoord2f (1.0,  0.0); glVertex3f(x1, y0 * invaspect, 0);
  glEnd();
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_SRC_ALPHA_SATURATE);
  glPopMatrix();
}

static void
unity_button(PuglView* view,
    const float x0, const float x1,
    const float y0, const float y1,
    int hover
    )
{
  GLfloat btncol[] = {0.1, 0.3, 0.1, 1.0 };
  if (hover) {
    btncol[0] = 0.2; btncol[1] = 0.6; btncol[2] = 0.2;
  }
  unity_button_color(view, x0, x1, y0, y1, btncol);
}

static void
gui_button(PuglView* view,
    const float x0, const float x1,
    const float y0, const float y1,
    int hovermask, const char *label
    )
{
  B3ui* ui = (B3ui*)puglGetHandle(view);
  const GLfloat mat_white[] = { 1.0, 1.0, 1.0, 1.0 };
  const float invaspect = 320. / 960.;
  unity_button(view, x0, x1, y0, y1, ui->mouseover & hovermask);
  render_title(view, label, (x1 + x0) / 2.0 / SCALE, invaspect * (y1 + y0) / 2.0 / SCALE, 0.5, mat_white, TA_CENTER_MIDDLE);
}

static void
menu_button(PuglView* view,
    const float x0, const float x1,
    const float y0, const float y1,
    int texid,
    int hovermask, const char *label
    )
{
  B3ui* ui = (B3ui*)puglGetHandle(view);
  const float invaspect = 320. / 960.;
  GLfloat mat_x[] = { 1.0, 1.0, 1.0, 1.0 };

  if (ui->mouseover & hovermask) {
    mat_x[0] = 0.5;
    mat_x[1] = 0.5;
    mat_x[2] = 0.5;
    mat_x[3] = 1.0;
  }

  if (texid > 0) {
    glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_x);
    glMaterialfv(GL_FRONT, GL_AMBIENT, mat_x);
    glMaterialfv(GL_FRONT, GL_EMISSION, mat_x);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, ui->texID[texid]);
    glBegin(GL_QUADS);
    glColor3f(1.0, 1.0, 1.0);
    glTexCoord2f (0.0, 0.0); glVertex3f(x0, invaspect * y0, 0);
    glTexCoord2f (0.0, 1.0); glVertex3f(x0, invaspect * y1, 0);
    glTexCoord2f (1.0, 1.0); glVertex3f(x1, invaspect * y1, 0);
    glTexCoord2f (1.0, 0.0); glVertex3f(x1, invaspect * y0, 0);
    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_SRC_ALPHA_SATURATE);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    if (ui->mouseover & hovermask) {
      const GLfloat mat_w[] = { 1.0, 1.0, 1.0, 1.0 };
      render_title(view, label, (x1 + x0) / 2.0 / SCALE, invaspect * (y1 + y0) / 2.0 / SCALE, 0.5, mat_w, TA_CENTER_MIDDLE);
    }
  } else {
    const GLfloat mat_w[] = { 1.0, 1.0, 1.0, 1.0 };
    unity_button(view, x0, x1, y0, y1, ui->mouseover & hovermask);
    render_title(view, label, (x1 + x0) / 2.0 / SCALE, invaspect * (y1 + y0) / 2.0 / SCALE, 0.5, mat_w, TA_CENTER_MIDDLE);
  }

}

static void
unity_tri(PuglView* view,
    const float x0,
    const float y0, const float y1,
    const GLfloat color[4])
{
  const float invaspect = 320. / 960.;
  glPushMatrix();
  glLoadIdentity();
  glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, color);
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, color);
  glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, color);
  glBegin(GL_TRIANGLES);
  glVertex3f(x0                          , invaspect * (y1+y0)/2.0, 0);
  glVertex3f(x0 + invaspect * (y1-y0)/2.0, invaspect * y1, 0);
  glVertex3f(x0 + invaspect * (y1-y0)/2.0, invaspect * y0, 0);
  glEnd();
  glPopMatrix();
}


/******************************************************************************
 */

int save_cfgpgm(PuglView* view, const char *fn, int mode, int override) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  if (check_extension(fn, mode == 6 ? ".pgm" : ".cfg")) {
    if (mode == 6)
      show_message(view, "file does not end in '.pgm'");
    else
      show_message(view, "file does not end in '.cfg'");
    return -1;
  }
  if (!override && !access(fn, F_OK)) {
    if (!show_message(view, "file exists. Overwrite?")) {
      ui->pendingdata = strdup(fn);
      ui->pendingmode = mode;
    }
    return 0;
  }
  if (mode == 6) {
    forge_message_str(ui, ui->uris.sb3_savepgm, fn);
  } else {
    forge_message_str(ui, ui->uris.sb3_savecfg, fn);
  }
  return 0;
}

void handle_msg_reply(PuglView* view) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  if (!ui->pendingdata || !ui->pendingmode) return;

  if (ui->pendingmode == 5 || ui->pendingmode == 6) {
    save_cfgpgm(view, ui->pendingdata, ui->pendingmode, 1);
  } else {
    fprintf(stderr, "B3Lv2UI: invalid pending mode.\n");
  }
}

/******************************************************************************
 * openGL text entry
 */

static int txtentry_start(PuglView* view, const char *title, const char *defaulttext) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  if (ui->textentry_active) return -1;

  if (!defaulttext) {
    ui->textentry_text[0] = '\0';
  } else {
    strncpy(ui->textentry_text, defaulttext, 1024);
  }
  sprintf(ui->textentry_title, "%s", title);
  ui->textentry_active = 1;
  onReshape(view, ui->width, ui->height);
  puglPostRedisplay(view);
  return 0;
}

static void txtentry_end(PuglView* view, const char *txt) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  if (!txt || strlen(txt) ==0) {
    show_message(view, "empty name.");
    ui->displaymode = 0;
    return;
  }
  switch(ui->displaymode) {
    case 3:
      if (ui->pgm_sel >= 0) {
	forge_message_kv(ui, ui->uris.sb3_midisavepgm, ui->pgm_sel, txt);
      }
      ui->displaymode = 0;
      break;
    case 6:
    case 5:
      {
      char * rfn = absfilepath(ui->curdir, txt);
      if (save_cfgpgm(view, rfn, ui->displaymode, 0)) {
	ui->textentry_active = 1;
      } else {
	ui->displaymode = 0;
      }
      free(rfn);
      }
      break;
    default:
      fprintf(stderr, "B3Lv2UI: unhandled text entry (mode:%d)\n", ui->displaymode);
      ui->displaymode = 0;
      break;
  }
  onReshape(view, ui->width, ui->height);
}

static void txtentry_handle(PuglView* view, uint32_t key) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  int pos = strlen(ui->textentry_text);
  switch (key) {
    case 8: // backspace
    case 127: // delete
      if (pos > 0) pos--;
      break;
    case 27: // ESC
      pos = 0;
      ui->textentry_text[pos] = '\0';
      ui->textentry_active = 0;
      ui->displaymode = 0;
      onReshape(view, ui->width, ui->height);
      break;
    case 13: // Enter
      ui->textentry_active = 0;
      txtentry_end(view, ui->textentry_text);
      onReshape(view, ui->width, ui->height);
      break;
    default:
      if (key >= 32 && key <= 125 && key != 47 && key !=92) {
	ui->textentry_text[pos++] = (char) key;
      }
      break;
  }
  if (pos > 1023) pos=1023;
  ui->textentry_text[pos] = '\0';
  puglPostRedisplay(view);
}

static void txtentry_render(PuglView* view) {
  B3ui* ui = (B3ui*)puglGetHandle(view);

  const GLfloat mat_b[] = {0.0, 0.0, 0.0, 1.0};
  const GLfloat mat_w[] = {1.0, 1.0, 1.0, 1.0};
  const GLfloat mat_g[] = {0.1, 0.9, 0.15, 1.0};
  const GLfloat mat_x[] = {0.1, 0.1, 0.15, 1.0};

  unity_box(view, -1.0, 1.0, -.12, .12, mat_x);

  glPushMatrix();
  glEnable(GL_BLEND);
  glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_b);
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_b);
  glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_g);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);

  glLoadIdentity();
  glScalef(0.001,0.001,1.00);
  glRotatef(180, 1, 0, 0);
  float bb[6];
  ftglGetFontBBox(ui->font_big, ui->textentry_text, -1, bb);

  glTranslatef((bb[3] - bb[0])/-2.0, -0.25 * (1000.0*SCALE), 0.1);
  ftglRenderFont(ui->font_big, ui->textentry_text, FTGL_RENDER_ALL);

  glTranslatef((bb[3] - bb[0]), 0, 0);
  glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_w);
  ftglRenderFont(ui->font_big, "|", FTGL_RENDER_ALL);

  glLoadIdentity();
  glScalef(0.001,0.001,1.00);
  glRotatef(180, 1, 0, 0);
  ftglGetFontBBox(ui->font_big, ui->textentry_title, -1, bb);
  glTranslatef((bb[3] - bb[0])/-2.0, (bb[4] - bb[1])/-2.0, 0);
  glTranslatef(0, 4.5 * (1000.0*SCALE), 0.1);
  ftglRenderFont(ui->font_big, ui->textentry_title, FTGL_RENDER_ALL);

  glDisable(GL_BLEND);
  glPopMatrix();
  render_text(view, "<enter> to confirm, <ESC> to abort", 0, 6.0, 0, TA_CENTER_MIDDLE);

}

/******************************************************************************
 * keyboard manuals & pedals
 */

static void piano_manual(PuglView* view, float y0, float z0, int active_key, unsigned int *active_keys) {
  B3ui* ui = (B3ui*)puglGetHandle(view);

  const GLfloat no_mat[] = { 0.0, 0.0, 0.0, 1.0 };
  const GLfloat mat_key_white[] = { 0.7, 0.8, 0.8, 1.0 };
  const GLfloat mat_key_black[] = { 0.2, 0.15, 0.05, 1.0 };
  const GLfloat glow_red[] = { 1.0, 0.0, 0.00, 1.0 };

  int i;
  for (i = 0; i < 61; ++i) {
    glPushMatrix();
    glLoadIdentity();
    glScalef(SCALE, SCALE, SCALE);

    const int octave  = i / 12;
    const int key  = i % 12;
    const int bwk  = key >= 5 ? key + 1 : key;

    if (bwk % 2 == 0) {
      float x0 = (octave * 7.0 + bwk / 2.0) * 1.00;
      glTranslatef(-16.f + x0, y0, z0);
      glRotatef(180, 0, 1, 0);

      glMaterialfv(GL_FRONT, GL_AMBIENT, mat_key_white);

      if (/* i == active_key || */ active_keys[i/32] & (1<<(i%32))) {
	if (ui->highlight_keys) {
	  glMaterialfv(GL_FRONT, GL_EMISSION, glow_red);
	  glMaterialfv(GL_FRONT, GL_DIFFUSE, glow_red);
	} else {
	  glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);
	  glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_key_white);
	}
	glRotatef(-5, 1, 0, 0);
      } else {
	glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);
	glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_key_white);
      }

      if (i == 60) {
	drawMesh(view, OBJ_WHITE_KEY0, 1);
      } else switch(key) {
	case 0:
	case 5:
	  drawMesh(view, OBJ_WHITE_KEYCF, 1);
	  break;
	case 4:
	case 11:
	  drawMesh(view, OBJ_WHITE_KEYEB, 1);
	  break;
	case 2:
	  drawMesh(view, OBJ_WHITE_KEYD, 1);
	  break;
	case 7:
	  drawMesh(view, OBJ_WHITE_KEYG, 1);
	  break;
	case 9:
	  drawMesh(view, OBJ_WHITE_KEYA, 1);
	  break;
	default:
	  break;
      }

    } else {
      float x0 = (octave * 7.0 + bwk / 2.0) * 1.00 - .1;
      if (key == 1 || key == 6) x0 -= .2;
      if (key == 8) x0 -= .1;

      glTranslatef(-16.f + x0, y0, z0);
      glRotatef(180, 0, 1, 0);

      glMaterialfv(GL_FRONT, GL_AMBIENT, mat_key_black);
      glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_key_black);

      if (/* i == active_key || */ active_keys[i/32] & (1<<(i%32))) {
	if (ui->highlight_keys) {
	  glMaterialfv(GL_FRONT, GL_EMISSION, glow_red);
	} else {
	  glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);
	}
	glTranslatef(0.f, .0f, -.15f);
	glRotatef(-2, 1, 0, 0);
      } else {
	glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);
      }
      drawMesh(view, OBJ_BLACK_KEY, 1);
    }
    glPopMatrix();
  }
}

static void piano_pedals(PuglView* view, int active_key, unsigned int active_keys) {
  B3ui* ui = (B3ui*)puglGetHandle(view);

  const float y0 = -7;
  const float z0 = 27.25;
  const GLfloat mat_key_white[] = { 0.6, 0.55, 0.45, 1.0 };
  const GLfloat mat_key_black[] = { 0.3, 0.25, 0.15, 1.0 };
  const GLfloat no_mat[] = { 0.0, 0.0, 0.0, 1.0 };
  const GLfloat glow_red[] = { 1.0, 0.0, 0.00, 1.0 };

  int i;
  for (i = 0; i < 25; ++i) {
    glPushMatrix();
    glLoadIdentity();
    glScalef(SCALE, SCALE, SCALE);

    const int octave  = i / 12;
    const int key  = i % 12;
    const int bwk  = key >= 5 ? key + 1 : key;

    if (bwk % 2 == 0) {
      float x0 = (octave * 7.0 + bwk / 2.0) * 2.00;
      glTranslatef(-13 + x0, 12.2 + y0, z0);
      glRotatef(180, 0, 1, 0);

      glMaterialfv(GL_FRONT, GL_AMBIENT, mat_key_white);
      glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_key_white);

      if (/*i == active_key || */ active_keys & (1<<i)) {
	glRotatef( 2, 1, 0, 0);
	if (ui->highlight_keys) {
	  glMaterialfv(GL_FRONT, GL_EMISSION, glow_red);
	} else {
	  glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);
	}
      } else {
	glRotatef( 0, 1, 0, 0);
	glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);
      }

      drawMesh(view, OBJ_PEDAL_WHITE, 1);

    } else {
      float x0 = (octave * 7.0 + bwk / 2.0) * 2.00 - .2;

      glTranslatef(-13.f + x0, y0, z0);
      glRotatef(180, 0, 1, 0);

      glMaterialfv(GL_FRONT, GL_AMBIENT, mat_key_black);
      glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_key_black);

      if (/*i == active_key || */ active_keys & (1<<i)) {
	if (ui->highlight_keys) {
	  glMaterialfv(GL_FRONT, GL_EMISSION, glow_red);
	} else {
	  glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);
	}
	glRotatef(-5, 1, 0, 0);
      } else {
	glRotatef( 0, 1, 0, 0);
	glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);
      }

      drawMesh(view, OBJ_PEDAL_BLACK, 1);
    }
    glPopMatrix();
  }
}

/******************************************************************************
 * 'advanced config
 */

static float coeff_to_db(const float c, float min120) {
  return c >= 1e-6 ? 20.0 * log10f(c) : min120;
}

static float db_to_coeff(const float c) {
  if (c < -120.f) return 0.0;
  return powf(10, .05f * c);
}

static const ConfigDoc * searchDoc (const ConfigDoc *d, const char *key) {
  while (d && d->name) {
    if (!strcmp(d->name, key)) {
      return d;
      break;
    }
    ++d;
  }
  return NULL;
}

static const ConfigDoc * searchDocs (const char *key) {
  ConfigDoc const * rv = NULL;
  if (!rv) rv = searchDoc(midiDoc(), key);
  if (!rv) rv = searchDoc(pgmDoc(), key);
  if (!rv) rv = searchDoc(oscDoc(), key);
  if (!rv) rv = searchDoc(scannerDoc(), key);
  if (!rv) rv = searchDoc(ampDoc(), key);
  if (!rv) rv = searchDoc(whirlDoc(), key);
  if (!rv) rv = searchDoc(reverbDoc(), key);
  return rv;
}

// one-time call. populate b3config struct.
static void cfg_initialize_param(B3ui * ui, const char *cfgkey, int p) {
  ui->cfgvar[p].d = searchDocs (cfgkey);
  assert(ui->cfgvar[p].d);
  assert(ui->cfgvar[p].d->type != CFG_DECIBEL || ui->cfgvar[p].format == CF_DECIBEL);
  assert(ui->cfgvar[p].d->type != CFG_INT || ui->cfgvar[p].format == CF_INTEGER);

  switch(ui->cfgvar[p].d->type) {
    case CFG_DOUBLE:
    case CFG_DECIBEL:
    case CFG_FLOAT:
    case CFG_INT:
      {
      assert(ui->cfgvar[p].format == CF_DECIBEL || ui->cfgvar[p].format == CF_NUMBER || ui->cfgvar[p].format == CF_PERCENT || ui->cfgvar[p].format == CF_DEGREE || ui->cfgvar[p].format == CF_INTEGER);
      assert(ui->cfgvar[p].d->dflt);
      LOCALEGUARD_START;
      ui->cfgvar[p].dflt = atof(ui->cfgvar[p].d->dflt);
      LOCALEGUARD_END;
      }
      break;
    case CFG_TEXT:
      if (ui->cfgvar[p].lut) {
	assert(ui->cfgvar[p].format == CF_LISTLUT);
	int ii = 0;
	while (ui->cfgvar[p].lut[ii].label) {
	  // strstr due to quotes in doc
	  if (strstr(ui->cfgvar[p].d->dflt, ui->cfgvar[p].lut[ii].label)) {
	    ui->cfgvar[p].dflt = ui->cfgvar[p].lut[ii].val;
	    break;
	  }
	  ++ii;
	}
      } else {
	assert(ui->cfgvar[p].format == CF_NUMBER);
	assert(0);
      }
    default:
      break;
  }
}

#define CFGP(ID, TITLE, FORMAT, LUT)  \
  ui->cfgvar[p].title   = TITLE;      \
  ui->cfgvar[p].format  = FORMAT;     \
  ui->cfgvar[p].lut     = LUT;        \
  cfg_initialize_param(ui, ID, p);    \
  ++p;

static const b3scalepoint x_temperament[] = {
  {0, "gear60"},
  {1, "gear50"},
  {2, "equal"},
  {0, NULL}
};

static const b3scalepoint x_contactmodel[] = {
  {0, "click"},
  {1, "shelf"},
  {2, "cosine"},
  {3, "linear"},
  {0, NULL}
};

static const b3scalepoint x_filtertype[] = {
  {0, "Low Pass"},
  {1, "High Pass"},
  {2, "Band Pass (const skirt)"},
  {3, "Band Pass (0db peak)"},
  {4, "Notch"},
  {5, "All Pass"},
  {6, "Peaking"},
  {7, "Low Shelf"},
  {8, "High Shelf"},
  {0, NULL}
};

static const b3scalepoint x_brakepos[] = {
  {0,    "Free, no brake."},
  {0.25, "Left (90deg)"},
  {0.50, "Back (180deg)"},
  {0.75, "Right (-90deg)"},
  {1.0,  "Front (center)"},
  {0, NULL}
};

static const b3scalepoint x_micwidth[] = {
  {-1, "mono (left)"},
  { 0, "stereo"},
  { 1, "mono (right)"},
  {0, NULL}
};

static const b3scalepoint x_bypass[] = {
  {0, "off (effect is active)"},
  {1, "on (effect is disabled)"},
  {0, NULL}
};

static const b3scalepoint x_zerooff[] = { {0, "off"}, {0, NULL} };
static const b3scalepoint x_zeronone[] = { {0, "none"}, {0, NULL} };
//static const b3scalepoint x_zerodisabled[] = { {0, "disabled"}, {0, NULL} };


static void cfg_initialize(B3ui * ui) {
  memset(ui->cfgvar, 0, sizeof(ui->cfgvar)); // XXX
  int p = 0;

  CFGP("osc.tuning",                  "Tuning",                      CF_NUMBER, NULL);
  CFGP("osc.temperament",             "Temperament",                 CF_LISTLUT, x_temperament); // 'gear60'

  p+=2;
  CFGP("midi.transpose",              "Transpose",                   CF_INTEGER, NULL);
  CFGP("midi.upper.transpose",        "Transp. Upper",               CF_INTEGER, NULL);
  CFGP("midi.lower.transpose",        "Transp. Lower",               CF_INTEGER, NULL);
  CFGP("midi.pedals.transpose",       "Transp. Pedal",               CF_INTEGER, NULL);

#if 0 // need further setup work to initialize key-split
  CFGP("midi.upper.transpose.split",  "Splitpoint for Upper Manual", CF_INTEGER, NULL);
  CFGP("midi.lower.transpose.split",  "Splitpoint for Lower Manual", CF_INTEGER, NULL);
  CFGP("midi.pedals.transpose.split", "Splitpoint for Pedals",       CF_INTEGER, NULL);
#endif

  p=48; // tab 2;

  CFGP("osc.compartment-crosstalk",   "Comp. X-Talk",         CF_DECIBEL, NULL);
  //CFGP("osc.transformer-crosstalk", "trans. X-Talk",        CF_DECIBEL, NULL);
  CFGP("osc.terminalstrip-crosstalk", "Term. X-Talk",         CF_DECIBEL, NULL);
  CFGP("osc.wiring-crosstalk",        "Wire X-Talk",          CF_DECIBEL, NULL);
  p+=1;

  p+=3;
  CFGP("osc.contribution-floor",      "X-Talk Floor",         CF_DECIBEL, x_zerooff);
  p+=3;
  CFGP("osc.contribution-min",        "X-Talk Min",           CF_DECIBEL, x_zeronone);

  p+=4;

  CFGP("osc.attack.model",            "Attack Model",         CF_LISTLUT, x_contactmodel);
  CFGP("osc.attack.click.level",      "Key Click Level",      CF_PERCENT, NULL);
  p+=1;
  CFGP("osc.attack.click.minlength",  "Click Len Min",        CF_PERCENT, NULL);

  CFGP("osc.release.model",           "Release Model",        CF_LISTLUT, x_contactmodel);
  CFGP("osc.release.click.level",     "Keyrelease Att.",      CF_PERCENT, NULL);
  p+=1;
  CFGP("osc.attack.click.maxlength",  "Click Len Max",        CF_PERCENT, NULL);


  p=24; // tab 1;
  CFGP("scanner.hz",                  "Vibrato Freq",         CF_NUMBER, NULL);
  CFGP("scanner.modulation.v1",       "Vibrato 1 Mod.",       CF_NUMBER, NULL);
  CFGP("scanner.modulation.v2",       "Vibrato 2 Mod.",       CF_NUMBER, NULL);
  CFGP("scanner.modulation.v3",       "Vibrato 3 Mod.",       CF_NUMBER, NULL);

  p+=4;
  p+=4;

  CFGP("osc.perc.fast",               "Perc. fast decay",     CF_NUMBER, NULL);
  CFGP("osc.perc.slow",               "Perc. slow decay",     CF_NUMBER, NULL);
  CFGP("osc.perc.normal",             "Perc. Amp norm",       CF_DECIBEL, NULL);
  CFGP("osc.perc.soft",               "Perc. Amp soft",       CF_DECIBEL, NULL);

  CFGP("osc.perc.gain",               "Perc. Gain Scale",     CF_NUMBER, NULL);

  p=72; // tab 3;
  CFGP("whirl.horn.slowrpm",          "Horn RPM [slow]",      CF_NUMBER, NULL);
  CFGP("whirl.horn.fastrpm",          "Horn RPM [fast]",      CF_NUMBER, NULL);
  CFGP("whirl.drum.slowrpm",          "Drum RPM [slow]",      CF_NUMBER, NULL);
  CFGP("whirl.drum.fastrpm",          "Drum RPM [fast]",      CF_NUMBER, NULL);

  CFGP("whirl.horn.acceleration",     "Horn Acceleraton",     CF_NUMBER, NULL);
  CFGP("whirl.horn.deceleration",     "Horn Deceleration",    CF_NUMBER, NULL);
  CFGP("whirl.drum.acceleration",     "Drum Acceleraton",     CF_NUMBER, NULL);
  CFGP("whirl.drum.deceleration",     "Drum Deceleration",    CF_NUMBER, NULL);

  CFGP("whirl.horn.level",            "Horn Level",           CF_DECIBEL, NULL);
  CFGP("whirl.horn.leak",             "Horn leakage",         CF_DECIBEL, NULL);

  CFGP("whirl.horn.width",            "Horn mic",             CF_NUMBER, x_micwidth);
  CFGP("whirl.drum.width",            "Drum mic",             CF_NUMBER, x_micwidth);

  CFGP("whirl.horn.radius",           "Horn Radius",          CF_NUMBER, NULL);
  CFGP("whirl.horn.brakepos",         "Horn Break",           CF_DEGREE, x_brakepos);
  CFGP("whirl.drum.radius",           "Drum Radius",          CF_NUMBER, NULL);
  CFGP("whirl.drum.brakepos",         "Drum Break",           CF_DEGREE, x_brakepos);

  CFGP("whirl.horn.mic.angle",        "Horn mic Agle",        CF_NUMBER, NULL);
  p+=3;

#if 1
  CFGP("whirl.horn.offset.x",         "Horn X offset",        CF_NUMBER, NULL);
  CFGP("whirl.horn.offset.z",         "Horn Z offset",        CF_NUMBER, NULL);
  CFGP("whirl.mic.distance",          "Mic distance",         CF_NUMBER, NULL);
#else
  CFGP("whirl.mic.distance",          "Mic distance",         CF_NUMBER, NULL);
  p+=2;
#endif

  CFGP("whirl.bypass",                "Bypass",               CF_INTEGER, x_bypass);

  p=96;
  CFGP("whirl.drum.filter.hz",        "Drum Filter Freq",     CF_NUMBER, NULL);
  CFGP("whirl.drum.filter.gain",      "Drum Filter Gain",     CF_NUMBER, NULL);
  CFGP("whirl.drum.filter.q",         "Drum Filter Q",        CF_NUMBER, NULL);
  CFGP("whirl.drum.filter.type",      "Type",                 CF_INTEGER, x_filtertype);

  CFGP("whirl.horn.filter.a.hz",      "Horn Filter 1 Freq",   CF_NUMBER, NULL);
  CFGP("whirl.horn.filter.a.gain",    "Horn Filter 1 Gain",   CF_NUMBER, NULL);
  CFGP("whirl.horn.filter.a.q",       "Horn Filter 1 Q",      CF_NUMBER, NULL);
  CFGP("whirl.horn.filter.a.type",    "Type",                 CF_INTEGER, x_filtertype);

  CFGP("whirl.horn.filter.b.hz",      "Horn Filter 2 Freq",   CF_NUMBER, NULL);
  CFGP("whirl.horn.filter.b.gain",    "Horn Filter 2 Gain",   CF_NUMBER, NULL);
  CFGP("whirl.horn.filter.b.q",       "Horn Filter 2 Q",      CF_NUMBER, NULL);
  CFGP("whirl.horn.filter.b.type",    "Type",                 CF_INTEGER, x_filtertype);
  //CFGP("", "", "", .0, .5);

  p+=4;
  p+=4;
  CFGP("reverb.inputgain",            "Reverb Gain",          CF_DECIBEL, NULL);

}

static void cfg_set_defaults(B3ui * ui) {
  int i;
  for (i=0; i < MAXCFG; ++i) {
    ui->cfgvar[i].cur = ui->cfgvar[i].dflt;
  }
}

static const char *lut_lookup_value(b3scalepoint const * const lut, const float val) {
  int ii = 0;
  if (!lut) return NULL;
  while (lut[ii].label) {
#if 0 // within 0.01%
    if (rintf(10000.0 * val) == rintf(10000.0 * lut[ii].val))
#else // exact
    if (val == lut[ii].val)
#endif
    {
      return lut[ii].label;
    }
    ++ii;
  }
  return NULL;
}

/* message from backend (on re-init) */
static void cfg_parse_config(B3ui * ui, const char *key, const char *value) {
  int relevant = 0;
  int i;
  if (!strcmp("lv2.info", key)) {
    strncpy(ui->lv2nfo, value, 128);
    return;
  }
  for (i = 0 ; i < MAXCFG ; ++i) {
    if (!ui->cfgvar[i].d) continue;
    if (!strcmp(ui->cfgvar[i].d->name, key)) {

      switch(ui->cfgvar[i].format) {
	case CF_LISTLUT:
	  {
	    int ii = 0;
	    while (ui->cfgvar[i].lut[ii].label) {
	      if (!strcmp(value, ui->cfgvar[i].lut[ii].label)) {
		ui->cfgvar[i].cur = ui->cfgvar[i].lut[ii].val;
		break;
	      }
	      ++ii;
	    }
	  }
	  break;
	default:
	  {
	  LOCALEGUARD_START;
	  ui->cfgvar[i].cur = atof(value);
	  LOCALEGUARD_END;
	  }
	  break;
      }
      relevant = 1;
      break;
    }
  }

  if (ui->displaymode == 8 && relevant) {
    puglPostRedisplay(ui->view);
  }
}

static void cfg_special_button(PuglView *view, int ccc) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  assert(ui->cfgtab == 0);

  if (ui->reinit) {
    puglPostRedisplay(view);
    return;
  }

  if (ccc == 21) {
    forge_message_str(ui, ui->uris.sb3_cfgstr, "special.reset=1");
    ui->reinit = 1;
    return;
  }
  if (ccc == 22) {
    forge_message_str(ui, ui->uris.sb3_cfgstr, "special.reconfigure=1");
    ui->reinit = 1;
    return;
  }
}

static void cfg_start_drag(B3ui* ui, int cfg) {
  assert (cfg > 0 && cfg <= 24);

  int ccc = 24 * ui->cfgtab + cfg - 1;
  if (ccc >= MAXCFG || !ui->cfgvar[ccc].d) return;

  ui->cfgdrag = cfg;
  ui->dragval = ui->cfgvar[ccc].cur;

  float range;
  const float step  = ui->cfgvar[ccc].d->ui_step;
  switch (ui->cfgvar[ccc].format) {
    case CF_DECIBEL:
      range = fabsf (coeff_to_db (ui->cfgvar[ccc].d->max, -120) - coeff_to_db (ui->cfgvar[ccc].d->min, -120));
      break;
    default:
      range = ui->cfgvar[ccc].d->max - ui->cfgvar[ccc].d->min;
      break;
  }
  assert (range >= step);
  ui->dragmult = (range / step < 12) ? 5 : (range / step);
  ui->dragmult /= 350; // px
  //printf("DRAG S:%f  R: %f   M: %f  1/M: %f\n", step, range, ui->dragmult, 1.0 / ui->dragmult);
}

static float cfg_update_parameter(B3ui* ui, int ccc, float val, int delta) {
  assert (ccc >= 0 && ccc < MAXCFG && ui->cfgvar[ccc].d);

  float rv;
  if (delta == 0) {
    rv = ui->cfgvar[ccc].dflt;
  } else {
    switch (ui->cfgvar[ccc].format) {
      case CF_DECIBEL:
	rv = db_to_coeff(coeff_to_db(val, -120) + delta * ui->cfgvar[ccc].d->ui_step);
	break;
      default:
	rv = val + delta * ui->cfgvar[ccc].d->ui_step;
	break;
    }
  }

  if (rv < ui->cfgvar[ccc].d->min)
    rv = ui->cfgvar[ccc].d->min;

  if (rv > ui->cfgvar[ccc].d->max)
    rv = ui->cfgvar[ccc].d->max;
  return rv;
}

static void cfg_tx_update(B3ui* ui, int ccc) {
  char cfgstr[128];
  LOCALEGUARD_START;
  switch(ui->cfgvar[ccc].format) {
    case CF_LISTLUT:
      snprintf(cfgstr, sizeof(cfgstr), "%s=%s",
	  ui->cfgvar[ccc].d->name,
	  ui->cfgvar[ccc].lut[(int)rint(ui->cfgvar[ccc].cur)].label);
      break;
    case CF_INTEGER:
      snprintf(cfgstr, sizeof(cfgstr), "%s=%.0f",
	  ui->cfgvar[ccc].d->name, ui->cfgvar[ccc].cur);
      break;
    case CF_PERCENT:
    case CF_DEGREE:
      snprintf(cfgstr, sizeof(cfgstr), "%s=%.4f",
	  ui->cfgvar[ccc].d->name, ui->cfgvar[ccc].cur);
      break;
    default:
      snprintf(cfgstr, sizeof(cfgstr), "%s=%.10f",
	  ui->cfgvar[ccc].d->name, ui->cfgvar[ccc].cur);
      break;
  }
  LOCALEGUARD_END;

  forge_message_str(ui, ui->uris.sb3_cfgstr, cfgstr);
  ui->reinit = 1;
}

/* handle user click or wheel */
static void cfg_update_value(PuglView *view, int ccc, int dir) {
  B3ui* ui = (B3ui*)puglGetHandle(view);

  assert(dir >= -10 && dir <= 10);

  if (ccc >= 24) return;

  if (ui->reinit) {
    puglPostRedisplay(view);
    return;
  }

  ccc += 24 * ui->cfgtab;

  if (ccc >= MAXCFG || !ui->cfgvar[ccc].d) return;

  float oldval = ui->cfgvar[ccc].cur;

  ui->cfgvar[ccc].cur = cfg_update_parameter(ui, ccc, ui->cfgvar[ccc].cur, dir);

  if (oldval == ui->cfgvar[ccc].cur) {
    return;
  }
  cfg_tx_update(ui, ccc);
}

static void
render_cfg_button(PuglView* view,
    int ccc,
    float x0, float x1, float y0, float y1,
    int hover_btn, int hover_tri, bool dragging)
{
  B3ui* ui = (B3ui*)puglGetHandle(view);
  const GLfloat mat_tri[] = {0.1, 0.1, 0.1, 1.0};
  const GLfloat mat_act[] = {0.9, 0.9, 0.9, 1.0};
  const GLfloat mat_w[] = {1.0, 1.0, 1.0, 1.0};
  const GLfloat mat_b[] = {0.0, 0.0, 0.0, 1.0};
  const float invaspect = 320. / 960.;

  GLfloat btncol[] = {0.1, 0.3, 0.1, 1.0 };
  if (dragging) {
    btncol[0] = 0.6; btncol[1] = 0.6; btncol[2] = 0.1;
  } else if (hover_btn) {
    btncol[0] = 0.2; btncol[1] = 0.6; btncol[2] = 0.2;
  }

  const float val = dragging ? ui->dragval : ui->cfgvar[ccc].cur;

  if (val != ui->cfgvar[ccc].dflt) {
    btncol[2] += 0.15;
  }

  unity_button_color(view, x0, x1, y0, y1, btncol);
  if (!hover_btn) hover_tri = 0;
  unity_tri(view, x0 + .004, y0 + .020, y1 - .020, hover_tri < 0 ? mat_act : mat_tri);
  unity_tri(view, x1 - .004, y1 - .020, y0 + .020, hover_tri > 0 ? mat_act : mat_tri);

  char txt[64]; char const * lbl;

  switch(ui->cfgvar[ccc].format) {
    case CF_LISTLUT:
      snprintf(txt, sizeof(txt), "%s: %s", ui->cfgvar[ccc].title,
	  ui->cfgvar[ccc].lut[(int)rint(val)].label);
      break;
    case CF_INTEGER:
      if ((lbl = lut_lookup_value(ui->cfgvar[ccc].lut, val))) {
	snprintf(txt, sizeof(txt), "%s: %s",
	    ui->cfgvar[ccc].title, lbl);
      } else {
	snprintf(txt, sizeof(txt), "%s: %.0f %s",
	    ui->cfgvar[ccc].title, val, ui->cfgvar[ccc].d->unit);
      }
      break;
    case CF_PERCENT:
      if ((lbl = lut_lookup_value(ui->cfgvar[ccc].lut, val))) {
	snprintf(txt, sizeof(txt), "%s: %s",
	    ui->cfgvar[ccc].title, lbl);
      } else {
	snprintf(txt, sizeof(txt), "%s: %.1f%s",
	    ui->cfgvar[ccc].title, 100.f * val, ui->cfgvar[ccc].d->unit);
      }
      break;
    case CF_DEGREE:
      if ((lbl = lut_lookup_value(ui->cfgvar[ccc].lut, val))) {
	snprintf(txt, sizeof(txt), "%s: %s",
	    ui->cfgvar[ccc].title, lbl);
      } else {
	snprintf(txt, sizeof(txt), "%s: %.1f%s",
	    ui->cfgvar[ccc].title, 360.f * val, ui->cfgvar[ccc].d->unit);
      }
      break;
    case CF_DECIBEL:
      if ((lbl = lut_lookup_value(ui->cfgvar[ccc].lut, val))) {
	snprintf(txt, sizeof(txt), "%s: %s",
	    ui->cfgvar[ccc].title, lbl);
      } else {
	snprintf(txt, sizeof(txt), "%s: %+.0f%s",
	    ui->cfgvar[ccc].title, coeff_to_db(val, -INFINITY), ui->cfgvar[ccc].d->unit);
      }
      break;
    default:
      if ((lbl = lut_lookup_value(ui->cfgvar[ccc].lut, val))) {
	snprintf(txt, sizeof(txt), "%s: %s",
	    ui->cfgvar[ccc].title, lbl);
      } else {
	snprintf(txt, sizeof(txt), "%s: %.2f%s",
	    ui->cfgvar[ccc].title, val, ui->cfgvar[ccc].d->unit);
      }
      break;
  }

  render_small_text(view, txt,
      (x1 + x0) / 2.0 / SCALE, invaspect * (y1 + y0) / 2.0 / SCALE,
      0.5, hover_btn ? mat_b : mat_w, TA_CENTER_MIDDLE);
}

static int cfg_tabbar(const float fx) {
  if      (fx > -.975 && fx < -.625) return 0;
  else if (fx > -.575 && fx < -.225) return 1;
  else if (fx > -.175 && fx <  .175) return 2;
  else if (fx >  .226 && fx <  .575) return 3;
  else if (fx >  .625 && fx <  .975) return 4;
  return -1;
}

static int cfg_mousepos(const float fx, const float fy, int *tri) {
  int xh = -1;
  int yh = -1;
  int rv = 0; // ui->mouseover


  if      (fx > -.95 && fx < -.55) xh = 0;
  else if (fx > -.45 && fx < -.05) xh = 1;
  else if (fx >  .05 && fx <  .45) xh = 2;
  else if (fx >  .55 && fx <  .95) xh = 3;

  if      (fy > -.70 && fy < -.55) yh = 0;
  else if (fy > -.45 && fy < -.30) yh = 1;
  else if (fy > -.20 && fy < -.05) yh = 2;
  else if (fy >  .05 && fy <  .20) yh = 3;
  else if (fy >  .30 && fy <  .45) yh = 4;
  else if (fy >  .55 && fy <  .70) yh = 5;

  if (xh != -1 && yh != -1) {
    rv = 1 + yh * 4 + xh;
    const float ccx = -.95 + .5 * xh;
    if (fx > ccx && fx < ccx + .05) *tri = -1;
    else if (fx > ccx + .35 && fx < ccx + .4) *tri = 1;
  }
  // NB. no paging here -- 0 <= rc <= 24
  return rv;
}

static void
advanced_config_screen(PuglView* view)
{
  B3ui* ui = (B3ui*)puglGetHandle(view);
  const GLfloat mat_w[] = {1.0, 1.0, 1.0, 1.0};
  const GLfloat mat_b[] = {0.0, 0.0, 0.0, 1.0};
  const GLfloat mat_g[] = {0.6, 0.6, 0.6, 1.0};
  const GLfloat mat_tb[] = { 0.2, 0.2, 0.2, 1.0 };
  const GLfloat mat_bg[] = { 0.1, 0.1, 0.1, 1.0 };
  const GLfloat mat_hv[] = { 0.3, 0.3, 0.3, 1.0 };
  int mouseover = ui->mouseover;

  unity_box(view, -1.f, 1.f, -1.f, -.8f, mat_tb);
  unity_box(view, -1.f, 1.f, -.8, .8f, mat_bg);

  { // active tab
    float tabx = (-.8 + ui->cfgtab * .4) - .175;
    unity_box(view, tabx, tabx + .35f, -.96f, -.8f, mat_bg);
  }

  if (mouseover > 24 && mouseover < 32 && ui->cfgtab + 25 != mouseover) {
    // hovered tab
    float tabx = (-.8 + (mouseover - 25) * .4) - .175;
    unity_box(view, tabx, tabx + .35f, -.96f, -.8f, mat_hv);
  }

  render_title(view, "Advanced Config", 19.0, 7.72, 0.1, mat_w, TA_RIGHT_BOTTOM);

#define TABTXTCOLOR(x) ((ui->cfgtab == x) ? mat_w : mat_hv)
  render_title(view, "Tuning",           -.8/SCALE, -7.4, 0.5, TABTXTCOLOR(0), TA_CENTER_MIDDLE);
  render_title(view, "Vibrato & Perc.",  -.4/SCALE, -7.4, 0.5, TABTXTCOLOR(1), TA_CENTER_MIDDLE);
  render_title(view, "Analog Model",       0/SCALE, -7.4, 0.5, TABTXTCOLOR(2), TA_CENTER_MIDDLE);
  render_title(view, "Leslie Config",     .4/SCALE, -7.4, 0.5, TABTXTCOLOR(3), TA_CENTER_MIDDLE);
  render_title(view, "Leslie Filters",    .8/SCALE, -7.4, 0.5, TABTXTCOLOR(4), TA_CENTER_MIDDLE);

  if (mouseover > 0 && mouseover <= 24) {
    const int ccc = ui->cfgtab * 24 + mouseover - 1;
    if (ccc < MAXCFG && ui->cfgvar[ccc].d && ui->cfgvar[ccc].d->desc) {
      render_small_text(view, "Description (see the manual for complete info):", -23.75, 7.5, 0.5, mat_g, TA_LEFT_BOTTOM);
      render_small_text(view, ui->cfgvar[ccc].d->desc, -23.75, 8.22, 0.5, mat_w, TA_LEFT_BOTTOM);
    }
  }

  if (ui->cfgtab == 0) {
    // first tab 'special'
    float yto = -.02 / SCALE;
    render_small_text(view,
	"setBfree is a 'Tonewheel Organ Construction Kit' with over 1000 configurable parameters.",
	0, yto, 0.5, mat_w, TA_CENTER_BOTTOM); yto+=.75;
    render_small_text(view,
	"This dialog only exposes some more common 'advanced' settings. Use a config file for complete control.",
	0, yto, 0.5, mat_w, TA_CENTER_BOTTOM); yto+=.75;
    render_small_text(view,
	"NOTE: changing any of these parameters re-initializes the synth.",
	0, yto, 0.5, mat_w, TA_CENTER_BOTTOM); yto+=.75; yto+=.5;
    render_small_text(view,
	"Shift + Click on an element to restore its setting to the default value.",
	0, yto, 0.5, mat_w, TA_CENTER_BOTTOM); yto+=.75;
    render_small_text(view,
	"Click and drag on the button for large changes, click on the arrows for stepwise adjustment.",
	0, yto, 0.5, mat_w, TA_CENTER_BOTTOM); yto+=.75;
    render_small_text(view,
	"Hold Ctrl to alter the granularity (fine graind drag; or large click-steps).",
	0, yto, 0.5, mat_w, TA_CENTER_BOTTOM); yto+=.75;

  }

  if (ui->reinit) {
    mouseover = 0;
    render_title(view, "[busy, please wait]", 0.0, 7.72, 0.1, mat_w, TA_CENTER_BOTTOM);
  }

  int xh, yh;
  for (xh = 0; xh < 4; ++xh) {
    for (yh = 0; yh < 6; ++yh) {
      const int ccm = yh * 4 + xh;
      const int ccc = yh * 4 + xh + ui->cfgtab * 24;
      if (!ui->cfgvar[ccc].d) continue;
      const float ccx = -.95 + .5 * xh;
      const float ccy = -.70 + .25 * yh;
      render_cfg_button(view, ccc, ccx, ccx+.4, ccy, ccy+ .15,
	  mouseover == ccm + 1, ui->cfgtriover, ui->cfgdrag == ccm + 1);
    }
  }

  // special reset buttons @ 21, 22
  if (ui->cfgtab == 0) {
    assert(!ui->cfgvar[21].d);
    assert(!ui->cfgvar[22].d);
    const float invaspect = 320. / 960.;
    float x0, y0;

    x0 = -.95 + .5 * 1;
    y0 = -.70 + .25 * 5;
    unity_button(view, x0, x0+.4, y0, y0+ .15, mouseover == 22);
    render_small_text(view, "Factory Reset",
	(x0 +.2) / SCALE, invaspect * (y0 + .075) / SCALE,
	0.5,  mouseover == 22 ? mat_b : mat_w, TA_CENTER_MIDDLE);

    x0 = -.95 + .5 * 2;
    y0 = -.70 + .25 * 5;
    unity_button(view, x0, x0+.4, y0, y0+ .15, mouseover == 23);
    render_small_text(view, "Reset, keep MIDI & Program",
	(x0 +.2) / SCALE, invaspect * (y0 + .075) / SCALE,
	0.5,  mouseover == 23 ? mat_b : mat_w, TA_CENTER_MIDDLE);
  }
}

#define SCOORD(X,Y) (X)/SCALE, (Y) * invaspect / SCALE, 0.5

static void helpscreentext(PuglView* view)
{
  const GLfloat mat_w[] = {1.0, 1.0, 1.0, 1.0};
  const GLfloat mat_r[] = {1.0, 0.0, 0.0, 1.0};
  const float invaspect = 320. / 960.;
  float yto, xm0, xm1, xm2;

  render_title(view, "DSP Tonewheel Organ", SCOORD(-.9, -.82), mat_w, TA_LEFT_BOTTOM);

  yto = -.61; xm0 = -.79;
  render_gl_text(view, "Interaction with the synth is done", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "with the mouse: either via click+drag", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "or for fine-grained control using the", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "scroll-wheel on an element.", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  yto+=.05;
  render_gl_text(view, "All actions can be triggered as well", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "by using MIDI-CC messages.", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  yto+=.05;
  render_gl_text(view, "The communication is bidirectional:", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "GUI updates will be sent as feedback", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "to the MIDI output, incoming MIDI-CC", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "message update the GUI.", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  yto+=.05;
  render_gl_text(view, "In Key-control mode the PC-keyboard", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "can be used to control the elements,", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "overriding the default key-bindings.", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM, FS_MEDIUM, 0); yto+=.10;

  yto+=.02;
  render_gl_text(view, "http://setbfree.org/", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM, FS_SMALL, 0);

  //
  render_gl_text(view, "Keyboard Shortcuts", SCOORD(.22, -.82), mat_w, TA_CENTER_BOTTOM, FS_MEDIUM, 0);

  yto = -.61; xm0 = .27;
  render_gl_text(view, "P", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "Recall MIDI Program:", SCOORD(xm0 - .01, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "<Shift>P", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "Store MIDI Program:", SCOORD(xm0 - .01, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  yto+=.13;
  render_gl_text(view, "M", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "Display CC-map:", SCOORD(xm0 - .01, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "<Ctrl>Btn2", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "(Re) Assign MIDI-CC:", SCOORD(xm0 - .01, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "(hold <Shift> to invert mapped value)", SCOORD(xm0 - .06, yto), mat_w, TA_CENTER_BOTTOM, FS_SMALL, 0); yto+=.10;
  yto+=.08;

  render_gl_text(view, "~", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "Config Editor:", SCOORD(xm0 - .01, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  yto+=.08;

  render_gl_text(view, "<Shift>L", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "Load .pgm/.cfg:", SCOORD(xm0 - .01, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "<Shift>C", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "Export .cfg file:", SCOORD(xm0 - .01, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "<Shift>V", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "Export .pgm file:", SCOORD(xm0 - .01, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  yto+=.08;

  render_gl_text(view, "<Tab>", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "Toggle Key Control:", SCOORD(xm0 - .01, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "?", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "Toggle Help Text:", SCOORD(xm0 - .01, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0); yto+=.10;
  render_gl_text(view, "(In \"Key-Control\" mode '?' show the keyboard map)", SCOORD(xm0 - .06, yto), mat_w, TA_CENTER_BOTTOM, FS_SMALL, 0);

  //
  render_gl_text(view, "3D Navigation", SCOORD(.95, -.82), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0);

  yto = -.66; xm0 = .85; xm1 = .9; xm2 = .95;
  render_gl_text(view, "J", SCOORD(xm1, yto), mat_r, TA_CENTER_BOTTOM, FS_MEDIUM, 0); yto+=.15;
  render_gl_text(view, "H", SCOORD(xm0, yto), mat_r, TA_CENTER_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "L", SCOORD(xm2, yto), mat_r, TA_CENTER_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "Move:", SCOORD(xm0 - .03, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0);
  yto+=.15;
  render_gl_text(view, "K", SCOORD(xm1, yto), mat_r, TA_CENTER_BOTTOM, FS_MEDIUM, 0);

  yto+=.25;
  render_gl_text(view, "Z", SCOORD(xm0, yto), mat_r, TA_CENTER_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "C", SCOORD(xm2, yto), mat_r, TA_CENTER_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "Turn (Z Axis):", SCOORD(xm0 - .03, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0);

  yto+=.25;

  render_gl_text(view, "W", SCOORD(xm1, yto), mat_r, TA_CENTER_BOTTOM, FS_MEDIUM, 0); yto+=.15;
  render_gl_text(view, "A", SCOORD(xm0, yto), mat_r, TA_CENTER_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "D", SCOORD(xm2, yto), mat_r, TA_CENTER_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "Tilt (X,Y Axis):", SCOORD(xm0 - .03, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0);
  yto+=.15;
  render_gl_text(view, "X", SCOORD(xm1, yto), mat_r, TA_CENTER_BOTTOM, FS_MEDIUM, 0);

  yto+=.20;
  render_gl_text(view, "+", SCOORD(xm0, yto), mat_r, TA_CENTER_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "-", SCOORD(xm2, yto), mat_r, TA_CENTER_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "Zoom:", SCOORD(xm0 - .03, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0);

  yto+=.22;
  render_gl_text(view, "E", SCOORD(xm0, yto), mat_r, TA_CENTER_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "S", SCOORD(xm2, yto), mat_r, TA_CENTER_BOTTOM, FS_MEDIUM, 0);
  render_gl_text(view, "3D Presets:", SCOORD(xm0 - .03, yto), mat_w, TA_RIGHT_BOTTOM, FS_MEDIUM, 0);
}

static void keybindinghelp(PuglView* view)
{
  const float invaspect = 320. / 960.;
  const GLfloat mat_w[] = {1.0, 1.0, 1.0, 1.0};
  const GLfloat mat_g[] = { 0.1, 0.1, 0.1, 1.0 };
  const GLfloat mat_r[] = {1.0, 0.0, 0.0, 1.0};
  glLoadIdentity();

  render_title(view, "-- [Computer] Keyboard Control --", SCOORD(0, -.95), mat_w, TA_CENTER_MIDDLE);
  render_small_text(view, "Toggle keyboard-grab with <Tab>. When enabled, organ-controls can be modified with by keypresses. Currently a US-104 keyboard layout is assumed.", SCOORD(0, -.80), mat_w, TA_CENTER_MIDDLE);
  render_small_text(view, "[click or press '?' to close this help]", .2/ SCALE, .15 * invaspect / SCALE, 0.5, mat_w, TA_CENTER_BOTTOM);


  float yto, xm0, xm1;

  yto = -.45; xm0 = 0; xm1 = .4;
  unity_box(view, xm0-.05, xm1 + .05, yto -.25, yto+.25, mat_g);
  render_title(view, "Presets", SCOORD((xm0 + xm1)/2, yto-.15), mat_w, TA_CENTER_MIDDLE);
  render_title(view, "1 - 9", SCOORD((xm0 + xm1)/2, yto + .05), mat_r, TA_CENTER_BOTTOM);
  render_small_text(view, "(recall progam 1-9)", SCOORD((xm0 + xm1)/2, yto + .15), mat_w, TA_CENTER_BOTTOM);

  yto = -.45; xm0 = -.9; xm1 = -.15;
  unity_box(view, xm0-.05, xm1 + .05, yto -.25, yto+.7, mat_g);
  render_title(view, "Drawbars", SCOORD((xm0 + xm1)/2, yto-.15), mat_w, TA_CENTER_MIDDLE);
  render_small_text(view, "first select which drawbars are affected:", SCOORD((xm0 + xm1)/2, yto), mat_w, TA_CENTER_BOTTOM); yto+=.1;
  render_title(view, "[  ]  \\ ", SCOORD(xm1, yto), mat_r, TA_RIGHT_BOTTOM);
  render_small_text(view, "(select: pedal, lower, upper)", SCOORD(xm0, yto), mat_w, TA_LEFT_BOTTOM);
  yto+=.15;
  render_small_text(view, "Drawbars are mapped left to right:", SCOORD((xm0 + xm1)/2, yto), mat_w, TA_CENTER_BOTTOM); yto+=.15;
  render_title(view, "Q W E R T Y U I O", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM);
  render_small_text(view, "(push drawbar in)", SCOORD(xm1, yto), mat_w, TA_RIGHT_BOTTOM); yto+=.2;
  render_title(view, "A S D F G H J K L", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM);
  render_small_text(view, "(pull drawbar out)", SCOORD(xm1, yto), mat_w, TA_RIGHT_BOTTOM);

  yto = -.45; xm0 = .55; xm1 = .9;
  unity_box(view, xm0-.05, xm1 + .05, yto -.25, yto+.7, mat_g);
  render_title(view, "Dials", SCOORD((xm0 + xm1)/2, yto-.15), mat_w, TA_CENTER_MIDDLE);
  render_title(view, "- =", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM);
  render_small_text(view, "(Volume)", SCOORD(xm1,  yto), mat_w, TA_RIGHT_BOTTOM); yto+=.2;
  render_title(view, "; '", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM);
  render_small_text(view, "(Reverb)", SCOORD(xm1,  yto), mat_w, TA_RIGHT_BOTTOM); yto+=.2;
  render_title(view, "< >", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM);
  render_small_text(view, "(Overdrive)", SCOORD(xm1, yto), mat_w, TA_RIGHT_BOTTOM); yto+=.2;
  render_title(view, ", .", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM);
  render_small_text(view, "(Vibrator/Chorus)", SCOORD(xm1, yto), mat_w, TA_RIGHT_BOTTOM);

  yto = .625; xm0 = -.9; xm1 = -.15;
  unity_box(view, xm0-.05, xm1 + .05, yto -.25, yto+.35, mat_g);
  render_title(view, "Switches", SCOORD((xm0 + xm1)/2, yto-.15), mat_w, TA_CENTER_MIDDLE);
  render_small_text(view, "Switches are mapped left-to right as seen on the organ", SCOORD((xm0 + xm1)/2, yto), mat_w, TA_CENTER_BOTTOM); yto+=.075;
  render_small_text(view, "to the lower row of the keyboard Z-M:", SCOORD((xm0 + xm1)/2, yto), mat_w, TA_CENTER_BOTTOM); yto+=.175;
  render_title(view, "Z X C V B N M", SCOORD(xm0, yto), mat_r, TA_LEFT_BOTTOM);
  render_small_text(view, "(overdrive, 2 x vibratos, 4 x percussion)", SCOORD(xm1,  yto), mat_w, TA_RIGHT_BOTTOM);

  yto = .775; xm0 = 0; xm1 = .9;
  unity_box(view, xm0-.05, xm1 + .05, yto -.25, yto+.20, mat_g);
  render_title(view, "Leslie", SCOORD((xm0 + xm1)/2, yto-.15), mat_w, TA_CENTER_MIDDLE);

  render_title(view, "<Space>", SCOORD(xm0 + (xm1-xm0)/4, yto), mat_r, TA_CENTER_BOTTOM);
  render_title(view, "<Shift>+<Space>", SCOORD(xm0 + (xm1-xm0)*3/4, yto), mat_r, TA_CENTER_BOTTOM);
  yto+=.10;
  render_small_text(view, "(linked horn + baffle; iterate 3 states)", SCOORD(xm0 + (xm1-xm0)/4, yto), mat_w, TA_CENTER_BOTTOM);// yto+=.2;
  render_small_text(view, "(iterate over all 9 states)", SCOORD(xm0 + (xm1-xm0)*3/4, yto), mat_w, TA_CENTER_BOTTOM);
}

/**
 */

static void reset_state_ccbind(PuglView* view) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  if (ui->uiccbind >= 0) {
    ui->uiccbind = -1;
    forge_message_kv(ui, ui->uris.sb3_uimccset, 0, "off");
  }
  puglPostRedisplay(view);
}

static void reset_state(PuglView* view) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  ui->dndid = -1;
  ui->pgm_sel = -1;
  ui->dir_sel = -1;
  ui->dir_scrollgrab = NOSCROLL;
  ui->dir_scroll = 0;
  ui->dir_hidedotfiles = 0;
  ui->mouseover = 0;
  ui->cfgtriover = 0;
  ui->cfgdrag = 0;
  reset_state_ccbind(view);
}


/**
 * main display fn
 */

static void
onDisplay(PuglView* view)
{
  int i;
  B3ui* ui = (B3ui*)puglGetHandle(view);

  const GLfloat no_mat[] = { 0.0, 0.0, 0.0, 1.0 };
  const GLfloat mat_specular[] = { 1.0, 1.0, 1.0, 1.0 };
  const GLfloat no_shininess[] = { 128.0 };
  const GLfloat high_shininess[] = { 5.0 };

  const GLfloat mat_organ[] = { 0.5, 0.25, 0.1, 1.0 };
  const GLfloat mat_dial[] = { 0.1, 0.1, 0.1, 1.0 };
  const GLfloat mat_lever[] = { 0.3, 0.3, 0.3, 1.0 };
  const GLfloat mat_switch[] = { 0.8, 0.8, 0.74, 1.0 };
  const GLfloat glow_red[] = { 1.0, 0.0, 0.00, 1.0 };
  const GLfloat mat_drawbar_white[] = { 0.91, 0.91, 0.91, 1.0 };
  const GLfloat mat_drawbar_brown[] = { 0.39, 0.25, 0.1, 1.0 };
  const GLfloat mat_drawbar_black[] = { 0.09, 0.09, 0.09, 1.0 };
  const GLfloat mat_w[] = {1.0, 1.0, 1.0, 1.0};

  if (!ui->initialized) {

#ifndef BUILTINFONT
#  ifdef __APPLE__
    char fontfilepath[1024] = FONTFILE;
    uint32_t i;
    uint32_t ic = _dyld_image_count();
    for (i = 0; i < ic; ++i) {
      if (strstr(_dyld_get_image_name(i), "/b_synthUI.dylib") || strstr(_dyld_get_image_name(i), "/setBfreeUI")) {
	char *tmp = strdup(_dyld_get_image_name(i));
	strcpy(fontfilepath, dirname(tmp));
	free(tmp);
	strcat(fontfilepath, (const char*)"/");
	tmp = strdup(FONTFILE);
	strcat(fontfilepath, basename(tmp));
	free(tmp);
	break;
      }
    }
#  else
    const char *fontfilepath = FONTFILE;
#  endif
#endif

    /* initialization needs to happen from event context
     * after pugl set glXMakeCurrent() - this /should/ otherwise
     * be done during initialization()
     */
    ui->initialized = 1;
    setupOpenGL();
    initMesh(ui->view);
    setupLight();
    initTextures(ui->view);
#ifndef BUILTINFONT
    ui->font_big = ftglCreateBufferFont(fontfilepath);
    ui->font_medium = ftglCreateBufferFont(fontfilepath);
    ui->font_small = ftglCreateBufferFont(fontfilepath);
#else
    ui->font_big = ftglCreateBufferFontMem(VeraBd_ttf, VeraBd_ttf_len);
    ui->font_medium = ftglCreateBufferFontMem(VeraBd_ttf, VeraBd_ttf_len);
    ui->font_small = ftglCreateBufferFontMem(VeraBd_ttf, VeraBd_ttf_len);
#endif

    ftglSetFontFaceSize(ui->font_big, 36, 72);
    ftglSetFontCharMap(ui->font_big, ft_encoding_unicode);
    ftglSetFontFaceSize(ui->font_medium, 30, 72);
    ftglSetFontCharMap(ui->font_medium, ft_encoding_unicode);
    ftglSetFontFaceSize(ui->font_small, 20, 72);
    ftglSetFontCharMap(ui->font_small, ft_encoding_unicode);
  }
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  onReshape(view, ui->width, ui->height); // XXX

  if (ui->popupmsg) {
    if (ui->queuepopup) {
      reset_state(view);
      onReshape(view, ui->width, ui->height);
      ui->queuepopup = 0;
    }
    unity_box(view, -1.0, 1.0, -.12, .12, mat_dial);
    render_title(view, ui->popupmsg, 0, 0, 0.1, mat_drawbar_white, TA_CENTER_MIDDLE);
    if (ui->pendingmode) {
      render_text(view, "Press <enter> to confirm, <ESC> to abort, or press button.", 0, 7.0, 0, TA_CENTER_MIDDLE);
      gui_button(view, BTNLOC_NO, HOVER_NO, "No");
      gui_button(view, BTNLOC_YES, HOVER_YES, "Yes");
    } else {
      render_text(view, "Press <enter> or <ESC> to continue.", 0, 7.0, 0, TA_CENTER_MIDDLE);
      gui_button(view, BTNLOC_OK, HOVER_OK, "Ok");
    }
    return;
  }

  if (ui->textentry_active) {
    txtentry_render(view);
    return;
  }

  if (ui->displaymode == 1) {
    /* Help screen */
    const float invaspect = 320. / 960.;
    glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_w);
    glMaterialfv(GL_FRONT, GL_AMBIENT, mat_w);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    glBindTexture(GL_TEXTURE_2D, ui->texID[14]);
    glBegin(GL_QUADS);
    glColor3f(1.0, 1.0, 1.0);
    glTexCoord2f (0.0, 0.0); glVertex3f(-1, -invaspect, 0);
    glTexCoord2f (0.0, 1.0); glVertex3f(-1,  invaspect, 0);
    glTexCoord2f (1.0, 1.0); glVertex3f( 1,  invaspect, 0);
    glTexCoord2f (1.0, 0.0); glVertex3f( 1, -invaspect, 0);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    helpscreentext(view);
    return;
  } else if (ui->displaymode == 9) {
    keybindinghelp(view);
    return;
  } else if (ui->displaymode == 2 || ui->displaymode == 3) {
    /* midi program list */
    int i;
    const float invaspect = 320. / 960.;
    const float w = 1.0/2.8 * 22.0 * SCALE;
    const float h = 2.0/24.0 * 22.0 * SCALE;

    render_title(view, (ui->displaymode == 2) ? "recall" : "store", 16.5, -.75, 0.1, mat_drawbar_white, TA_LEFT_BOTTOM);
    gui_button(view, BTNLOC_CANC2, HOVER_CANC2, "Cancel");

    for (i=0; i < 128; i++) {
      char txt[40];
      sprintf(txt, "p%3d: %s", i+1, ui->midipgm[i]);
      float x = -1.1 +  (i/24)/2.7; // 0..5
      float y = -1.0 +  (i%24)/12.0; // 0..23

      x *= 22.0; y *= 22.0;

      const float bx = x * SCALE;
      const float by = y * SCALE;

      GLfloat mat_x[] = {0.1, 0.1, 0.1, 1.0};
      if (i == ui->pgm_sel) {
	if (ui->displaymode == 2) mat_x[2] = .6;
	else mat_x[0] = .6;
      }
      else if (i%2) {
	mat_x[0] = .125;
	mat_x[1] = .125;
	mat_x[2] = .125;
      }
      unity_box(view, bx, bx+w, by, by+h, mat_x);
      y *= invaspect;
      render_text(view, txt, x, y, .1f, TA_LEFT_TOP);
    }
    if (ui->pgm_sel >= 0) {
      char *t0, *t1; int ln = 0;
      t0 = ui->mididsc[ui->pgm_sel];
      while (*t0 && (t1 = strchr(t0, '\n'))) {
	*t1='\0';
	render_text(view, t0, 16.5, ++ln*0.5 - .1 , .1f, TA_LEFT_BOTTOM);
	*t1='\n';
	t0=t1+1;
      }
    }
    return;
  } else if (IS_FILEBROWSER(ui)) {
    int i;
    const float invaspect = 320. / 960.;
    const float w = 1.0/2.8 * 22.0 * SCALE;
    const float h = 2.0/24.0 * 22.0 * SCALE;

    switch(ui->displaymode) {
      case 4:
	render_title(view, "open .pgm or .cfg",  .2/SCALE, invaspect * .78/SCALE, 0.1, mat_drawbar_white, TA_RIGHT_TOP);
	render_text(view, "Note: loading a .cfg will re-initialize the organ.", -20.0, 7.75, 0.0, TA_LEFT_BOTTOM);
#ifdef JACK_DESCRIPT
	if (ui->bundlePath && strcmp(ui->curdir, ui->bundlePath)) {
	  gui_button(view, BTNLOC_RSRC, HOVER_RSRC, "Go To /CFG");
	}
#endif
	gui_button(view, BTNLOC_CANC3, HOVER_CANC3, "Cancel");
	break;
      case 5:
	render_title(view, ".cfg", -.92/SCALE, invaspect * .85/SCALE, 0.1, mat_drawbar_white, TA_CENTER_MIDDLE);
	render_title(view, "save", -.92/SCALE, invaspect * .75/SCALE, 0.1, mat_drawbar_white, TA_CENTER_MIDDLE);
	gui_button(view, BTNLOC_SAVE, HOVER_SAVE, "Save");
	gui_button(view, BTNLOC_CANC, HOVER_CANC, "Cancel");
#ifdef JACK_DESCRIPT
	if (ui->defaultConfigFile) {
	  gui_button(view, BTNLOC_DFLT, HOVER_DFLT, "Save as default");
	}
#endif
	render_text(view, "select a file or press OK or <enter> to create new.", -20.0, 7.75, 0.0, TA_LEFT_BOTTOM);
	break;
      case 6:
	render_title(view, ".pgm", -.92/SCALE, invaspect * .85/SCALE, 0.1, mat_drawbar_white, TA_CENTER_MIDDLE);
	render_title(view, "save", -.92/SCALE, invaspect * .75/SCALE, 0.1, mat_drawbar_white, TA_CENTER_MIDDLE);
	gui_button(view, BTNLOC_SAVE, HOVER_SAVE, "Save");
	gui_button(view, BTNLOC_CANC, HOVER_CANC, "Cancel");
#ifdef JACK_DESCRIPT
	if (ui->defaultConfigFile) {
	  gui_button(view, BTNLOC_DFLT, HOVER_DFLT, "Save as default");
	}
#endif
	render_text(view, "select a file or press OK or <enter> to create new.", -20.0, 7.75, 0.0, TA_LEFT_BOTTOM);
	break;
      default:
	break;
    }

    render_text(view, "Path:", -20.0, 7.0, 0.0, TA_LEFT_BOTTOM);
    render_text(view, ui->curdir, -18.25, 7.0, 0.0, TA_LEFT_BOTTOM);

    float xscolloff = 0;
    if (ui->dirlistlen > 120) {
      GLfloat mat_sbg[] = {0.1, 0.1, 0.1, 1.0};
      GLfloat mat_sfg[] = {0.1, 0.9, 0.15, 1.0};
      if (ui->mouseover & HOVER_SCROLLBAR || ui->dir_scrollgrab != NOSCROLL) {
	mat_sbg[0] = mat_sbg[1] = mat_sbg[2] = 0.15;
	mat_sfg[0] = 0.4; mat_sfg[1] = 1.0; mat_sfg[2] = 0.4;
      }
      unity_box(view, SCROLLBAR, mat_sbg);
      int pages = (ui->dirlistlen / 20);
      float ss = 1.6 / (float)pages;
      float sw = 5.0 * ss;
      float sx = ui->dir_scroll * ss - .8;
      unity_box(view, sx, sx+sw, 0.63, 0.695, mat_sfg);
      unity_tri(view, sx, 0.63, 0.695, mat_sbg);
      unity_tri(view, sx+sw, 0.695, 0.63, mat_sbg);
      xscolloff = ui->dir_scroll / 2.7;
    }

    for (i=0; i < ui->dirlistlen; i++) {
      char txt[30];
      snprintf(txt, 24, "%s", ui->dirlist[i]);
      txt[24]='\0';
      if (strlen(ui->dirlist[i]) > 24) strcat(txt, "...");

      float x = -1.1 + (i/20)/2.7; // 0..5
      float y = -1.0 + (i%20)/12.0; // 0..19
      x -= xscolloff;

      x *= 22.0; y *= 22.0;

      const float bx = x * SCALE;
      const float by = y * SCALE + .02;

      GLfloat mat_x[] = {0.1, 0.1, 0.1, 1.0};
      if (i == ui->dir_sel) {
	mat_x[2] = .6;
      }
      else if (i%2) {
	mat_x[0] = .125;
	mat_x[1] = .125;
	mat_x[2] = .125;
      }
      if (ui->dirlist[i][strlen(ui->dirlist[i])-1] != '/') {
	mat_x[0] += .1;
	mat_x[1] += .1;
	mat_x[2] += .1;
      }
      unity_box(view, bx, bx+w, by-h, by, mat_x);

      y *= invaspect;
      render_text(view, txt, x, y, .1f, TA_LEFT_BOTTOM);
    }
    return;
  } else if (ui->displaymode == 7) {

    const GLfloat mat_x[] = {0.4, 0.4, 0.4, 1.0};
    const float invaspect = 320. / 960.;
    glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_x);
    glMaterialfv(GL_FRONT, GL_AMBIENT, mat_x);
    glMaterialfv(GL_FRONT, GL_EMISSION, mat_x);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, ui->texID[17]);
    glBegin(GL_QUADS);
    glColor3f(1.0, 1.0, 1.0);
    glTexCoord2f (0.0, 0.0); glVertex3f(-1, -invaspect, 0);
    glTexCoord2f (0.0, 1.0); glVertex3f(-1,  invaspect, 0);
    glTexCoord2f (1.0, 1.0); glVertex3f( 1,  invaspect, 0);
    glTexCoord2f (1.0, 0.0); glVertex3f( 1, -invaspect, 0);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    menu_button(view, MENU_PGML,  18, HOVER_MPGML, "Recall PGM");
    menu_button(view, MENU_PGMS,  19, HOVER_MPGMS, "Store PGM");
    menu_button(view, MENU_LOAD,  20, HOVER_MLOAD, "Load pgm or cfg File");
    menu_button(view, MENU_SAVEP, 21, HOVER_MSAVEP, "Export Program File");
    menu_button(view, MENU_SAVEC, 22, HOVER_MSAVEC, "Export Config File");
    menu_button(view, MENU_ACFG,  23, HOVER_MACFG, "Advanced Config");
    menu_button(view, MENU_CANC, -1,  HOVER_MCANC, "Close");
    return;
  } else if (ui->displaymode == 8) {
    advanced_config_screen(view);
    menu_button(view, MENU_CANC, -1,  HOVER_MCANC, "Close");
    return;
  }

  /* main organ */

  /** step 0 - help button **/

  if (1) {
    glPushMatrix();
    glLoadIdentity();
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_drawbar_white);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_drawbar_white);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_drawbar_white);
    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, ui->texID[16]);
    glBegin(GL_QUADS);
    glTexCoord2f (0.0,  0.0); glVertex3f(0.92, -.25, 0);
    glTexCoord2f (0.0,  1.0); glVertex3f(0.92, -.16, 0);
    glTexCoord2f (1.0, 1.0); glVertex3f(1.1,  -.16, 0);
    glTexCoord2f (1.0, 0.0); glVertex3f(1.1,  -.25, 0);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glPopMatrix();
  }

  glPushMatrix();
  glLoadIdentity();
  glTranslatef(1.07, -.21, 0.0f);
  glScalef(SCALE, SCALE, SCALE);
  glScalef(.6, .6, 1.0);
  glRotatef(180, 1, 0, 0);

  glMaterialfv(GL_FRONT, GL_AMBIENT, mat_dial);
  glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_dial);
  glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);

  drawMesh(view, OBJ_PUSHBUTTON, 1);
  glPopMatrix();

  render_title(view, "?", 1.07/SCALE, -.21/SCALE, 0.012, mat_drawbar_white, TA_CENTER_MIDDLE);

  /** step 1 - draw background -- fixed objects **/

  glPushMatrix();
  glLoadIdentity();
  glRotatef(180, 1, 0, 0);
  glScalef(SCALE, SCALE, SCALE);

  /* organ - background */
  glMaterialfv(GL_FRONT, GL_AMBIENT, no_mat);
  glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_organ);
  glMaterialfv(GL_FRONT, GL_SPECULAR, no_mat);
  glMaterialfv(GL_FRONT, GL_SHININESS, no_shininess);
  glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);

  glEnable(GL_TEXTURE_2D);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
  glBindTexture(GL_TEXTURE_2D, ui->texID[0]);

  drawMesh(view, OBJ_ORGANBG, 1);

  glDisable(GL_TEXTURE_2D);
  glPopMatrix();

  /* insets */
  glPushMatrix();
  glLoadIdentity();
  glRotatef(180, 1, 0, 0);

  glScalef(SCALE, SCALE, SCALE);
  glTranslatef(22.875, -1.49067, 0);

  glMaterialfv(GL_FRONT, GL_AMBIENT, mat_dial);
  glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_dial);
  glMaterialfv(GL_FRONT, GL_SPECULAR, mat_specular);
  glMaterialfv(GL_FRONT, GL_SHININESS, high_shininess);
  glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);

  /* right ctrl */
  glEnable(GL_TEXTURE_2D);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
  glBindTexture(GL_TEXTURE_2D, ui->texID[10]);
  drawMesh(view, OBJ_INSET, 1);
  glDisable(GL_TEXTURE_2D);

  /* left ctrl */
  glTranslatef(-22.875 * 2.0, 0, 0);
  glEnable(GL_TEXTURE_2D);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
  glBindTexture(GL_TEXTURE_2D, ui->texID[11]);
  drawMesh(view, OBJ_INSET, 1);
  glDisable(GL_TEXTURE_2D);

  /* leslie drum box */
  glTranslatef(-2.8, -5.50485, 0);
  glEnable(GL_TEXTURE_2D);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
  glBindTexture(GL_TEXTURE_2D, ui->texID[12]);
  drawMesh(view, OBJ_GEARBOX, 1);
  glDisable(GL_TEXTURE_2D);

  /* leslie horn box */
  glTranslatef(5.6, 0, 0);
  glEnable(GL_TEXTURE_2D);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
  glBindTexture(GL_TEXTURE_2D, ui->texID[13]);
  drawMesh(view, OBJ_GEARBOX, 1);
  glDisable(GL_TEXTURE_2D);
  glPopMatrix();


  /** step 2 - draw /movable/ objects **/

  /* base material of moveable objects */
  glMaterialfv(GL_FRONT, GL_AMBIENT, no_mat);
  glMaterialfv(GL_FRONT, GL_SPECULAR, mat_specular);
  glMaterialfv(GL_FRONT, GL_SHININESS, high_shininess);

  for (i = 0; i < TOTAL_OBJ; ++i) {
    float y = ui->ctrls[i].y;

    if (ui->ctrls[i].type == OBJ_DRAWBAR) { /* drawbar */
      y += 6.0;
      y -= (float) vmap_val_to_midi(view, i) / 12.7;
    }

    glPushMatrix();
    glLoadIdentity();
    glScalef(SCALE, SCALE, SCALE);
    glTranslatef(ui->ctrls[i].x, y, 0.0f);
    glRotatef(180, 0, 1, 0);

    switch(ui->ctrls[i].type) {
      case OBJ_DIAL:
	glMaterialfv(GL_FRONT, GL_AMBIENT, mat_dial);
	glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_dial);
	glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);
	if (ui->ctrls[i].max == 0) {
	  glRotatef(
	      240.0 - (360.0 * rint(ui->ctrls[i].cur - ui->ctrls[i].min) / (1.0 + ui->ctrls[i].max - ui->ctrls[i].min))
	      , 0, 0, 1);
	} else {
	  glRotatef(
	      150.0 - (300.0 * rint(ui->ctrls[i].cur - ui->ctrls[i].min) / (ui->ctrls[i].max - ui->ctrls[i].min))
	      , 0, 0, 1);
	}
	break;
      case OBJ_SWITCH:
	glMaterialfv(GL_FRONT, GL_AMBIENT, mat_switch);
	glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_switch);
	if (ui->ctrls[i].cur == ui->ctrls[i].max) {
	  glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, glow_red);
	} else {
	  glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, no_mat);
	}
	glRotatef((vmap_val_to_midi(view, i) < 64 ? -12 : 12.0), 1, 0, 0); // XXX
	break;
      case OBJ_DRAWBAR:
	glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);
	switch(i) {
	  case 0:
	  case 1:
	  case 9:
	  case 10:
	  case 18:
	  case 19:
	    glMaterialfv(GL_FRONT, GL_AMBIENT, mat_drawbar_brown);
	    glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_drawbar_brown);
	    break;
	  case 4:
	  case 6:
	  case 7:
	  case 13:
	  case 15:
	  case 16:
	    glMaterialfv(GL_FRONT, GL_AMBIENT, mat_drawbar_black);
	    glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_drawbar_black);
	    break;
	  default:
	    glMaterialfv(GL_FRONT, GL_AMBIENT, mat_drawbar_white);
	    glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_drawbar_white);
	    break;
	}
	break;
      case OBJ_LEVER:
	glMaterialfv(GL_FRONT, GL_AMBIENT, mat_dial);
	glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_lever);
	glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);
	glRotatef(
	    (-40.0 + 80.0 * rint(ui->ctrls[i].cur - ui->ctrls[i].min) / (ui->ctrls[i].max - ui->ctrls[i].min))
	    , 0, 1, 0);
	break;
      default:
	break;
    }

    if (ui->ctrls[i].texID > 0) {
      switch(ui->ctrls[i].type) {
	case OBJ_SWITCH:
	  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	  break;
	default:
	  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
	  break;
      }
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, ui->texID[ui->ctrls[i].texID]);
    }

    drawMesh(view, ui->ctrls[i].type, 1);
    glDisable(GL_TEXTURE_2D);
    glPopMatrix();

    float x = ui->ctrls[i].x;
    if (i == 31) x += 2.8;
    if (i == 32) x -= 2.8;
    if (i < 20)  y -= 0.4;

    if (ui->uiccbind == i) {

      char bind_text[64];
      if (i == 31) {
	strcpy(bind_text, "move slider (8 step)");
      } else if (i == 32) {
	strcpy(bind_text, "move slider (3 step)");
      } else {
	strcpy(bind_text, "move slider");
      }

      if (ui->uiccflag & MFLAG_INV) {
	strcat (bind_text, " (inv)");
      }

      render_text(view, bind_text, x, y-.8, 1.65f, TA_CENTER_MIDDLE);
    } else if (ui->show_mm) {
      render_text(view, ui->ctrls[i].midinfo, x, y, 1.6f, TA_CENTER_MIDDLE);
    }
  }

  /** step 3 - keyboard & pedals **/

  piano_manual(view, 7.0, 1.8, ui->upper_key, &ui->active_keys[0]);
  piano_manual(view, 12.5, 3.9, ui->lower_key, &ui->active_keys[2]);
  piano_pedals(view, ui->pedal_key, ui->active_keys[4]);

  if (ui->keyboard_control & 1) {
    glPushMatrix();
    glLoadIdentity();
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_w);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_w);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_w);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glScalef(.001, .001, .001);
    glRotatef(240, 1, 0, 0);
    glTranslatef(0, 180, -110);
    char const * txt;
    switch(ui->keyboard_control >> 1) {
      case 1:
	txt = "KEYBOARD CONTROL LOWER MANUAL";
	break;
      case 2:
	txt = "KEYBOARD CONTROL PEDALS";
	break;
      default:
	txt = "KEYBOARD CONTROL UPPER MANUAL";
	break;
    }
    float bb[6];
    ftglGetFontBBox(ui->font_big, txt, -1, bb);
    glTranslatef((bb[3] - bb[0])/-2.0, 0, 0);
    ftglRenderFont(ui->font_big, txt, FTGL_RENDER_ALL);
    glPopMatrix();
  }
}

#define SKD(mode) if (ui->displaymode != mode) break;

static void
onKeyboard(PuglView* view, bool press, uint32_t key)
{
  B3ui* ui = (B3ui*)puglGetHandle(view);
  int queue_reshape = 0;
  if (!press) {
    return;
  }
  if (ui->textentry_active && !ui->popupmsg) {
    txtentry_handle(view, key);
    return;
  }

  if ((ui->displaymode == 0 || ui->displaymode == 9) && (ui->keyboard_control & 1) ANDNOANIM) {
    /* hardcoded US keyboard layout */
    switch (key) {
      case '?': // help screen
	if (ui->displaymode == 0) ui->displaymode = 9;
	else ui->displaymode = 0;
	queue_reshape = 1;
	break;

      case 9: // tab
	ui->keyboard_control &= ~1;
	queue_reshape = 1;
	break;

      case '\\':
	ui->keyboard_control = 1;
	queue_reshape = 1;
	break;
      case ']':
	ui->keyboard_control = 3;
	queue_reshape = 1;
	break;
      case '[':
	ui->keyboard_control = 5;
	queue_reshape = 1;
	break;

#define KEYADJ(ELEM, DELTA) { ui->dndval = ui->ctrls[(ELEM)].cur + (DELTA); processMotion(view, (ELEM), 0, 0); }
#define KEYADJ2(UPPER, LOWER, DELTA) \
	if (ui->keyboard_control == 3) KEYADJ(LOWER, DELTA) \
	if (ui->keyboard_control == 1) KEYADJ(UPPER, DELTA)

#define KEYSWITCH(ELEM) { assert(ui->ctrls[(ELEM)].type == OBJ_SWITCH); if (ui->ctrls[(ELEM)].cur == ui->ctrls[(ELEM)].max) ui->ctrls[(ELEM)].cur = ui->ctrls[(ELEM)].min; else ui->ctrls[(ELEM)].cur = ui->ctrls[(ELEM)].max; puglPostRedisplay(view); notifyPlugin(view, (ELEM)); }

      case 'q':
	if (ui->keyboard_control == 5) KEYADJ(18, 1);
	if (ui->keyboard_control == 3) KEYADJ( 9, 1);
	if (ui->keyboard_control == 1) KEYADJ( 0, 1);
	break;
      case 'a':
	if (ui->keyboard_control == 5) KEYADJ(18, -1);
	if (ui->keyboard_control == 3) KEYADJ( 9, -1);
	if (ui->keyboard_control == 1) KEYADJ( 0, -1);
	break;
      case 'w':
	if (ui->keyboard_control == 5) KEYADJ(19, 1);
	if (ui->keyboard_control == 3) KEYADJ(10, 1);
	if (ui->keyboard_control == 1) KEYADJ( 1, 1);
	break;
      case 's':
	if (ui->keyboard_control == 5) KEYADJ(19, -1);
	if (ui->keyboard_control == 3) KEYADJ(10, -1);
	if (ui->keyboard_control == 1) KEYADJ( 1, -1);
	break;
      case 'e': KEYADJ2( 2, 11,  1) break;
      case 'd': KEYADJ2( 2, 11, -1) break;
      case 'r': KEYADJ2( 3, 12,  1) break;
      case 'f': KEYADJ2( 3, 12, -1) break;
      case 't': KEYADJ2( 4, 13,  1) break;
      case 'g': KEYADJ2( 4, 13, -1) break;
      case 'y': KEYADJ2( 5, 14,  1) break;
      case 'h': KEYADJ2( 5, 14, -1) break;
      case 'u': KEYADJ2( 6, 15,  1) break;
      case 'j': KEYADJ2( 6, 15, -1) break;
      case 'i': KEYADJ2( 7, 16,  1) break;
      case 'k': KEYADJ2( 7, 16, -1) break;
      case 'o': KEYADJ2( 8, 17,  1) break;
      case 'l': KEYADJ2( 8, 17, -1) break;

      // percussion
      case 'v': KEYSWITCH(20); break;
      case 'b': KEYSWITCH(21); break;
      case 'n': KEYSWITCH(22); break;
      case 'm': KEYSWITCH(23); break;

      // vibrato on/off
      case 'x': KEYSWITCH(24); break;
      case 'c': KEYSWITCH(25); break;

      // overdrive on/off
      case 'z': KEYSWITCH(26); break;

      // overdrive character
      case '<': KEYADJ(27, -2); break;
      case '>': KEYADJ(27, 2); break;

      // vibrato/chorus mode dial
      case ',': KEYADJ(28, -1); break;
      case '.': KEYADJ(28, 1); break;

      // reverb
      case ';':  KEYADJ(30, -2); break;
      case '\'': KEYADJ(30, 2); break;

      // level
      case '-':  KEYADJ(29, -2); break;
      case '=': KEYADJ(29, 2); break;

      case ' ':
	{
	  int32_t val;

	  int hr = rint(ui->ctrls[32].cur); // UI: horn 0:chorale, 1:off, 2:tremolo
	  int bf = rint(ui->ctrls[31].cur); // UI: drum 0:chorale, 1:off, 2:tremolo

	  if (hr != 2) hr = (hr == 1) ? 0 : 1;
	  if (bf != 2) bf = (bf == 1) ? 0 : 1;

	  if (puglGetModifiers(view) & PUGL_MOD_SHIFT) {
	    val = bf * 15 + hr * 45;
	    val = (val + 15) % 120;
	  } else {
	    val = hr * 60;
	    val = (val + 60) % 180;
	  }

	  b3_forge_message(ui, obj_control[31], val);

	  hr = (val / 45) % 3; // horn 0:off, 1:chorale  2:tremolo
	  bf = (val / 15) % 3; // drum 0:off, 1:chorale  2:tremolo

	  if (hr != 2) hr = (hr == 1) ? 0 : 1;
	  if (bf != 2) bf = (bf == 1) ? 0 : 1;

	  ui->ctrls[32].cur = hr; // UI: horn 0:chorale, 1:off, 2:tremolo
	  ui->ctrls[31].cur = bf; // UI: drum 0:chorale, 1:off, 2:tremolo
	  puglPostRedisplay(ui->view);
	}
	break;

      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
	forge_message_int(ui, ui->uris.sb3_midipgm, key - 49);
	puglPostRedisplay(view);
	break;
    }
  }
  else switch (key) {
    case 'a': SKD(0)
      if (ui->rot[0] > -55) { ui->rot[0] -= 5; queue_reshape = 1; }
      break;
    case 'd': SKD(0)
      if (ui->rot[0] <  55) { ui->rot[0] += 5; queue_reshape = 1; }
      break;
    case 'w': SKD(0)
      if (ui->rot[1] > -80) { ui->rot[1] -= 5; queue_reshape = 1; }
      break;
    case 'x': SKD(0)
      if (ui->rot[1] <  0)  { ui->rot[1] += 5; queue_reshape = 1; }
      break;
    case 'z': SKD(0)
      if (ui->rot[2] > -35) { ui->rot[2] -= 5; queue_reshape = 1; }
      break;
    case 'c': SKD(0)
      if (ui->rot[2] <  35) { ui->rot[2] += 5; queue_reshape = 1; }
      break;
    case '+': SKD(0)
      if (ui->scale < 1.5) { ui->scale += .025; queue_reshape = 1; }
      break;
    case '-': SKD(0)
      if (ui->scale > 0.5) { ui->scale -= .025; queue_reshape = 1; }
      break;
    case 'h': SKD(0)
      if (ui->off[0] > -.9) { ui->off[0] -= .025; queue_reshape = 1; }
      break;
    case 'l': SKD(0)
      if (ui->off[0] <  .9) { ui->off[0] += .025; queue_reshape = 1; }
      break;
    case 'j': SKD(0)
      if (ui->off[1] > -.9) { ui->off[1] -= .025; queue_reshape = 1; }
      break;
    case 'k': SKD(0)
      if (ui->off[1] <  .9) { ui->off[1] += .025; queue_reshape = 1; }
      break;
    case 's': SKD(0)
      ui->rot[0] = ui->rot[1] = ui->rot[2] = 0.0;
      ui->scale =   0.875;
      ui->off[0] =  0.0;
      ui->off[1] = -0.22;
      ui->off[2] =  0.0;
      queue_reshape = 1;
      break;
    case 'e': SKD(0)
      ui->scale  = 0.9;
      ui->rot[0] = -15;
      ui->rot[1] = -20;
      ui->rot[2] =   0;
      ui->off[0] =  0.0f;
      ui->off[1] = -0.1f;
      ui->off[2] =  0.0f;
      queue_reshape = 1;
      break;
    case '~':
      if (ui->displaymode == 0 ANDNOANIM) ui->displaymode = 8;
      else if (ui->displaymode == 8) ui->displaymode = 0;
      queue_reshape = 1;
      reset_state(view);
      break;
    case 'p':
      if (ui->displaymode == 0 ANDNOANIM) ui->displaymode = 2;
      else if (ui->displaymode == 2) ui->displaymode = 0;
      queue_reshape = 1;
      reset_state(view);
      break;
    case '?':
      if (ui->displaymode == 0 ANDNOANIM) ui->displaymode = 1;
      else if (ui->displaymode == 1) ui->displaymode = 0;
      queue_reshape = 1;
      reset_state(view);
      break;
    case 'P':
      if (ui->displaymode == 0 ANDNOANIM) ui->displaymode = 3;
      else if (ui->displaymode == 3) ui->displaymode = 0;
      queue_reshape = 1;
      reset_state(view);
      break;
    case 'r':
      ui->highlight_keys = !ui->highlight_keys;
      queue_reshape = 1;
      break;
    case 13: // Enter
      if (ui->popupmsg) {
	free(ui->popupmsg); ui->popupmsg = NULL;
	handle_msg_reply(view);
	free(ui->pendingdata); ui->pendingdata = NULL;
	ui->pendingmode = 0;
      } else if (ui->displaymode == 5 || ui->displaymode == 6) {
	txtentry_start(view, "Enter File Name:", "" );
      }
      queue_reshape = 1;
      break;
    case 9: // tab
      if (ui->displaymode == 0 ANDNOANIM) {
	ui->keyboard_control |= 1;
	queue_reshape = 1;
      }
      break;
    case 27: // ESC
      if (ui->popupmsg) {
	free(ui->popupmsg); ui->popupmsg = NULL;
	free(ui->pendingdata); ui->pendingdata = NULL;
	ui->pendingmode = 0;
      } else if (ui->displaymode == 7) {
#ifdef ANIMSTEPS
	ui->animdirection = 0;
	ui->openanim = ANIMSTEPS - 1;
#else
	ui->displaymode = 0;
#endif
	reset_state(view);
      } else if (ui->displaymode) {
	ui->displaymode = 0;
	reset_state(view);
      }
      queue_reshape = 1;
      break;
    case 'm': SKD(0)
      if (ui->show_mm) {
	ui->show_mm = 0;
      } else {
	int i;
	for (i = 0; i < TOTAL_OBJ; ++i) {
	  ui->ctrls[i].midinfo[0] = '\0';
	}
	forge_message_str(ui, ui->uris.sb3_uimccquery, NULL);
	ui->show_mm = 1;
      }
      puglPostRedisplay(view);
      break;
    case ' ':
      if (ui->displaymode == 0 ANDNOANIM) {
#ifdef ANIMSTEPS
	ui->animdirection = 1;
	ui->openanim = 1;
#else
	ui->displaymode = 7;
#endif
	reset_state(view);
      }
      else if (ui->displaymode == 7
#ifdef ANIMSTEPS
	  && ui->openanim == ANIMSTEPS
#endif
	  )
      {
#ifdef ANIMSTEPS
	ui->animdirection = 0;
	ui->openanim = ANIMSTEPS - 1;
#else
	ui->displaymode = 0;
#endif
	reset_state(view);
      }
      queue_reshape = 1;
      reset_state(view);
      break;
    case 'L':
      if (ui->displaymode == 0 ANDNOANIM) {
	dirlist(view, ui->curdir);
	ui->displaymode = 4;
      }
      else if (ui->displaymode == 4) {
	ui->displaymode = 0;
      }
      queue_reshape = 1;
      reset_state(view);
      break;
    case 'C':
      if (ui->displaymode == 0 ANDNOANIM) {
	dirlist(view, ui->curdir);
	ui->displaymode = 5;
      }
      else if (ui->displaymode == 5) {
	ui->displaymode = 0;
      }
      queue_reshape = 1;
      reset_state(view);
      break;
    case 'V':
      if (ui->displaymode == 0 ANDNOANIM) {
	dirlist(view, ui->curdir);
	ui->displaymode = 6;
      }
      else if (ui->displaymode == 6) {
	ui->displaymode = 0;
      }
      queue_reshape = 1;
      reset_state(view);
      break;
    case '.':
      if (IS_FILEBROWSER(ui)) {
	ui->dir_hidedotfiles = !ui->dir_hidedotfiles;
	dirlist(view, ui->curdir);
      }
      break;
    default:
      break;
  }

  if (queue_reshape) {
    onReshape(view, ui->width, ui->height);
    puglPostRedisplay(view);
  }
}

static void
onScroll(PuglView* view, int x, int y, float dx, float dy)
{
  B3ui* ui = (B3ui*)puglGetHandle(view);
  float fx, fy;
  if (ui->popupmsg) return;
  if (ui->textentry_active) return;
  if (fabs(dy) < .1) return;

  if (ui->displaymode == 8) {
    fx = (2.0 * x / ui->width ) - 1.0;
    fy = (2.0 * y / ui->height ) - 1.0;
    fy *= (ui->height / (float) ui->width) / (320. / 960.);

    int tri = 0;
    int cfg = cfg_mousepos(fx, fy, &tri);
    if (cfg > 0) {
      cfg_update_value(view, cfg - 1, SIGNUM(dy));
    }
  }

  if (ui->displaymode) return;

  project_mouse(view, x, y, -.5, &fx, &fy);
  int i;
  for (i = 0; i < TOTAL_OBJ ; ++i) {
    if (MOUSEOVER(ui->ctrls[i], fx, fy)) {
      ui->dndval = ui->ctrls[i].cur + SIGNUM(dy);
      processMotion(view, i, 0, 0);
      break;
    }
  }
}

static void
onMotion(PuglView* view, int x, int y)
{
  B3ui* ui = (B3ui*)puglGetHandle(view);
  float fx, fy;
  const int phov = ui->mouseover;

  ui->mouseover = 0;

  /* mouse - button hover */
  fx = (2.0 * x / ui->width ) - 1.0;
  fy = (2.0 * y / ui->height ) - 1.0;
  fy *= (ui->height / (float) ui->width) / (320. / 960.);

  if (ui->displaymode == 7) { // menu
    if (MOUSEIN(MENU_SAVEP, fx, fy)) ui->mouseover |= HOVER_MSAVEP;
    if (MOUSEIN(MENU_SAVEC, fx, fy)) ui->mouseover |= HOVER_MSAVEC;
    if (MOUSEIN(MENU_LOAD, fx, fy)) ui->mouseover |= HOVER_MLOAD;
    if (MOUSEIN(MENU_PGMS, fx, fy)) ui->mouseover |= HOVER_MPGMS;
    if (MOUSEIN(MENU_PGML, fx, fy)) ui->mouseover |= HOVER_MPGML;
    if (MOUSEIN(MENU_ACFG, fx, fy)) ui->mouseover |= HOVER_MACFG;
    if (MOUSEIN(MENU_CANC, fx, fy)) ui->mouseover |= HOVER_MCANC;
    if (phov != ui->mouseover) {
      puglPostRedisplay(view);
    }
    return;
  }
  else if (ui->displaymode == 8) { // cfg
    const int trih = ui->cfgtriover;
    ui->cfgtriover = 0;
    if (ui->cfgdrag > 0) {
      ui->mouseover = ui->cfgdrag;
      float mult = ui->dragmult;
      if (puglGetModifiers(view) & PUGL_MOD_CTRL) { mult *= .1; } // DND TODO
      int delta = rintf (((x - ui->cfgdrag_x) - (y - ui->cfgdrag_y)) * mult);
      if (delta != 0) {
	const float oldval = ui->dragval;
	int ccc = 24 * ui->cfgtab + ui->cfgdrag - 1;
	ui->dragval = cfg_update_parameter(ui, ccc, ui->dragval, delta);
	if (oldval != ui->dragval) { puglPostRedisplay(view); }
	ui->cfgdrag_x = x; ui->cfgdrag_y = y;
      }

    } else if (MOUSEIN(MENU_CANC, fx, fy)) {
      ui->mouseover = HOVER_MCANC;
    } else if (fy < -.8) { // TAB bar
      int tab = cfg_tabbar(fx);
      if (tab >= 0 && tab < MAXTAB) {
	ui->mouseover = 25 + tab;
      }
    } else {
      ui->mouseover = cfg_mousepos(fx, fy, &ui->cfgtriover);
    }
    if (phov != ui->mouseover || trih != ui->cfgtriover) {
      puglPostRedisplay(view);
    }
    return;
  }
  else if (ui->textentry_active || ui->popupmsg || ui->displaymode) {
    if (MOUSEIN(BTNLOC_OK, fx, fy)) ui->mouseover |= HOVER_OK;
    if (MOUSEIN(BTNLOC_NO, fx, fy)) ui->mouseover |= HOVER_NO;
    if (MOUSEIN(BTNLOC_YES, fx, fy)) ui->mouseover |= HOVER_YES;
    if (MOUSEIN(BTNLOC_SAVE, fx, fy)) ui->mouseover |= HOVER_SAVE;
    if (MOUSEIN(BTNLOC_DFLT, fx, fy)) ui->mouseover |= HOVER_DFLT;
    if (MOUSEIN(BTNLOC_CANC, fx, fy)) ui->mouseover |= HOVER_CANC;
    if (MOUSEIN(BTNLOC_CANC2, fx, fy)) ui->mouseover |= HOVER_CANC2;
    if (MOUSEIN(BTNLOC_CANC3, fx, fy)) ui->mouseover |= HOVER_CANC3;
    if (MOUSEIN(BTNLOC_RSRC, fx, fy)) ui->mouseover |= HOVER_RSRC;
    if (MOUSEIN(SCROLLBAR, fx, fy)) ui->mouseover |= HOVER_SCROLLBAR;
    if (phov != ui->mouseover) {
      puglPostRedisplay(view);
    }
  }

  if (ui->textentry_active || ui->popupmsg) return;

  /* mouse - listview hover */
  if (ui->displaymode == 2 || ui->displaymode == 3) {
    int pgm_sel; // = ui->pgm_sel;
    fx /= SCALE * 22.0; fy /= SCALE * 22.0;
    fx += 1.1; fy += 1.0;
    fx *= 2.7; fy *= 12.0;
    if (fx > 0 && fx < 6 && fy > 0 && fy < 24) {
      pgm_sel = floor(fx) * 24 + floor(fy);
      if (pgm_sel < 0 || pgm_sel > 127) pgm_sel = -1;
    } else {
      pgm_sel = -1;
    }
    if (pgm_sel != ui->pgm_sel) {
      ui->pgm_sel = pgm_sel;
      puglPostRedisplay(view);
    }
    ui->dndid = -1;
    return;
  } else {
    ui->pgm_sel = -1;
  }

  if (IS_FILEBROWSER(ui) && ui->dir_scrollgrab != NOSCROLL) {
    fx = (2.0 * x / ui->width ) - 1.0;
    const int pages = (ui->dirlistlen / 20);
    const float ss = 1.6 / (float)pages;
    const int dir_scroll = ui->dir_scroll;
    ui->dir_scroll = (fx +.8 - ui->dir_scrollgrab) / ss;
    if (ui->dir_scroll < 0) ui->dir_scroll = 0;
    if (ui->dir_scroll > pages - 5) ui->dir_scroll = pages - 5;
    if (ui->dir_scroll != dir_scroll) {
      puglPostRedisplay(view);
    }
    return;
  }
  else if (IS_FILEBROWSER(ui)) {
    int dir_sel; // = ui->dir_sel;
    fx = (2.0 * x / ui->width ) - 1.0;
    fy = (2.0 * y / ui->height ) - 1.0;
    fy *= (ui->height / (float) ui->width) / (320. / 960.);

    fx /= SCALE * 22.0; fy /= SCALE * 22.0;
    fx += 1.1; fy += 1.0;
    fx *= 2.7; fy *= 12.0;
    fy+=1; fx+=ui->dir_scroll;
    if (fx > 0 && fy > 0 && fy < 20) {
      dir_sel = floor(fx) * 20 + floor(fy);
      if (dir_sel >= ui->dirlistlen) dir_sel = -1;
    } else {
      dir_sel = -1;
    }
    if (dir_sel != ui->dir_sel) {
      ui->dir_sel = dir_sel;
      puglPostRedisplay(view);
    }
    ui->dndid = -1;
    ui->pgm_sel = -1;
    return;
  } else {
    ui->dir_sel = -1;
  }

  if (ui->dndid < 0) return;

  project_mouse(view, x, y, -.5, &fx, &fy);

  const float dx = (fx - ui->dndx);
  const float dy = (fy - ui->dndy);

  processMotion(view, ui->dndid, dx, dy);
}

static void
onMouse(PuglView* view, int button, bool press, int x, int y)
{
  B3ui* ui = (B3ui*)puglGetHandle(view);
  int i;
  float fx, fy;
  float kx, ky;

  if (ui->displaymode || ui->textentry_active || ui->popupmsg) {
    if (button != 1) return;
  }

  if (!press) {
    if (ui->upper_key >= 0) {
      forge_note(ui, 0, ui->upper_key + 36, false);
      puglPostRedisplay(view);
    }
    if (ui->lower_key >= 0) {
      forge_note(ui, 1, ui->lower_key + 36, false);
      puglPostRedisplay(view);
    }
    if (ui->pedal_key >= 0) {
      forge_note(ui, 2, ui->pedal_key + 24, false);
      puglPostRedisplay(view);
    }
    if (ui->cfgdrag) {
      int ccc = 24 * ui->cfgtab + ui->cfgdrag - 1;
      if (ui->dragval != ui->cfgvar[ccc].cur) {
	ui->cfgvar[ccc].cur = ui->dragval;
	cfg_tx_update(ui, ccc);
      }
      ui->cfgdrag = 0;
      onMotion(view, x, y);
      puglPostRedisplay(view);
    }

    ui->dndid = -1;
    ui->upper_key = -1;
    ui->lower_key = -1;
    ui->pedal_key = -1;
    ui->dir_scrollgrab = NOSCROLL;
    return;
  }

  if (ui->popupmsg) {
    fx = (2.0 * x / ui->width ) - 1.0;
    fy = (2.0 * y / ui->height ) - 1.0;
    fy *= (ui->height / (float) ui->width) / (320. / 960.);

    if (ui->pendingmode) {
      if (MOUSEIN(BTNLOC_NO, fx, fy)) { ; } // NO
      else if (MOUSEIN(BTNLOC_YES, fx, fy)) {  // YES
	handle_msg_reply(view);
      } else {
	return; // clicked elsewhere
      }
    } else {
      if (!MOUSEIN(BTNLOC_OK, fx, fy)) {
	return; // clicked elsewhere
      }
    }
    free(ui->popupmsg); ui->popupmsg = NULL;
    free(ui->pendingdata); ui->pendingdata = NULL;
    ui->pendingmode = 0;
    onReshape(view, ui->width, ui->height);
    puglPostRedisplay(view);
    return;
  }

  if (ui->textentry_active) return;


  switch(ui->displaymode) {
    case 2:
      fx = (2.0 * x / ui->width ) - 1.0;
      fy = (2.0 * y / ui->height ) - 1.0;
      fy *= (ui->height / (float) ui->width) / (320. / 960.);
      if (ui->pgm_sel >= 0) {
	ui->displaymode = 0;
	forge_message_int(ui, ui->uris.sb3_midipgm, ui->pgm_sel);
	onReshape(view, ui->width, ui->height);
	puglPostRedisplay(view);
	reset_state(view);
      } else if (MOUSEIN(BTNLOC_CANC2, fx, fy)) {
	ui->displaymode = 0;
	onReshape(view, ui->width, ui->height);
	puglPostRedisplay(view);
	reset_state(view);
      }
      return;

    case 3:
      fx = (2.0 * x / ui->width ) - 1.0;
      fy = (2.0 * y / ui->height ) - 1.0;
      fy *= (ui->height / (float) ui->width) / (320. / 960.);
      if (ui->pgm_sel >= 0) {
	txtentry_start(view,"Enter Preset Name:", strlen(ui->midipgm[ui->pgm_sel]) > 0 ? ui->midipgm[ui->pgm_sel] : "User" );
	puglPostRedisplay(view);
      } else if (MOUSEIN(BTNLOC_CANC2, fx, fy)) {
	ui->displaymode = 0;
	onReshape(view, ui->width, ui->height);
	puglPostRedisplay(view);
	reset_state(view);
      }
      return;

    case 9: // key help screen
    case 1: // help screen
      ui->displaymode = 0;
      onReshape(view, ui->width, ui->height);
      puglPostRedisplay(view);
      reset_state(view);
      return;

    case 8:
      fx = (2.0 * x / ui->width ) - 1.0;
      fy = (2.0 * y / ui->height ) - 1.0;
      fy *= (ui->height / (float) ui->width) / (320. / 960.);
      if (MOUSEIN(MENU_CANC, fx, fy)) {
	ui->displaymode = 0;
	onReshape(view, ui->width, ui->height);
	puglPostRedisplay(view);
	reset_state(view);
      } else if (fy < -.8) { // TAB bar
	int tab = cfg_tabbar(fx);
	if (tab >= 0 && tab < MAXTAB) {
	  if (tab != ui->cfgtab) {
	    ui->cfgtab = tab;
	    puglPostRedisplay(view);
	  }
	}
      } else {
	int tri = 0;
	int cfg = cfg_mousepos(fx, fy, &tri);

	assert (ui->cfgdrag == 0);

	if (cfg > 21 && cfg < 24  && ui->cfgtab == 0) {
	  // special push buttons
	  cfg_special_button(view, cfg -1);
	} else if (cfg > 0 && (puglGetModifiers(view) & PUGL_MOD_SHIFT)) {
	  cfg_update_value(view, cfg - 1, 0);
	} else if (cfg > 0 && tri != 0) {
	  cfg_update_value(view, cfg - 1, tri * ((puglGetModifiers(view) & PUGL_MOD_CTRL) ? 5 : 1));
	} else if (cfg > 0 && cfg <= 24) {
	  ui->cfgdrag_x = x; ui->cfgdrag_y = y;
	  cfg_start_drag(ui, cfg);
	  puglPostRedisplay(view);
	}
      }
      return;

    case 7:
      fx = (2.0 * x / ui->width ) - 1.0;
      fy = (2.0 * y / ui->height ) - 1.0;
      fy *= (ui->height / (float) ui->width) / (320. / 960.);

      if (MOUSEIN(MENU_SAVEP, fx, fy)) {
	dirlist(view, ui->curdir);
	ui->displaymode = 6;
	reset_state(view);
	RESETANIM
      }
      if (MOUSEIN(MENU_SAVEC, fx, fy)) {
	dirlist(view, ui->curdir);
	ui->displaymode = 5;
	reset_state(view);
	RESETANIM
      }
      if (MOUSEIN(MENU_LOAD, fx, fy)) {
	dirlist(view, ui->curdir);
	ui->displaymode = 4;
	reset_state(view);
	RESETANIM
      }
      if (MOUSEIN(MENU_PGML, fx, fy)) {
	ui->displaymode = 2;
	reset_state(view);
	RESETANIM
      }
      if (MOUSEIN(MENU_PGMS, fx, fy)) {
	ui->displaymode = 3;
	reset_state(view);
	RESETANIM
      }
      if (MOUSEIN(MENU_ACFG, fx, fy)) {
	ui->displaymode = 8;
	reset_state(view);
	RESETANIM
      }
      if (MOUSEIN(MENU_CANC, fx, fy)) {
#ifdef ANIMSTEPS
	ui->animdirection = 0;
	ui->openanim = ANIMSTEPS - 1;
#else
	ui->displaymode = 0;
#endif
	reset_state(view);
	onReshape(view, ui->width, ui->height);
      }
      puglPostRedisplay(view);
      return;

    case 4:
    case 5:
    case 6: //IS_FILEBROWSER() == displaymode 4,5,6
      fx = (2.0 * x / ui->width ) - 1.0;
      fy = (2.0 * y / ui->height ) - 1.0;
      fy *= (ui->height / (float) ui->width) / (320. / 960.);

      if (ui->dir_sel >= 0 && ui->dir_sel < ui->dirlistlen) {
	/* click on file */
	struct stat fs;
	char * rfn = absfilepath(ui->curdir, ui->dirlist[ui->dir_sel]);
	if(rfn && stat(rfn, &fs) == 0) {
	  if (S_ISDIR(fs.st_mode)) {
	    free(ui->curdir);
	    ui->curdir = rfn;
	    dirlist(view, ui->curdir);
	    puglPostRedisplay(view);
	    return;
	  } else if (S_ISREG(fs.st_mode)) {
	    switch(ui->displaymode) {
	      case 4:
		if (!check_extension(rfn, ".pgm")) {
		  forge_message_str(ui, ui->uris.sb3_loadpgm, rfn);
		}
		else if (!check_extension(rfn, ".cfg")) {
		  forge_message_str(ui, ui->uris.sb3_loadcfg, rfn);
		} else {
		  show_message(view, "file is not a .pgm nor .cfg");
		}
		break;
	      case 6:
	      case 5:
		if (save_cfgpgm(view, rfn, ui->displaymode, 0)) {
		  /* failed -> retry */
		  free(rfn);
		  puglPostRedisplay(view);
		  return;
		}
		break;
	    }
	  }
	}
	free(rfn);
      } else if (
	  ui->dirlistlen > 120
	  && MOUSEIN(SCROLLBAR, fx, fy)
	  ) {
	  // handle scrollbar
	int pages = (ui->dirlistlen / 20);
	float ss = 1.6 / (float)pages;
	float sw = 5.0 * ss;
	float sx = ui->dir_scroll * ss - .8;
	if (fx < sx && ui->dir_scroll > 0) --ui->dir_scroll;
	else if (fx > sx+sw && ui->dir_scroll < (pages-4)) ++ui->dir_scroll;
	else if (fx >= sx && fx <= sx+sw) {
	  ui->dir_scrollgrab = fx - sx;
	}
	ui->dir_sel = -1;
	puglPostRedisplay(view);
	return;
#ifdef JACK_DESCRIPT
      } else if (ui->displaymode == 5 && MOUSEIN(BTNLOC_DFLT, fx, fy) && ui->defaultConfigFile) {
	save_cfgpgm(view, ui->defaultConfigFile, 5, 0);
	ui->displaymode = 0;
	return;
      } else if (ui->displaymode == 6 && MOUSEIN(BTNLOC_DFLT, fx, fy) && ui->defaultProgrammeFile) {
	save_cfgpgm(view, ui->defaultProgrammeFile, 6, 0);
	ui->displaymode = 0;
	return;
      } else if (ui->displaymode == 4 && MOUSEIN(BTNLOC_RSRC, fx, fy) && ui->bundlePath) {
	    free(ui->curdir);
	    ui->curdir = strdup(ui->bundlePath);;
	    dirlist(view, ui->curdir);
	    puglPostRedisplay(view);
	return;
#endif
      } else if (
	  (ui->displaymode == 5 || ui->displaymode == 6)
	  && MOUSEIN(BTNLOC_SAVE, fx, fy)
	  ) {
	txtentry_start(view, "Enter File Name:", "" );
	return;
      } else if (ui->displaymode == 4 && !MOUSEIN(BTNLOC_CANC3, fx, fy)) {
	return;
      } else if ((ui->displaymode == 5 || ui->displaymode == 6) && !MOUSEIN(BTNLOC_CANC, fx, fy)) {
	return;
      }
      ui->displaymode = 0;
      onReshape(view, ui->width, ui->height);
      puglPostRedisplay(view);
      reset_state(view);
      return;

    default:
      break;
  }

  /* main organ view  */

  project_mouse(view, x, y, -.5, &fx, &fy);

  if (button == 1 && ui->displaymode == 0 && fx >= 1.04 && fx <= 1.100 && fy >= -.24 && fy <= -.18 ANDNOANIM) {
    /* help button */
    reset_state_ccbind(view);
    ui->displaymode = (ui->keyboard_control & 1) ? 9 : 1;
    onReshape(view, ui->width, ui->height);
    puglPostRedisplay(view);
    return;
  }

  if (button == 1 && ui->displaymode == 0 && fx >= 0.92 && fx <= 1.04 && fy >= -.25 && fy <= -.16 ANDNOANIM) {
    /* menu button */
    reset_state_ccbind(view);
#ifdef ANIMSTEPS
    ui->animdirection = 1;
    ui->openanim = 1;
#else
    ui->displaymode = 7;
#endif
    onReshape(view, ui->width, ui->height);
    puglPostRedisplay(view);
    return;
  }

  if (puglGetModifiers(view) & PUGL_MOD_CTRL &&
#ifdef __APPLE__ // cater for users w/o 3 button mouse
      (button == 2 || button == 3)
#else
      button == 2
#endif
     ) {
    for (i = 0; i < TOTAL_OBJ; ++i) {
      if (!MOUSEOVER(ui->ctrls[i], fx, fy)) {
	continue;
      }
      ui->uiccbind = i;
      ui->uiccflag = (puglGetModifiers(view) & PUGL_MOD_SHIFT) ? MFLAG_INV : 0;
      forge_message_kv(ui, ui->uris.sb3_uimccset, ui->uiccflag , obj_control[i]);
      puglPostRedisplay(view);
      return;
    }
  }

  reset_state_ccbind(view);

  if (button != 1) {
    return;
  }

  for (i = 0; i < TOTAL_OBJ; ++i) {
    if (!MOUSEOVER(ui->ctrls[i], fx, fy)) {
      continue;
    }

    switch (ui->ctrls[i].type) {
      case OBJ_DRAWBAR:
      case OBJ_DIAL:
      case OBJ_LEVER:
	ui->dndid = i;
	ui->dndx = fx;
	ui->dndy = fy;
	ui->dndval = ui->ctrls[i].cur;
	break;
      case OBJ_SWITCH:
	if (press) {
	  if (ui->ctrls[i].cur == ui->ctrls[i].max)
	    ui->ctrls[i].cur = ui->ctrls[i].min;
	  else
	    ui->ctrls[i].cur = ui->ctrls[i].max;
	  puglPostRedisplay(view);
	  notifyPlugin(view, i);
	}
	break;
      default:
	break;
    }
    return;
  }

  // upper manual
  project_mouse(view, x, y, 1.8, &kx, &ky);
  if (kx >= -.677 && kx <= .760 && ky >= .3 && ky <= .5) {
    int key;
    if (ky >= .41) {
      // white key
      int wk = floor((kx + .677) * 36. / (.760 + .677));
      key = 12 * (wk / 7) + (wk % 7) * 2;
      if (wk % 7 > 2) {
	key--;
      }
    } else {
      // white or black key
      key = floor(.5 + (kx + .677) * 61. / (.760 + .677));
    }
    // top-most white key is "special" (no upper black)
    if (key > 60) key = 60;
    ui->upper_key = key;
    forge_note(ui, 0, key + 36, true);
    puglPostRedisplay(view);
    return;
  }

  // lower manual
  project_mouse(view, x, y, 3.9, &kx, &ky);
  if (kx >= -.677 && kx <= .760 && ky >= .5 && ky <= .7) {
    int key;
    if (ky >= .62) {
      // white key
      int wk = floor((kx + .677) * 36. / (.760 + .677));
      key = 12 * (wk / 7) + (wk % 7) * 2;
      if (wk % 7 > 2) {
	key--;
      }
    } else {
      // white or black key
      key = floor(.5 + (kx + .677) * 61. / (.760 + .677));
    }
    // top-most white key is "special" (no upper black)
    if (key > 60) key = 60;
    ui->lower_key = key;
    forge_note(ui, 1, key + 36, true);
    puglPostRedisplay(view);
    return;
  }

  // pedals
  project_mouse(view, x, y, 27.5, &kx, &ky);
  if (kx >= -.554 && kx <= .600 && ky >= -.360 && ky <= .3) {
    int key = -1;
    int wk = floor((kx + .554) * 29. / (.600 + .554));
    if (ky >= -.075) {
      // white keys only
      if (wk % 2 == 0) {
	key = wk;
      }
    } else {
      // white or black key
      key = wk;
      if (wk ==  5) key = -1;
      if (wk == 13) key = -1;
      if (wk == 19) key = -1;
      if (wk == 27) key = -1;
    }
    if (key >= 0) {
      if (wk >=  5) --key;
      if (wk >= 13) --key;
      if (wk >= 19) --key;
      if (wk >= 27) --key;

      ui->pedal_key = key;
      forge_note(ui, 2, key + 24, true);
      puglPostRedisplay(view);
    }
    return;
  }

}


/******************************************************************************
 * misc - used for LV2 init/operation
 */
static void check_anim(B3ui* ui)
{
#ifdef ANIMSTEPS
  if (ui->openanim > 0 && ui->openanim < ANIMSTEPS) {
    if (ui->animdirection) {
      ++ui->openanim;
    } else {
      --ui->openanim;
    }
    if (ui->openanim == ANIMSTEPS) {
      ui->displaymode = 7;
    } else {
      ui->displaymode = 0;
    }
    onReshape(ui->view, ui->width, ui->height);
    puglPostRedisplay(ui->view);
  }
#endif
}

#ifdef OLD_SUIL
static void* ui_thread(void* ptr)
{
  B3ui* ui = (B3ui*)ptr;
  while (!ui->exit) {
    usleep(1000000 / 25);  // 25 FPS
    puglProcessEvents(ui->view);
    check_anim(ui);
  }
  return NULL;
}
#elif !defined XTERNAL_UI
static int idle(LV2UI_Handle handle) {
  B3ui* ui = (B3ui*)handle;
  puglProcessEvents(ui->view);
  check_anim(ui);
  return 0;
}
#endif

#ifdef XTERNAL_UI

static void onClose(PuglView* view) {
  B3ui* ui = (B3ui*)puglGetHandle(view);
  ui->close_ui = true;
}

static void x_run (struct lv2_external_ui * handle) {
  B3ui* ui = (B3ui*)handle->self;
  puglProcessEvents(ui->view);
  if (ui->close_ui && ui->ui_closed) {
    ui->close_ui = false;
    puglHideWindow(ui->view);
    ui->ui_closed(ui->controller);
  } else {
    check_anim(ui);
  }
}

static void x_show (struct lv2_external_ui * handle) {
  B3ui* ui = (B3ui*)handle->self;
  puglShowWindow(ui->view);
  forge_message_str(ui, ui->uris.sb3_uiinit, NULL);
}

static void x_hide (struct lv2_external_ui * handle) {
  B3ui* ui = (B3ui*)handle->self;
  puglHideWindow(ui->view);
}
#endif

/******************************************************************************
 * main GUI setup
 */

static int sb3_gui_setup(B3ui* ui, const LV2_Feature* const* features) {
  PuglNativeWindow parent = 0;
  LV2UI_Resize*    resize = NULL;
  int i;

  ui->displaymode = 0;
  ui->pgm_sel     = -1;
  ui->show_mm     = 0;
  ui->uiccbind    = -1;
  ui->uiccflag    = 0;
  ui->reinit      = 0;
  ui->width       = 960;
  ui->height      = 320;
  ui->dndid       = -1;
  ui->initialized = 0;
  ui->textentry_active = 0;
  ui->dirlist     = NULL;
  ui->dirlistlen  = 0;
  ui->dir_sel     = -1;
  ui->dir_scroll  = 0;
  ui->dir_scrollgrab = NOSCROLL;
  ui->dir_hidedotfiles = 0;
  ui->mouseover = 0;
  ui->popupmsg = NULL;
  ui->queuepopup = 0;
  ui->pendingdata = NULL;
  ui->pendingmode = 0;
  ui->upper_key = -1;
  ui->lower_key = -1;
  ui->pedal_key = -1;
  ui->keyboard_control = 0;
  ui->cfgtriover = 0;
  ui->cfgdrag = 0;
  ui->cfgtab = 0;
  ui->highlight_keys = true;
#ifdef ANIMSTEPS
  ui->openanim = 0;
  ui->animdirection = 0;
#endif

  for (int i = 0; i < 5; ++i) {
    ui->active_keys[i] = 0;
  }

  ui->scale  = 0.9;
  ui->rot[0] = -15;
  ui->rot[1] = -20;
  ui->rot[2] =   0;
  ui->off[0] =  0.0f;
  ui->off[1] = -0.1f;
  ui->off[2] =  0.0f;

  for (int i = 0; features && features[i]; ++i) {
    if (!strcmp(features[i]->URI, LV2_UI__parent)) {
      parent = (PuglNativeWindow)features[i]->data;
    } else if (!strcmp(features[i]->URI, LV2_UI__resize)) {
      resize = (LV2UI_Resize*)features[i]->data;
    }
#ifdef XTERNAL_UI
    else if (!strcmp(features[i]->URI, LV2_EXTERNAL_UI_URI) && !ui->extui) {
      ui->extui = (struct lv2_external_ui_host*) features[i]->data;
    }
    else if (!strcmp(features[i]->URI, LV2_EXTERNAL_UI_URI__KX__Host)) {
      ui->extui = (struct lv2_external_ui_host*) features[i]->data;
    }
#endif
  }

  if (!parent
#ifdef XTERNAL_UI
      && !ui->extui
#endif
     )
  {
    fprintf(stderr, "B3Lv2UI error: No parent window provided.\n");
    return -1;
  }

  /* prepare meshes */
  ui->vbo = (GLuint *)malloc(OBJECTS_COUNT * sizeof(GLuint));
  ui->vinx = (GLuint *)malloc(OBJECTS_COUNT * sizeof(GLuint));

  /* Set up GL UI */
  ui->view = puglCreate(
      parent,
      "setBfree",
      ui->width, ui->height,
      ui->width, ui->height,
      true, false, 0);

  if (!ui->view) {
    return -1;
  }

  puglSetHandle(ui->view, ui);
  puglSetDisplayFunc(ui->view, onDisplay);
  puglSetReshapeFunc(ui->view, onReshape);
  puglSetKeyboardFunc(ui->view, onKeyboard);
  puglSetMotionFunc(ui->view, onMotion);
  puglSetMouseFunc(ui->view, onMouse);
  puglSetScrollFunc(ui->view, onScroll);

#ifdef _WIN32
  ui->curdir = strdup("C:\\");
#else
  if (getenv("HOME")) {
    ui->curdir = strdup(getenv("HOME"));
  } else {
    ui->curdir = strdup("/");
  }
#endif

#ifdef XTERNAL_UI
  ui->ui_closed = NULL;
  ui->close_ui = false;
  if (ui->extui) {
    puglSetCloseFunc(ui->view, onClose);
    ui->ui_closed = ui->extui->ui_closed;
  }
#endif

  if (resize) {
    resize->ui_resize(resize->handle, ui->width, ui->height);
  }

  /** add control elements **/

#define CTRLELEM(ID, TYPE, VMIN, VMAX, VCUR, PX, PY, W, H, TEXID) \
  {\
    ui->ctrls[ID].type = TYPE; \
    ui->ctrls[ID].min = VMIN; \
    ui->ctrls[ID].max = VMAX; \
    ui->ctrls[ID].cur = VCUR; \
    ui->ctrls[ID].x = PX; \
    ui->ctrls[ID].y = PY; \
    ui->ctrls[ID].w = W; \
    ui->ctrls[ID].h = H; \
    ui->ctrls[ID].texID = TEXID; \
  }

  /* drawbars */
  for (i = 0; i < 9; ++i)
    CTRLELEM(i, OBJ_DRAWBAR, 0, 8, 0,   3.6 + 1.4 * i,      1.0, 1.2, 12, 1);
  for (; i < 18; ++i)
    CTRLELEM(i, OBJ_DRAWBAR, 0, 8, 0, -10.5 + 1.4 * (i-9),  1.0, 1.2, 12, 1);
  for (; i < 20; ++i)
    CTRLELEM(i, OBJ_DRAWBAR, 0, 8, 0, -14.8 + 1.4 * (i-18), 1.0, 1.2, 12, 1);

  /* btn - perc 20 - 23*/
  for (; i < 24; ++i)
    CTRLELEM(i, OBJ_SWITCH, 0, 1, 0, 18.75 + 2.75 * (i-20), -1, 2, 4, i-14);

  /* btn - vib 24, 25*/
  CTRLELEM(24, OBJ_SWITCH, 0, 1, 0, -21.50, -1, 2, 4, 3);
  CTRLELEM(25, OBJ_SWITCH, 0, 1, 0, -18.75, -1, 2, 4, 4);

  /* btn -- overdrive */
  CTRLELEM(26, OBJ_SWITCH, 0, 1, 0, -25.375, -1, 2, 4, 5);

  /* dials */
  CTRLELEM(27, OBJ_DIAL,  0, 127, -5,  -25.375, 3.5, 4, 4, 2); // overdrive
  CTRLELEM(28, OBJ_DIAL, -5, 0,   -5,  -20.375, 3.5, 4, 4, 2); // vibrato

  CTRLELEM(29, OBJ_DIAL,  0, 127,  0,   25.375, 3.5, 4, 4, 2); // volume
  CTRLELEM(30, OBJ_DIAL,  0, 127,  0,   20.375, 3.5, 4, 4, 2); // reverb

  /* Leslie levers */
  CTRLELEM(31, OBJ_LEVER, 0, 2, 2, -25.675, 8, 4, 3, -1);
  CTRLELEM(32, OBJ_LEVER, 0, 2, 0, -20.075, 8, 4, 3, -1);

  cfg_initialize(ui);
  cfg_set_defaults(ui);

#ifdef OLD_SUIL
  ui->exit = false;
  pthread_create(&ui->thread, NULL, ui_thread, ui);
#endif

  return 0;
}


/******************************************************************************
 * LV2 callbacks
 */

static LV2UI_Handle
instantiate(const LV2UI_Descriptor*   descriptor,
            const char*               plugin_uri,
            const char*               bundle_path,
            LV2UI_Write_Function      write_function,
            LV2UI_Controller          controller,
            LV2UI_Widget*             widget,
            const LV2_Feature* const* features)
{
  int i;
  B3ui* ui = (B3ui*)calloc(1, sizeof(B3ui));

  ui->map        = NULL;
  ui->write      = write_function;
  ui->controller = controller;
#ifdef XTERNAL_UI
  ui->extui = NULL;
#endif
#ifdef OLD_SUIL
  ui->exit       = true; // thread not active
#endif

  for (i = 0; features[i]; ++i) {
    if (!strcmp(features[i]->URI, LV2_URID__map)) {
      ui->map = (LV2_URID_Map*)features[i]->data;
    }
  }

  if (!ui->map) {
    fprintf(stderr, "B3Lv2UI error: Host does not support urid:map\n");
    free(ui);
    return NULL;
  }

  map_setbfree_uris(ui->map, &ui->uris);
  lv2_atom_forge_init(&ui->forge, ui->map);

  if (sb3_gui_setup(ui, features)) {
    free(ui);
    return NULL;
  }

#ifdef _WIN32
  if (glext_func()) {
    fprintf(stderr, "B3Lv2UI error: System has insufficient GL capabilities\n");
    return NULL;
  }
#endif

  memset(ui->midipgm, 0, 128 * 32 * sizeof(char));
  memset(ui->mididsc, 0, 128 * 256 * sizeof(char));

#ifdef XTERNAL_UI
  if (ui->extui) {
    ui->xternal_ui.run  = &x_run;
    ui->xternal_ui.show = &x_show;
    ui->xternal_ui.hide = &x_hide;
    ui->xternal_ui.self = (void*) ui;
    *widget = (void*) &ui->xternal_ui;
  } else
#endif
  {
    *widget = (void*)puglGetNativeWindow(ui->view);
  }

#ifdef JACK_DESCRIPT
  // CODE DUP src/main.c, lv2.c
  ui->defaultConfigFile = NULL;
  ui->defaultProgrammeFile = NULL;
  ui->bundlePath = NULL;

#ifdef _WIN32
  char wintmp[1024] = "";
  if (ExpandEnvironmentStrings("%localappdata%\\setBfree\\default.cfg", wintmp, 1024)) {
    ui->defaultConfigFile = strdup (wintmp);
  }
  wintmp[0] = '\0';
  if (ExpandEnvironmentStrings("%localappdata%\\setBfree\\default.pgm", wintmp, 1024)) {
    ui->defaultProgrammeFile = strdup (wintmp);
  }
#else // unices: prefer XDG_CONFIG_HOME
  if (getenv("XDG_CONFIG_HOME")) {
    size_t hl = strlen(getenv("XDG_CONFIG_HOME"));
    ui->defaultConfigFile=(char*) malloc(hl+22);
    ui->defaultProgrammeFile=(char*) malloc(hl+22);
    sprintf(ui->defaultConfigFile,    "%s/setBfree/default.cfg", getenv("XDG_CONFIG_HOME"));
    sprintf(ui->defaultProgrammeFile, "%s/setBfree/default.pgm", getenv("XDG_CONFIG_HOME"));
  }
  else if (getenv("HOME")) {
    size_t hl = strlen(getenv("HOME"));
# ifdef __APPLE__
    ui->defaultConfigFile=(char*) malloc(hl+42);
    ui->defaultProgrammeFile=(char*) malloc(hl+42);
    sprintf(ui->defaultConfigFile,    "%s/Library/Preferences/setBfree/default.cfg", getenv("HOME"));
    sprintf(ui->defaultProgrammeFile, "%s/Library/Preferences/setBfree/default.pgm", getenv("HOME"));
# else // linux, BSD, etc
    ui->defaultConfigFile=(char*) malloc(hl+30);
    ui->defaultProgrammeFile=(char*) malloc(hl+30);
    sprintf(ui->defaultConfigFile,    "%s/.config/setBfree/default.cfg", getenv("HOME"));
    sprintf(ui->defaultProgrammeFile, "%s/.config/setBfree/default.pgm", getenv("HOME"));
# endif
  }
#endif

#ifdef __APPLE__
  {
    char pathbuf[1024];
    if (proc_pidpath (getpid(), pathbuf, sizeof(pathbuf)) >= 0) {
      char *d = dirname(dirname(pathbuf));
      ui->bundlePath = (char*) malloc(strlen(d) + 12);
      strcpy(ui->bundlePath, d);
      strcat(ui->bundlePath, "/Resources/");
#ifndef NDEBUG
      printf("%s\n", ui->bundlePath);
#endif
      struct stat fs;
      if (stat(ui->bundlePath, &fs) == 0) {
	if (!S_ISDIR(fs.st_mode))
	{
	  // not a dir
	  free(ui->bundlePath);
	  ui->bundlePath = 0;
	}
      } else {
	// not a path
	free(ui->bundlePath);
	ui->bundlePath = 0;
      }
    }
  }
#endif

#endif


    /* ask plugin about current state */
    forge_message_str(ui, ui->uris.sb3_uiinit, NULL);

    return ui;
  }

static void
cleanup(LV2UI_Handle handle)
{
  B3ui* ui = (B3ui*)handle;
#ifdef OLD_SUIL
  if (!ui->exit) {
    ui->exit = true;
    pthread_join(ui->thread, NULL);
  }
#endif
#ifdef JACK_DESCRIPT
  free(ui->defaultConfigFile);
  free(ui->defaultProgrammeFile);
  free(ui->bundlePath);
#endif
  free_dirlist(ui);
  ftglDestroyFont(ui->font_big);
  ftglDestroyFont(ui->font_medium);
  ftglDestroyFont(ui->font_small);
  puglDestroy(ui->view);
  free(ui->vbo);
  free(ui->vinx);
  free(ui->curdir);
  free(ui);
}

static void
port_event(LV2UI_Handle handle,
    uint32_t     port_index,
    uint32_t     buffer_size,
    uint32_t     format,
    const void*  buffer)
{
  B3ui* ui = (B3ui*)handle;
  char *k, *fn, *dsc; int v;

  if (format != ui->uris.atom_eventTransfer) {
    fprintf(stderr, "B3Lv2UI: Unknown message format.\n");
    return;
  }

  LV2_Atom* atom = (LV2_Atom*)buffer;

  if (atom->type == ui->uris.midi_MidiEvent) {
    return;
  }

  if (atom->type != ui->uris.atom_Blank && atom->type != ui->uris.atom_Object) {
    fprintf(stderr, "B3Lv2UI: not an atom:Blank|Object msg.\n");
    return;
  }

  LV2_Atom_Object* obj = (LV2_Atom_Object*)atom;

  if (obj->body.otype == ui->uris.sb3_activekeys) {
    LV2_Atom *a0 = NULL;
    if (1 == lv2_atom_object_get(obj, ui->uris.sb3_keyarrary, &a0, NULL)
	&& a0->type == ui->uris.atom_Vector)
    {
      LV2_Atom_Vector* voi = (LV2_Atom_Vector*)LV2_ATOM_BODY(a0);
      const size_t n_elem = (a0->size - sizeof(LV2_Atom_Vector_Body)) / voi->atom.size;
      const unsigned int *data = (unsigned int*) LV2_ATOM_BODY(&voi->atom);
      if (n_elem == 5 /* sizeof(ui->active_keys) / sizeof(unsigned int) */) {
	memcpy(ui->active_keys, data, 5 * sizeof(unsigned int));
      }
      puglPostRedisplay(ui->view);
    }
    return;
  }

  if (obj->body.otype == ui->uris.sb3_cfgkv) {
    const LV2_Atom* key = NULL;
    const LV2_Atom* val = NULL;
    if (2 == lv2_atom_object_get(obj, ui->uris.sb3_cckey, &key, ui->uris.sb3_ccval, &val, 0) && key && val) {
	char *kk = (char*) LV2_ATOM_BODY(key);
	char *vv = (char*) LV2_ATOM_BODY(val);
	cfg_parse_config(ui, kk, vv);
    }
    return;
  }

  if (!get_cc_key_value(&ui->uris, obj, &k, &v)) {
    if (!strcmp(k, "special.midimap")) {
      ui->uiccbind = -1;
      ui->show_mm = 0;
      puglPostRedisplay(ui->view);
    } else if (!strcmp(k, "special.reinit")) {
      cfg_set_defaults(ui);
      ui->reinit = 0;
      puglPostRedisplay(ui->view);
    } else if (!strcmp(k, "special.init")) {
      if (v != 0) {
	show_message(ui->view, "Signature verificaion failed.");
      }
    } else {
      processCCevent(ui, k, v);
    }
  } else if (!get_cc_midi_mapping(&ui->uris, obj, &fn, &k)) {
    int i;
    int fnid = -1;
    for (i = 0; i < TOTAL_OBJ; ++i) {
      if (!strcmp(obj_control[i], fn)) {fnid = i; break;}
    }
    if (fnid >= 0) {
      strcat(ui->ctrls[fnid].midinfo, k);
    }
    puglPostRedisplay(ui->view);
  } else if (!get_pgm_midi_mapping(&ui->uris, obj, &v, &fn, &dsc)) {
    strncpy(ui->midipgm[v], fn, 32);
    strncpy(ui->mididsc[v], dsc, 256);
    ui->midipgm[v][31] = '\0';
    ui->mididsc[v][255] = '\0';
    puglPostRedisplay(ui->view);
  } else if (obj->body.otype == ui->uris.sb3_uimsg) {
    const LV2_Atom* msg = NULL;
    lv2_atom_object_get(obj, ui->uris.sb3_uimsg, &msg, 0);
    if (msg) {
      show_message(ui->view, (char*) LV2_ATOM_BODY(msg));
    }
  }
}

/******************************************************************************
 * LV2 setup
 */

#if !defined OLD_SUIL && !defined XTERNAL_UI
static const LV2UI_Idle_Interface idle_iface = { idle };
#endif

static const void*
extension_data(const char* uri)
{
#if !defined OLD_SUIL && !defined XTERNAL_UI
  if (!strcmp(uri, LV2_UI__idleInterface)) {
    return &idle_iface;
  }
#endif
  return NULL;
}

static const LV2UI_Descriptor descriptor = {
  SB3_URI "#ui",
  instantiate,
  cleanup,
  port_event,
  extension_data
};

LV2_SYMBOL_EXPORT
const LV2UI_Descriptor*
lv2ui_descriptor(uint32_t index)
{
  switch (index) {
  case 0:
    return &descriptor;
  default:
    return NULL;
  }
}

/* vi:set ts=8 sts=2 sw=2: */
