#ifndef HAVE_MEMSTREAM
/* Compile-time assert-like macros.

   Copyright (C) 2005-2006, 2009-2012 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* Written by Paul Eggert, Bruno Haible, and Jim Meyering.  */

#ifndef _GL_VERIFY_H
# define _GL_VERIFY_H


/* Define _GL_HAVE__STATIC_ASSERT to 1 if _Static_assert works as per C11.
   This is supported by GCC 4.6.0 and later, in C mode, and its use
   here generates easier-to-read diagnostics when verify (R) fails.

   Define _GL_HAVE_STATIC_ASSERT to 1 if static_assert works as per C++11.
   This will likely be supported by future GCC versions, in C++ mode.

   Use this only with GCC.  If we were willing to slow 'configure'
   down we could also use it with other compilers, but since this
   affects only the quality of diagnostics, why bother?  */
# if (4 < __GNUC__ || (__GNUC__ == 4 && 6 <= __GNUC_MINOR__)) && !defined __cplusplus
#  define _GL_HAVE__STATIC_ASSERT 1
# endif
/* The condition (99 < __GNUC__) is temporary, until we know about the
   first G++ release that supports static_assert.  */
# if (99 < __GNUC__) && defined __cplusplus
#  define _GL_HAVE_STATIC_ASSERT 1
# endif

/* Each of these macros verifies that its argument R is nonzero.  To
   be portable, R should be an integer constant expression.  Unlike
   assert (R), there is no run-time overhead.

   If _Static_assert works, verify (R) uses it directly.  Similarly,
   _GL_VERIFY_TRUE works by packaging a _Static_assert inside a struct
   that is an operand of sizeof.

   The code below uses several ideas for C++ compilers, and for C
   compilers that do not support _Static_assert:

   * The first step is ((R) ? 1 : -1).  Given an expression R, of
     integral or boolean or floating-point type, this yields an
     expression of integral type, whose value is later verified to be
     constant and nonnegative.

   * Next this expression W is wrapped in a type
     struct _gl_verify_type {
       unsigned int _gl_verify_error_if_negative: W;
     }.
     If W is negative, this yields a compile-time error.  No compiler can
     deal with a bit-field of negative size.

     One might think that an array size check would have the same
     effect, that is, that the type struct { unsigned int dummy[W]; }
     would work as well.  However, inside a function, some compilers
     (such as C++ compilers and GNU C) allow local parameters and
     variables inside array size expressions.  With these compilers,
     an array size check would not properly diagnose this misuse of
     the verify macro:

       void function (int n) { verify (n < 0); }

   * For the verify macro, the struct _gl_verify_type will need to
     somehow be embedded into a declaration.  To be portable, this
     declaration must declare an object, a constant, a function, or a
     typedef name.  If the declared entity uses the type directly,
     such as in

       struct dummy {...};
       typedef struct {...} dummy;
       extern struct {...} *dummy;
       extern void dummy (struct {...} *);
       extern struct {...} *dummy (void);

     two uses of the verify macro would yield colliding declarations
     if the entity names are not disambiguated.  A workaround is to
     attach the current line number to the entity name:

       #define _GL_CONCAT0(x, y) x##y
       #define _GL_CONCAT(x, y) _GL_CONCAT0 (x, y)
       extern struct {...} * _GL_CONCAT (dummy, __LINE__);

     But this has the problem that two invocations of verify from
     within the same macro would collide, since the __LINE__ value
     would be the same for both invocations.  (The GCC __COUNTER__
     macro solves this problem, but is not portable.)

     A solution is to use the sizeof operator.  It yields a number,
     getting rid of the identity of the type.  Declarations like

       extern int dummy [sizeof (struct {...})];
       extern void dummy (int [sizeof (struct {...})]);
       extern int (*dummy (void)) [sizeof (struct {...})];

     can be repeated.

   * Should the implementation use a named struct or an unnamed struct?
     Which of the following alternatives can be used?

       extern int dummy [sizeof (struct {...})];
       extern int dummy [sizeof (struct _gl_verify_type {...})];
       extern void dummy (int [sizeof (struct {...})]);
       extern void dummy (int [sizeof (struct _gl_verify_type {...})]);
       extern int (*dummy (void)) [sizeof (struct {...})];
       extern int (*dummy (void)) [sizeof (struct _gl_verify_type {...})];

     In the second and sixth case, the struct type is exported to the
     outer scope; two such declarations therefore collide.  GCC warns
     about the first, third, and fourth cases.  So the only remaining
     possibility is the fifth case:

       extern int (*dummy (void)) [sizeof (struct {...})];

   * GCC warns about duplicate declarations of the dummy function if
     -Wredundant-decls is used.  GCC 4.3 and later have a builtin
     __COUNTER__ macro that can let us generate unique identifiers for
     each dummy function, to suppress this warning.

   * This implementation exploits the fact that older versions of GCC,
     which do not support _Static_assert, also do not warn about the
     last declaration mentioned above.

   * GCC warns if -Wnested-externs is enabled and verify() is used
     within a function body; but inside a function, you can always
     arrange to use verify_expr() instead.

   * In C++, any struct definition inside sizeof is invalid.
     Use a template type to work around the problem.  */

/* Concatenate two preprocessor tokens.  */
# define _GL_CONCAT(x, y) _GL_CONCAT0 (x, y)
# define _GL_CONCAT0(x, y) x##y

/* _GL_COUNTER is an integer, preferably one that changes each time we
   use it.  Use __COUNTER__ if it works, falling back on __LINE__
   otherwise.  __LINE__ isn't perfect, but it's better than a
   constant.  */
# if defined __COUNTER__ && __COUNTER__ != __COUNTER__
#  define _GL_COUNTER __COUNTER__
# else
#  define _GL_COUNTER __LINE__
# endif

/* Generate a symbol with the given prefix, making it unique if
   possible.  */
# define _GL_GENSYM(prefix) _GL_CONCAT (prefix, _GL_COUNTER)

/* Verify requirement R at compile-time, as an integer constant expression
   that returns 1.  If R is false, fail at compile-time, preferably
   with a diagnostic that includes the string-literal DIAGNOSTIC.  */

# define _GL_VERIFY_TRUE(R, DIAGNOSTIC) \
    (!!sizeof (_GL_VERIFY_TYPE (R, DIAGNOSTIC)))

# ifdef __cplusplus
#  if !GNULIB_defined_struct__gl_verify_type
template <int w>
  struct _gl_verify_type {
    unsigned int _gl_verify_error_if_negative: w;
  };
#   define GNULIB_defined_struct__gl_verify_type 1
#  endif
#  define _GL_VERIFY_TYPE(R, DIAGNOSTIC) \
    _gl_verify_type<(R) ? 1 : -1>
# elif defined _GL_HAVE__STATIC_ASSERT
#  define _GL_VERIFY_TYPE(R, DIAGNOSTIC) \
     struct {                                   \
       _Static_assert (R, DIAGNOSTIC);          \
       int _gl_dummy;                          \
     }
# else
#  define _GL_VERIFY_TYPE(R, DIAGNOSTIC) \
     struct { unsigned int _gl_verify_error_if_negative: (R) ? 1 : -1; }
# endif

/* Verify requirement R at compile-time, as a declaration without a
   trailing ';'.  If R is false, fail at compile-time, preferably
   with a diagnostic that includes the string-literal DIAGNOSTIC.

   Unfortunately, unlike C11, this implementation must appear as an
   ordinary declaration, and cannot appear inside struct { ... }.  */

# ifdef _GL_HAVE__STATIC_ASSERT
#  define _GL_VERIFY _Static_assert
# else
#  define _GL_VERIFY(R, DIAGNOSTIC)				       \
     extern int (*_GL_GENSYM (_gl_verify_function) (void))	       \
       [_GL_VERIFY_TRUE (R, DIAGNOSTIC)]
# endif

/* _GL_STATIC_ASSERT_H is defined if this code is copied into assert.h.  */
# ifdef _GL_STATIC_ASSERT_H
#  if !defined _GL_HAVE__STATIC_ASSERT && !defined _Static_assert
#   define _Static_assert(R, DIAGNOSTIC) _GL_VERIFY (R, DIAGNOSTIC)
#  endif
#  if !defined _GL_HAVE_STATIC_ASSERT && !defined static_assert
#   define static_assert _Static_assert /* C11 requires this #define.  */
#  endif
# endif

/* @assert.h omit start@  */

/* Each of these macros verifies that its argument R is nonzero.  To
   be portable, R should be an integer constant expression.  Unlike
   assert (R), there is no run-time overhead.

   There are two macros, since no single macro can be used in all
   contexts in C.  verify_true (R) is for scalar contexts, including
   integer constant expression contexts.  verify (R) is for declaration
   contexts, e.g., the top level.  */

/* Verify requirement R at compile-time, as an integer constant expression.
   Return 1.  This is equivalent to verify_expr (R, 1).

   verify_true is obsolescent; please use verify_expr instead.  */

# define verify_true(R) _GL_VERIFY_TRUE (R, "verify_true (" #R ")")

/* Verify requirement R at compile-time.  Return the value of the
   expression E.  */

# define verify_expr(R, E) \
    (_GL_VERIFY_TRUE (R, "verify_expr (" #R ", " #E ")") ? (E) : (E))

/* Verify requirement R at compile-time, as a declaration without a
   trailing ';'.  */

# define verify(R) _GL_VERIFY (R, "verify (" #R ")")

/* @assert.h omit end@  */

#endif

///////////////////////////////////////////////////////////////////////////////

/* Open a write stream around a malloc'd string.
   Copyright (C) 2010 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* Written by Eric Blake <address@hidden>, 2010.  */

/* Specification.  */
#include <stdio.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

# define INITIAL_ALLOC 64

struct data
{
  char **buf; /* User's argument.  */
  size_t *len; /* User's argument.  Smaller of pos or eof.  */
  size_t pos; /* Current position.  */
  size_t eof; /* End-of-file position.  */
  size_t allocated; /* Allocated size of *buf, always > eof.  */
  char c; /* Temporary storage for byte overwritten by NUL, if pos < eof.  */
};
typedef struct data data;

/* Stupid BSD interface uses int/int instead of ssize_t/size_t.  */
verify (sizeof (int) <= sizeof (size_t));
verify (sizeof (int) <= sizeof (ssize_t));

static int
mem_write (void *c, const char *buf, int n)
{
  data *cookie = c;
  char *cbuf = *cookie->buf;

  /* Be sure we don't overflow.  */
  if ((ssize_t) (cookie->pos + n) < 0)
    {
      errno = EFBIG;
      return EOF;
    }
  /* Grow the buffer, if necessary.  Use geometric growth to avoid
     quadratic realloc behavior.  Overallocate, to accomodate the
     requirement to always place a trailing NUL not counted by length.
     Thus, we want max(prev_size*1.5, cookie->pos+n+1).  */
  if (cookie->allocated <= cookie->pos + n)
    {
      size_t newsize = cookie->allocated * 3 / 2;
      if (newsize < cookie->pos + n + 1)
        newsize = cookie->pos + n + 1;
      cbuf = realloc (cbuf, newsize);
      if (!cbuf)
        return EOF;
      *cookie->buf = cbuf;
      cookie->allocated = newsize;
    }
  /* If we have previously done a seek beyond eof, ensure all
     intermediate bytges are NUL.  */
  if (cookie->eof < cookie->pos)
    memset (cbuf + cookie->eof, '\0', cookie->pos - cookie->eof);
  memcpy (cbuf + cookie->pos, buf, n);
  cookie->pos += n;
  /* If the user has previously written beyond the current position,
     remember what the trailing NUL is overwriting.  Otherwise,
     extend the stream.  */
  if (cookie->eof < cookie->pos)
    cookie->eof = cookie->pos;
  else
    cookie->c = cbuf[cookie->pos];
  cbuf[cookie->pos] = '\0';
  *cookie->len = cookie->pos;
  return n;
}

static fpos_t
mem_seek (void *c, fpos_t pos, int whence)
{
  data *cookie = c;
  off_t offset = pos;

  if (whence == SEEK_CUR)
    offset += cookie->pos;
  else if (whence == SEEK_END)
    offset += cookie->eof;
  if (offset < 0)
    {
      errno = EINVAL;
      offset = -1;
    }
  else if ((size_t) offset != offset)
    {
      errno = ENOSPC;
      offset = -1;
    }
  else
    {
      if (cookie->pos < cookie->eof)
        {
          (*cookie->buf)[cookie->pos] = cookie->c;
          cookie->c = '\0';
        }
      cookie->pos = offset;
      if (cookie->pos < cookie->eof)
        {
          cookie->c = (*cookie->buf)[cookie->pos];
          (*cookie->buf)[cookie->pos] = '\0';
          *cookie->len = cookie->pos;
        }
      else
        *cookie->len = cookie->eof;
    }
  return offset;
}

static int
mem_close (void *c)
{
  data *cookie = c;
  char *buf;

  /* Be nice and try to reduce excess memory.  */
  buf = realloc (*cookie->buf, *cookie->len + 1);
  if (buf)
    *cookie->buf = buf;
  free (cookie);
  return 0;
}

FILE *
open_memstream (char **buf, size_t *len)
{
  FILE *f;
  data *cookie;

  if (!buf || !len)
    {
      errno = EINVAL;
      return NULL;
    }
  if (!(cookie = malloc (sizeof *cookie)))
    return NULL;
  if (!(*buf = malloc (INITIAL_ALLOC)))
    {
      free (cookie);
      errno = ENOMEM;
      return NULL;
    }
  **buf = '\0';
  *len = 0;

  f = funopen (cookie, NULL, mem_write, mem_seek, mem_close);
  if (!f)
    {
      int saved_errno = errno;
      free (cookie);
      errno = saved_errno;
    }
  else
    {
      cookie->buf = buf;
      cookie->len = len;
      cookie->pos = 0;
      cookie->eof = 0;
      cookie->c = '\0';
      cookie->allocated = INITIAL_ALLOC;
    }
  return f;
}

///////////////////////////////////////////////////////////////////////////////


//
// Copyright 2012 Jeff Verkoeyen
// Originally ported from https://github.com/ingenuitas/python-tesseract/blob/master/fmemopen.c
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

struct fmem {
  size_t pos;
  size_t size;
  char *buffer;
};
typedef struct fmem fmem_t;

static int readfn(void *handler, char *buf, int size) {
  fmem_t *mem = handler;
  size_t available = mem->size - mem->pos;

  if (size > available) {
    size = available;
  }
  memcpy(buf, mem->buffer + mem->pos, sizeof(char) * size);
  mem->pos += size;

  return size;
}

static int writefn(void *handler, const char *buf, int size) {
  fmem_t *mem = handler;
  size_t available = mem->size - mem->pos;

  if (size > available) {
    size = available;
  }
  memcpy(mem->buffer + mem->pos, buf, sizeof(char) * size);
  mem->pos += size;

  return size;
}

static fpos_t seekfn(void *handler, fpos_t offset, int whence) {
  size_t pos;
  fmem_t *mem = handler;

  switch (whence) {
    case SEEK_SET: pos = offset; break;
    case SEEK_CUR: pos = mem->pos + offset; break;
    case SEEK_END: pos = mem->size + offset; break;
    default: return -1;
  }

  if (pos > mem->size) {
    return -1;
  }

  mem->pos = pos;
  return (fpos_t)pos;
}

static int closefn(void *handler) {
  free(handler);
  return 0;
}

FILE *fmemopen(void *buf, size_t size, const char *mode) {
  // This data is released on fclose.
  fmem_t* mem = (fmem_t *) malloc(sizeof(fmem_t));

  // Zero-out the structure.
  memset(mem, 0, sizeof(fmem_t));

  mem->size = size;
  mem->buffer = buf;

  // funopen's man page: https://developer.apple.com/library/mac/#documentation/Darwin/Reference/ManPages/man3/funopen.3.html
  return funopen(mem, readfn, writefn, seekfn, closefn);
}

#endif
