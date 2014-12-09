/*****************************************************************************

Copyright (c) 1994, 2013, Oracle and/or its affiliates. All Rights Reserved.

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

/**************************************************//**
@file include/page0types.h
Index page routines

Created 2/2/1994 Heikki Tuuri
*******************************************************/

#ifndef page0types_h
#define page0types_h

using namespace std;

#include <map>

#include "univ.i"
#include "dict0types.h"
#include "mtr0types.h"
#include "sync0types.h"
#include "os0thread.h"

/** Eliminates a name collision on HP-UX */
#define page_t	   ib_page_t
/** Type of the index page */
typedef	byte		page_t;
/** Index page cursor */
struct page_cur_t;

/** Compressed index page */
typedef byte		page_zip_t;

/* The following definitions would better belong to page0zip.h,
but we cannot include page0zip.h from rem0rec.ic, because
page0*.h includes rem0rec.h and may include rem0rec.ic. */

/** Number of bits needed for representing different compressed page sizes */
#define PAGE_ZIP_SSIZE_BITS 3

/** Maximum compressed page shift size */
#define PAGE_ZIP_SSIZE_MAX	\
	(UNIV_ZIP_SIZE_SHIFT_MAX - UNIV_ZIP_SIZE_SHIFT_MIN + 1)

/* Make sure there are enough bits available to store the maximum zip
ssize, which is the number of shifts from 512. */
#if PAGE_ZIP_SSIZE_MAX >= (1 << PAGE_ZIP_SSIZE_BITS)
# error "PAGE_ZIP_SSIZE_MAX >= (1 << PAGE_ZIP_SSIZE_BITS)"
#endif

/** Compressed page descriptor */
struct page_zip_des_t
{
	page_zip_t*	data;		/*!< compressed page data */

#ifdef UNIV_DEBUG
	unsigned	m_start:16;	/*!< start offset of modification log */
	bool		m_external;	/*!< Allocated externally, not from the
					buffer pool */
#endif /* UNIV_DEBUG */
	unsigned	m_end:16;	/*!< end offset of modification log */
	unsigned	m_nonempty:1;	/*!< TRUE if the modification log
					is not empty */
	unsigned	n_blobs:12;	/*!< number of externally stored
					columns on the page; the maximum
					is 744 on a 16 KiB page */
	unsigned	ssize:PAGE_ZIP_SSIZE_BITS;
					/*!< 0 or compressed page shift size;
					the size in bytes is
					(UNIV_ZIP_SIZE_MIN >> 1) << ssize. */
};

/** Compression statistics for a given page size */
struct page_zip_stat_t {
	/** Number of page compressions */
	ulint		compressed;
	/** Number of primary index page compressions */
	ulint		compressed_primary;
	/** Number of secondary index page compressions */
	ulint		compressed_secondary;
	/** Number of successful page compressions */
	ulint		compressed_ok;
	/** Number of successful primary index page compressions */
	ulint		compressed_primary_ok;
	/** Number of succesful secondary index page compressions */
	ulint		compressed_secondary_ok;
	/** Number of page decompressions */
	ulint		decompressed;
	/** Number of primary index page decompressions */
	ulint		decompressed_primary;
	/** Number of secondary index page decompressions */
	ulint		decompressed_secondary;
	/** Duration of page compressions */
	ulonglong	compressed_time;
	/** Duration of primary index page compressions */
	ulonglong	compressed_primary_time;
	/** Duration of secondary index page compressions */
	ulonglong	compressed_secondary_time;
	/** Duration of successful page compressions */
	ulonglong	compressed_ok_time;
	/** Duration of successful primary index page compressions */
	ulonglong	compressed_primary_ok_time;
	/** Duration of successful secondary index page compressions */
	ulonglong	compressed_secondary_ok_time;
	/** Duration of page decompressions */
	ulonglong	decompressed_time;
	/** Duration of primary index page decompressions */
	ulonglong	decompressed_primary_time;
	/** Duration of secondary index page decompressions */
	ulonglong	decompressed_secondary_time;

	page_zip_stat_t() :
		/* Initialize members to 0 so that when we do
		stlmap[key].compressed++ and element with "key" does not
		exist it gets inserted with zeroed members. */
		compressed(0),
		compressed_ok(0),
		decompressed(0),
		compressed_time(0),
		compressed_ok_time(0),
		decompressed_time(0)
	{ }
};

/** Compression statistics types */
typedef map<index_id_t, page_zip_stat_t>	page_zip_stat_per_index_t;

/** Statistics on compression, indexed by page_zip_des_t::ssize - 1 */
extern page_zip_stat_t				page_zip_stat[PAGE_ZIP_SSIZE_MAX];
/** Statistics on compression, indexed by dict_index_t::id */
extern page_zip_stat_per_index_t		page_zip_stat_per_index;
extern ib_mutex_t				page_zip_stat_per_index_mutex;
#ifdef HAVE_PSI_INTERFACE
extern mysql_pfs_key_t				page_zip_stat_per_index_mutex_key;
#endif /* HAVE_PSI_INTERFACE */

#endif
