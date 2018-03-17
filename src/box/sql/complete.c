/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * An tokenizer for SQL
 *
 * This file contains C code that implements the sqlite3_complete() API.
 * This code used to be part of the tokenizer.c source file.  But by
 * separating it out, the code will be automatically omitted from
 * static links that do not use it.
 */
#include "sqliteInt.h"
#ifndef SQLITE_OMIT_COMPLETE

/*
 * This is defined in tokenize.c.  We just have to import the definition.
 */
#ifndef SQLITE_AMALGAMATION
#ifdef SQLITE_ASCII
#define IdChar(C)  ((sqlite3CtypeMap[(unsigned char)C]&0x46)!=0)
#endif
#ifdef SQLITE_EBCDIC
extern const char sqlite3IsEbcdicIdChar[];
#define IdChar(C)  (((c=C)>=0x42 && sqlite3IsEbcdicIdChar[c-0x40]))
#endif
#endif				/* SQLITE_AMALGAMATION */

/*
 * Token types used by the sqlite3_complete() routine.  See the header
 * comments on that procedure for additional information.
 */
#define tkSEMI    0
#define tkWS      1
#define tkOTHER   2
#ifndef SQLITE_OMIT_TRIGGER
#define tkEXPLAIN 3
#define tkCREATE  4
#define tkTEMP    5
#define tkTRIGGER 6
#define tkEND     7
#endif

/*
 * Return TRUE if the given SQL string ends in a semicolon.
 *
 * Special handling is require for CREATE TRIGGER statements.
 * Whenever the CREATE TRIGGER keywords are seen, the statement
 * must end with ";END;".
 *
 * This implementation uses a state machine with 8 states:
 *
 *   (0) INVALID   We have not yet seen a non-whitespace character.
 *
 *   (1) START     At the beginning or end of an SQL statement.  This routine
 *                 returns 1 if it ends in the START state and 0 if it ends
 *                 in any other state.
 *
 *   (2) NORMAL    We are in the middle of statement which ends with a single
 *                 semicolon.
 *
 *   (3) EXPLAIN   The keyword EXPLAIN has been seen at the beginning of
 *                 a statement.
 *
 *   (4) CREATE    The keyword CREATE has been seen at the beginning of a
 *                 statement, possibly preceded by EXPLAIN and/or followed by
 *                 TEMP or TEMPORARY
 *
 *   (5) TRIGGER   We are in the middle of a trigger definition that must be
 *                 ended by a semicolon, the keyword END, and another semicolon.
 *
 *   (6) SEMI      We've seen the first semicolon in the ";END;" that occurs at
 *                 the end of a trigger definition.
 *
 *   (7) END       We've seen the ";END" of the ";END;" that occurs at the end
 *                 of a trigger definition.
 *
 * Transitions between states above are determined by tokens extracted
 * from the input.  The following tokens are significant:
 *
 *   (0) tkSEMI      A semicolon.
 *   (1) tkWS        Whitespace.
 *   (2) tkOTHER     Any other SQL token.
 *   (3) tkEXPLAIN   The "explain" keyword.
 *   (4) tkCREATE    The "create" keyword.
 *   (5) tkTEMP      The "temp" or "temporary" keyword.
 *   (6) tkTRIGGER   The "trigger" keyword.
 *   (7) tkEND       The "end" keyword.
 *
 * Whitespace never causes a state transition and is always ignored.
 * This means that a SQL string of all whitespace is invalid.
 *
 * If we compile with SQLITE_OMIT_TRIGGER, all of the computation needed
 * to recognize the end of a trigger can be omitted.  All we have to do
 * is look for a semicolon that is not part of an string or comment.
 */


#endif				/* SQLITE_OMIT_COMPLETE */
