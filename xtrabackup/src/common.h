/******************************************************
Copyright (c) 2011 Percona Ireland Ltd.

Common declarations for XtraBackup.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

*******************************************************/

#ifndef XB_COMMON_H
#define XB_COMMON_H

#include <my_global.h>
#include <mysql_version.h>
#include <fcntl.h>

#define xb_a(expr)							\
	do {								\
		if (!(expr)) {						\
			msg("Assertion \"%s\" failed at %s:%lu\n",	\
			    #expr, __FILE__, (unsigned long) __LINE__); \
			abort();					\
		}							\
	} while (0);

#ifdef XB_DEBUG
#define xb_ad(expr) xb_a(expr)
#else
#define xb_ad(expr)
#endif

static inline int msg(const char *fmt, ...) ATTRIBUTE_FORMAT(printf, 1, 2);
static inline int msg(const char *fmt, ...)
{
	int	result;
	va_list args;

	va_start(args, fmt);
	result = vfprintf(stderr, fmt, args);
	va_end(args);
	return result;
}

#if MYSQL_VERSION_ID >= 50500
# define MY_FREE(a) my_free(a)
#else
# define MY_FREE(a) my_free(a, MYF(0))
#endif

/* Use POSIX_FADV_NORMAL when available */

#ifdef POSIX_FADV_NORMAL
#define USE_POSIX_FADVISE
#endif

typedef enum {
	XB_STREAM_FMT_NONE,
	XB_STREAM_FMT_TAR,
	XB_STREAM_FMT_XBSTREAM
} xb_stream_fmt_t;

#endif
