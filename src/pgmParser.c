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

#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "pgmParser.h"
#include "program.h"

#define TKN_ERROR -3
#define TKN_VOID -2
#define TKN_EOF -1
#define TKN_REFERENCE ((int)'@')
#define TKN_STRING 256

/* Parser return codes */

#define P_OK 0      /* Accepted */
#define P_WARNING 1 /* Warning, parsing continues */
#define P_ERROR 2   /* Fatal, parsing stopped */

/* Buffer sizes */

#define STRINGBUFFERSZ 280
#define SYMBOLSIZE 256
#define VALUESIZE 256

typedef int B3TokenType;
typedef int ParseReturnCode;

typedef struct _parserstate {
	void*       p;
	const char* fileName;
	FILE*       fp;
	int         lineNumber;
	B3TokenType nextToken;
	char        stringBuffer[STRINGBUFFERSZ];
} ParserState;

/*
 * New syntax: 8-apr-03
 * S ::= <pgmdef> [<pgmdef>...]
 * <pgmdef> ::= [<program-number>] '{' <assignment> [[,] <assignment> ...] '}'
 * <program-number> ::= <number>
 * <assignment> ::= <parameter> '=' <expression>
 * <expression> ::= {<number>|<constant>|<program-reference>}
 * <program-reference> ::= '@' <program-number>
 *
 */

/*
 * Scans the next token from the input. Tokens are:
 * '{' '}' '=' string EOF
 */
static int
getToken (FILE* fp, int* linePtr, char* tokbuf, size_t tblen)
{
	int    c;
	size_t tp      = 0;
	int    tokType = TKN_VOID;
	int    state   = 1;

	tokbuf[tp]     = '\0';
	tokbuf[tp + 1] = '\0';

	/* Scan leading space */
	while (0 < state) {
		c = fgetc (fp);
		if (c == EOF)
			return TKN_EOF;

		if (state == 1) {
			if (c == '\n') {
				*linePtr += 1;
				continue;
			} else if (isspace (c)) {
				continue;
			} else if (c == '#') {
				state = 2;
			} else {
				state = 0;
			}
		} else if (state == 2) { /* Comment to end of line */
			if (c == '\n') {
				*linePtr += 1;
				state = 1;
			}
		}
	}

	/* Examine character */
	if ((c == '{') || (c == '}') || (c == '=') || (c == ',')) {
		tokType   = c; /* The character is its own token */
		tokbuf[0] = c;
		tokbuf[1] = '\0';
	} else {
		tokType = TKN_STRING;

		if (c == '"') {
			state = 0;

			for (;;) {
				c = fgetc (fp);

				if (c == EOF) {
					tokType = TKN_ERROR;
					strncpy (tokbuf, "End of file in quoted string", tblen);
					tokbuf[tblen - 1] = '\0';
					break;
				}

				if (state == 0) {
					if (c == '"') {
						break;          /* End of quoted string */
					} else if (c == '\\') { /* Next char is escape char */
						state = 1;
						continue;
					} else if (tp < tblen) { /* Append char to token buffer */
						tokbuf[tp++] = c;
					}
				} else if (state == 1) { /* Escaped char */
					if (tp < tblen) {
						tokbuf[tp++] = c;
					}
					state = 0;
				}
			}
		} else {
			while (isalnum (c) || (c == '-') || (c == '.') || (c == '_') || (c == '+')) {
				if (tp < tblen) {
					tokbuf[tp++] = c;
				}
				c = fgetc (fp);
			}
			ungetc (c, fp);
		}
	}

	tokbuf[tp] = '\0';
	return tokType;
}

/*
 * Retrieves the next token from the input file and puts it in the parser
 * state.
 */
static B3TokenType
getNextToken (ParserState* ps)
{
	return ps->nextToken = getToken (ps->fp,
	                                 &(ps->lineNumber),
	                                 ps->stringBuffer,
	                                 STRINGBUFFERSZ);
}

/*
 * Idempotent predicate: does the next token match the parameter?
 */
static int
nextTokenMatches (ParserState* ps, B3TokenType t)
{
	return ps->nextToken == t;
}

/*
 * Parses an integer number by parsing a string and then parsing it as
 * a number.
 */
static ParseReturnCode
parseInteger (ParserState* ps, int* value)
{
	if (nextTokenMatches (ps, TKN_STRING)) {
		if (sscanf (ps->stringBuffer, "%d", value) == 1) {
			(void)getNextToken (ps);
			return P_OK;
		}
	}
	return P_ERROR; /* Not an integer */
}

/*
 * Parses an identifier (actually, a string).
 */
static ParseReturnCode
parseIdentifier (ParserState* ps, char* identifier)
{
	if (!nextTokenMatches (ps, TKN_STRING)) {
		return P_ERROR;
	} else {
		/* Should be a setter function */
		strncpy (identifier, ps->stringBuffer, SYMBOLSIZE - 1);
		identifier[SYMBOLSIZE - 1] = '\0';
	}
	(void)getNextToken (ps);
	return P_OK;
}

/*
 * Parses a single token.
 */
static ParseReturnCode
parseToken (ParserState* ps, int token)
{
	if (nextTokenMatches (ps, token)) {
		(void)getNextToken (ps);
		return P_OK;
	} else {
		return P_ERROR;
	}
}

/*
 * This method is just a syntactic element since it equates an expression
 * with an identifier.
 */
static ParseReturnCode
parseExpression (ParserState* ps, char* value)
{
	return parseIdentifier (ps, value);
}

/*
 * Prints a message on standard output giving a message, the filename and
 * the linenumber.
 */
static ParseReturnCode
stateMessage (ParserState*    ps,
              ParseReturnCode code,
              const char*     msg)
{
	if (code == P_WARNING) {
		fprintf (stderr, "WARNING : ");
	} else if (code == P_ERROR) {
		fprintf (stderr, "ERROR : ");
	}
	fprintf (stderr, "%s : in file %s on line %d\n", msg, ps->fileName, ps->lineNumber);
	return code;
}

/*
 * Parses a list of programme property assignments and sends each to the
 * application via a call to bindToProgram(...);
 */
static ParseReturnCode
parseAssignmentList (ParserState* ps, const int pgmNr)
{
	ParseReturnCode R;
	char            symbol[SYMBOLSIZE];
	char            value[VALUESIZE];
	char            msg[STRINGBUFFERSZ];

	while (!nextTokenMatches (ps, '}')) {
		if ((R = parseIdentifier (ps, symbol)) == P_ERROR) {
			return stateMessage (ps, R, "identifier expected.");
		}

		if ((R = parseToken (ps, '=')) == P_ERROR) {
			snprintf (msg, sizeof(msg), "'=' expected after '%s'", symbol);
			return stateMessage (ps, R, msg);
		}

		if ((R = parseExpression (ps, value)) == P_ERROR) {
			snprintf (msg, sizeof(msg), "bad expression after '%s='", symbol);
			return stateMessage (ps, R, msg);
		}

		if (bindToProgram (ps->p, ps->fileName, ps->lineNumber, pgmNr, symbol, value)) {
			return P_ERROR;
		}

		if (nextTokenMatches (ps, ',')) {
			(void)getNextToken (ps);
		}
	}
	(void)getNextToken (ps);
	return P_OK;
}

/*
 * Parses one program definition by reading a programme number followed
 * by an assignment list.
 */
static ParseReturnCode
parseProgramDefinition (ParserState* ps)
{
	ParseReturnCode R;
	int             programNumber;
	if ((R = parseInteger (ps, &programNumber)) == P_ERROR) {
		return stateMessage (ps, R, "program number expected");
	}
	if ((R = parseToken (ps, '{')) == P_ERROR) {
		return stateMessage (ps, R, "assignment list expected");
	}
	return parseAssignmentList (ps, programNumber);
}

/*
 * Parse all program definitions from the current parser state to the
 * end of the file.
 */
static ParseReturnCode
parseProgramDefinitionList (ParserState* ps)
{
	ParseReturnCode R;
	while (!nextTokenMatches (ps, TKN_EOF)) {
		if ((R = parseProgramDefinition (ps)) == P_ERROR) {
			return stateMessage (ps, R, "bad program definition");
		}
	}
	return P_OK;
}

/*
 * Opens the named programme file and parses its contents.
 * fileName  The path to the programme file.
 * return    0 if the file was successfully read, non-zero otherwise.
 */
int
loadProgrammeFile (void* p, char* fileName)
{
	ParserState ps;
	ps.p = p;
	if ((ps.fp = fopen (fileName, "r")) != NULL) {
		int rtn;
		ps.fileName   = fileName;
		ps.lineNumber = 0;
		getNextToken (&ps);
		rtn = (int)parseProgramDefinitionList (&ps);
		fclose (ps.fp);
		return rtn;
	} else {
		perror (fileName);
		return (int)P_ERROR;
	}
}

#ifdef LV2SYNTH
int
loadProgrammeString (void* p, char* pdef)
{
	ParserState ps;
	ps.p   = p;
	int rv = (int)P_ERROR;
#ifdef _WIN32
	char temppath[MAX_PATH - 13];
	char filename[MAX_PATH + 1];
	if (0 == GetTempPath (sizeof (temppath), temppath))
		return rv;
	if (0 == GetTempFileName (temppath, "sbfpgm", 0, filename))
		return rv;
	FILE* f = fopen (filename, "wb");
	if (NULL == f)
		return rv;
	fwrite (pdef, strlen (pdef), 1, f);
	fclose (f);
	if ((ps.fp = fopen (filename, "rb")) != NULL)
#else
	if ((ps.fp = fmemopen (pdef, strlen (pdef), "r")) != NULL)
#endif
	{
		ps.fileName   = "<string-pipe>";
		ps.lineNumber = 0;
		getNextToken (&ps);
		rv = (int)parseProgramDefinitionList (&ps);
		fclose (ps.fp);
	}
#ifdef _WIN32
	unlink (filename);
#endif
	return rv;
}
#endif
