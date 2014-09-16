/*****************************************************************************

Copyright (c) 1994, 2009, Oracle and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************/

/*****************************************************************//**
@file ut/ut0dbg.cc
Debug utilities for Innobase.

Created 1/30/1994 Heikki Tuuri
**********************************************************************/

#include "univ.i"
#include "ut0dbg.h"
#ifndef UNIV_HOTBACKUP
# include "ha_prototypes.h"
#endif /* !UNIV_HOTBACKUP */

#if defined(__GNUC__) && (__GNUC__ > 2)
#else
/** This is used to eliminate compiler warnings */
UNIV_INTERN ulint	ut_dbg_zero	= 0;
#endif

/*************************************************************//**
Report a failed assertion. */
UNIV_INTERN
void
ut_dbg_assertion_failed(
/*====================*/
	const char* expr,	/*!< in: the failed assertion (optional) */
	const char* file,	/*!< in: source file containing the assertion */
	ulint line)		/*!< in: line number of the assertion */
{
	ut_print_timestamp(stderr);
#ifdef UNIV_HOTBACKUP
	fprintf(stderr, "  InnoDB: Assertion failure in file %s line %lu\n",
		file, line);
#else /* UNIV_HOTBACKUP */
	fprintf(stderr,
		"  InnoDB: Assertion failure in thread %lu"
		" in file %s line %lu\n",
		os_thread_pf(os_thread_get_curr_id()),
		innobase_basename(file), line);
#endif /* UNIV_HOTBACKUP */
	if (expr) {
		fprintf(stderr,
			"InnoDB: Failing assertion: %s\n", expr);
	}

	fputs("InnoDB: We intentionally generate a memory trap.\n"
	      "InnoDB: Submit a detailed bug report"
	      " to http://bugs.mysql.com.\n"
	      "InnoDB: If you get repeated assertion failures"
	      " or crashes, even\n"
	      "InnoDB: immediately after the mysqld startup, there may be\n"
	      "InnoDB: corruption in the InnoDB tablespace. Please refer to\n"
	      "InnoDB: " REFMAN "forcing-innodb-recovery.html\n"
	      "InnoDB: about forcing recovery.\n", stderr);
}

#ifdef UNIV_COMPILE_TEST_FUNCS

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <unistd.h>

#ifndef timersub
#define timersub(a, b, r)						\
	do {								\
		(r)->tv_sec = (a)->tv_sec - (b)->tv_sec;		\
		(r)->tv_usec = (a)->tv_usec - (b)->tv_usec;		\
		if ((r)->tv_usec < 0) {					\
			(r)->tv_sec--;					\
			(r)->tv_usec += 1000000;			\
		}							\
	} while (0)
#endif /* timersub */

/*******************************************************************//**
Resets a speedo (records the current time in it). */
UNIV_INTERN
void
speedo_reset(
/*=========*/
	speedo_t*	speedo)	/*!< out: speedo */
{
	gettimeofday(&speedo->tv, NULL);

	getrusage(RUSAGE_SELF, &speedo->ru);
}

/*******************************************************************//**
Shows the time elapsed and usage statistics since the last reset of a
speedo. */
UNIV_INTERN
void
speedo_show(
/*========*/
	const speedo_t*	speedo)	/*!< in: speedo */
{
	struct rusage	ru_now;
	struct timeval	tv_now;
	struct timeval	tv_diff;

	getrusage(RUSAGE_SELF, &ru_now);

	gettimeofday(&tv_now, NULL);

#define PRINT_TIMEVAL(prefix, tvp)		\
	fprintf(stderr, "%s% 5ld.%06ld sec\n",	\
		prefix, (tvp)->tv_sec, (tvp)->tv_usec)

	timersub(&tv_now, &speedo->tv, &tv_diff);
	PRINT_TIMEVAL("real", &tv_diff);

	timersub(&ru_now.ru_utime, &speedo->ru.ru_utime, &tv_diff);
	PRINT_TIMEVAL("user", &tv_diff);

	timersub(&ru_now.ru_stime, &speedo->ru.ru_stime, &tv_diff);
	PRINT_TIMEVAL("sys ", &tv_diff);
}

#endif /* UNIV_COMPILE_TEST_FUNCS */

/**********************************************************************//**
Dump a block of memory on the standard error stream, "hexdump -C"-style. */
UNIV_INTERN
void
ut_hexdump_func(
/*==================*/
	const char*	name,	/*!< in: name of the data structure */
	const void*	buf,	/*!< in: data */
	ulint		size)	/*!< in: length of the data, in bytes */
{
	const byte*	s	= static_cast<const byte*>(buf);
	ulint		addr;
	const ulint	width	= 16; /* bytes per line */

	fprintf(stderr, "%s:\n", name);

	for (addr = 0; addr < size; addr += width) {
		// line prefix: relative address of first byte displayed in line
		fprintf(stderr, "%08lx ", (ulong) addr);

		ulong const line_bytes = ut_min(width, size - addr);

		// output hex-encoded bytes
		for (ulong x = 0; x < line_bytes; ++x)
		{
			if (x % 8 == 0)
			{
				putc(' ', stderr);
			}

			fprintf(stderr, " %02x", (byte) s[x]);
		}

		// get the formatting right: add padding spaces in last line
		for (ulong x = line_bytes; x < width; ++x)
		{
			if (x % 8 == 0)
			{
				fputs("    ", stderr);
			} else {
				fputs("   ", stderr);
			}
		}

		// print out the data itself, but replace all characters that
		// are not printable characters with '.'
		fputs("  |", stderr);

		for (ulong x = 0; x < line_bytes; ++x)
		{
			char const c = s[x];
			if (c >= 0x20 && c < 0x7f)
			{
				// 7 bit ASCII printable character
				putc(c, stderr);
			} else {
				putc('.', stderr);
			}
		}

		fputs("|\n", stderr);

		s += line_bytes;
	}

	// print out relative address of the end of the hexdumped data
	fprintf(stderr, "%08lx\n", (ulong) size);
}
