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

#ifndef OVERMAKER_H
#define OVERMAKER_H

#define BUFSZ 512

/* clang-format off */
#define INTWRAP(FN)                                    \
        vspace (1);                                    \
        codeln ("void f" FN " (void *d, float f) {");  \
        pushIndent ();                                 \
        codeln (FN " (d, (unsigned char)(f*127.0));"); \
        popIndent ();                                  \
        codeln ("}");

#define FLOATWRAP(FN)                                         \
        vspace (1);                                           \
        codeln ("void " FN " (void *d, unsigned char uc) {"); \
        pushIndent ();                                        \
        codeln ("f" FN " (d, uc/127.0);");                    \
        popIndent ();                                         \
        codeln ("}");
/* clang-format on */

typedef struct mod_call_struct {
	void (*fn) (int, void*);
	void* handback;
} modCall;

#define MOD_CONFIG 7
#define MOD_DOC 1
#define MODVECSZ 8

extern void code (char* s);
extern void codeln (char* s);
extern void vspace (int ln);
extern void comment (char* s);
extern void commentln (char* s);
extern void pushIndent ();
extern void popIndent ();

void bindCallback (int i, void (*func) (int, void*), void* hbk);
#endif /* OVERMAKER_H */
