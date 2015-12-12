/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "archive_platform.h"
__FBSDID("$FreeBSD: head/lib/libarchive/archive_check_magic.c 201089 2009-12-28 02:20:23Z kientzle $");

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#if defined(_WIN32) && !defined(__CYGWIN__)
#include <windows.h>
#include <winbase.h>
#endif

#include "archive_private.h"

static void
errmsg(const char *m)
{
	size_t s = strlen(m);
	ssize_t written;

	while (s > 0) {
		written = write(2, m, strlen(m));
		if (written <= 0)
			return;
		m += written;
		s -= written;
	}
}

static void
diediedie(void)
{
#if defined(_WIN32) && !defined(__CYGWIN__) && defined(_DEBUG)
	/* Cause a breakpoint exception  */
	DebugBreak();
#endif
	abort();        /* Terminate the program abnormally. */
}

static const char *
state_name(unsigned s)
{
	switch (s) {
	case ARCHIVE_STATE_NEW:		return ("new");
	case ARCHIVE_STATE_HEADER:	return ("header");
	case ARCHIVE_STATE_DATA:	return ("data");
	case ARCHIVE_STATE_EOF:		return ("eof");
	case ARCHIVE_STATE_CLOSED:	return ("closed");
	case ARCHIVE_STATE_FATAL:	return ("fatal");
	default:			return ("??");
	}
}


static void
write_all_states(unsigned int states)
{
	unsigned int lowbit;

	/* A trick for computing the lowest set bit. */
	while ((lowbit = states & (1 + ~states)) != 0) {
		states &= ~lowbit;		/* Clear the low bit. */
		errmsg(state_name(lowbit));
		if (states != 0)
			errmsg("/");
	}
}

/*
 * Check magic value and current state; bail if it isn't valid.
 *
 * This is designed to catch serious programming errors that violate
 * the libarchive API.
 */
void
__archive_check_magic(struct archive *a, unsigned int magic,
    unsigned int state, const char *function)
{
	if (a->magic != magic) {
		errmsg("INTERNAL ERROR: Function ");
		errmsg(function);
		errmsg(" invoked with invalid struct archive structure.\n");
		diediedie();
	}

	if (state == ARCHIVE_STATE_ANY)
		return;

	if ((a->state & state) == 0) {
		errmsg("INTERNAL ERROR: Function '");
		errmsg(function);
		errmsg("' invoked with archive structure in state '");
		write_all_states(a->state);
		errmsg("', should be in state '");
		write_all_states(state);
		errmsg("'\n");
		diediedie();
	}
}
