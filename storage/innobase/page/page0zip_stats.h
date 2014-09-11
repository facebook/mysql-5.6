/*****************************************************************************

Copyright (c) 2005, 2014, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2012, Facebook Inc.

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
@file page/page0zip_stats.h
compression stats
*******************************************************/

#ifndef PAGE0ZIP_STATS_H
#define PAGE0ZIP_STATS_H

#include "fil0fil.h"

inline
void
page_zip_update_zip_stats_compress(
	page_zip_stat_t*	zip_stat,	/*!<in: stat to update */
	ulonglong		time,		/*!<in: time */
	bool			is_success,	/*!<in: compression suceed? */
	bool			is_clust)	/*!<in: cluster index? */
{
	zip_stat->compressed++;
	zip_stat->compressed_time += time;
	if (is_success) {
		zip_stat->compressed_ok++;
		zip_stat->compressed_ok_time += time;
	}
	if (is_clust) {
		zip_stat->compressed_primary++;
		zip_stat->compressed_primary_time += time;
		if (is_success) {
			zip_stat->compressed_primary_ok++;
			zip_stat->compressed_primary_ok_time += time;
		}
	} else {
		zip_stat->compressed_secondary++;
		zip_stat->compressed_secondary_time += time;
		if (is_success) {
			zip_stat->compressed_secondary_ok++;
			zip_stat->compressed_secondary_ok_time += time;
		}
	}
}

inline
void
page_zip_update_zip_stats_decompress(
	page_zip_stat_t*	zip_stat,	/*!<in: stat to update */
	ulonglong		time,		/*!<in: time */
	bool			is_clust)	/*!<in: cluster index? */
{
	zip_stat->decompressed++;
	zip_stat->decompressed_time += time;
	if (is_clust) {
		zip_stat->decompressed_primary++;
		zip_stat->decompressed_primary_time += time;
	} else {
		zip_stat->decompressed_secondary++;
		zip_stat->decompressed_secondary_time += time;
	}
}

inline
void
page_zip_update_fil_comp_stats_compress(
	ulint		space_id,	/*!<in: table space id */
	ulint		zip_size,	/*!<in: compressed page size */
	ulonglong	time,		/*!<in: time */
	bool		is_success,	/*!<in: compression succeed? */
	bool		is_clust,	/*!<in: cluster index? */
	ulint		padding)	/*!<in: padding size */
{
	ib_mutex_t* mutex;
	fil_stats_t* stats = fil_get_stats_lock_mutex_by_id(space_id, &mutex);
	if (!stats) {
		mutex_exit(mutex);
		return;
	}
	++stats->comp_stats.compressed;
	stats->comp_stats.page_size = zip_size;
	stats->comp_stats.compressed_time += time;
	if (is_success) {
		++stats->comp_stats.compressed_ok;
		stats->comp_stats.compressed_ok_time += time;
	}
	if (is_clust) {
		++stats->comp_stats.compressed_primary;
		stats->comp_stats.compressed_primary_time += time;
		/* only update the padding for table if this is the
		primary index */
		stats->comp_stats.padding = padding;
		if (is_success) {
			++stats->comp_stats.compressed_primary_ok;
			stats->comp_stats.compressed_primary_ok_time +=
				time;
		}
	}
	mutex_exit(mutex);
}

inline
void
page_zip_update_fil_comp_stats_decompress(
	ulint		space_id,	/*!<in: table space id */
	ulint		zip_size,	/*!<in: compressed page size */
	ulonglong	time)		/*!<in: time */
{
	ib_mutex_t* mutex;
	fil_stats_t* stats = fil_get_stats_lock_mutex_by_id(space_id, &mutex);
	if (stats) {
		++stats->comp_stats.decompressed;
		stats->comp_stats.decompressed_time += time;
		stats->comp_stats.page_size = zip_size;
	}
	mutex_exit(mutex);
}
#endif
