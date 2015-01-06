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
@file page/page0zip_dir.cc
This file hosts all functionalities related to dense page directory.
*******************************************************/
#include "page0zip_dir.h"
#include "page0zip.h"
#include "page0zip_helper.h"
#include "ut0sort.h"

/**********************************************************************//**
Compare two page directory entries.
@return	positive if rec1 > rec2 */
inline
ibool
page_zip_dir_cmp(
/*=============*/
	const rec_t*	rec1,	/*!< in: rec1 */
	const rec_t*	rec2)	/*!< in: rec2 */
{
	return(rec1 > rec2);
}

/**********************************************************************//**
Sort the dense page directory by address (heap_no). */
static
void
page_zip_dir_sort(
/*==============*/
	rec_t**	arr,	/*!< in/out: dense page directory */
	rec_t**	aux_arr,/*!< in/out: work area */
	ulint	low,	/*!< in: lower bound of the sorting area, inclusive */
	ulint	high)	/*!< in: upper bound of the sorting area, exclusive */
{
	UT_SORT_FUNCTION_BODY(page_zip_dir_sort, arr, aux_arr, low, high,
			      page_zip_dir_cmp);
}

/**********************************************************************//**
Populate the dense page directory from the sparse directory. */
void
page_zip_dir_encode(
/*================*/
	page_zip_des_t*	page_zip,	/*!< dense directory is encoded in the
					bottom of page_zip->data */
	const page_t*	page,	/*!< in: compact page */
	const rec_t**	recs)	/*!< in: pointer to an array of 0, or NULL;
				out: dense page directory sorted by ascending
				address (and heap_no) */
{
	const byte*	rec;
	ulint		status;
	ulint		min_mark;
	ulint		heap_no;
	ulint		i;
	ulint		n_heap;
	ulint		offs;
	byte*		buf = page_zip->data + page_zip_get_size(page_zip);

	min_mark = 0;

	if (page_is_leaf(page)) {
		status = REC_STATUS_ORDINARY;
	} else {
		status = REC_STATUS_NODE_PTR;
		if (UNIV_UNLIKELY
		    (mach_read_from_4(page + FIL_PAGE_PREV) == FIL_NULL)) {
			min_mark = REC_INFO_MIN_REC_FLAG;
		}
	}

	n_heap = page_dir_get_n_heap(page);

	/* Traverse the list of stored records in the collation order,
	starting from the first user record. */

	rec = page + PAGE_NEW_INFIMUM;

	i = 0;

	for (;;) {
		ulint	info_bits;
		offs = rec_get_next_offs(rec, TRUE);
		if (UNIV_UNLIKELY(offs == PAGE_NEW_SUPREMUM)) {
			break;
		}
		rec = page + offs;
		heap_no = rec_get_heap_no_new(rec);
		ut_a(heap_no >= PAGE_HEAP_NO_USER_LOW);
		ut_a(heap_no < n_heap);
		ut_a(offs < UNIV_PAGE_SIZE - PAGE_DIR);
		ut_a(offs >= PAGE_ZIP_START);
#if PAGE_ZIP_DIR_SLOT_MASK & (PAGE_ZIP_DIR_SLOT_MASK + 1)
# error "PAGE_ZIP_DIR_SLOT_MASK is not 1 less than a power of 2"
#endif
#if PAGE_ZIP_DIR_SLOT_MASK < UNIV_ZIP_SIZE_MAX - 1
# error "PAGE_ZIP_DIR_SLOT_MASK < UNIV_ZIP_SIZE_MAX - 1"
#endif
		if (UNIV_UNLIKELY(rec_get_n_owned_new(rec))) {
			offs |= PAGE_ZIP_DIR_SLOT_OWNED;
		}

		info_bits = rec_get_info_bits(rec, TRUE);
		if (info_bits & REC_INFO_DELETED_FLAG) {
			info_bits &= ~REC_INFO_DELETED_FLAG;
			offs |= PAGE_ZIP_DIR_SLOT_DEL;
		}
		ut_a(info_bits == min_mark);
		/* Only the smallest user record can have
		REC_INFO_MIN_REC_FLAG set. */
		min_mark = 0;

		mach_write_to_2(buf - PAGE_ZIP_DIR_SLOT_SIZE * ++i, offs);

		if (UNIV_LIKELY_NULL(recs)) {
			/* Ensure that each heap_no occurs at most once. */
			ut_a(!recs[heap_no - PAGE_HEAP_NO_USER_LOW]);
			/* exclude infimum and supremum */
			recs[heap_no - PAGE_HEAP_NO_USER_LOW] = rec;
		}

		ut_a(rec_get_status(rec) == status);
	}

	offs = page_header_get_field(page, PAGE_FREE);

	/* Traverse the free list (of deleted records). */
	while (offs) {
		ut_ad(!(offs & ~PAGE_ZIP_DIR_SLOT_MASK));
		rec = page + offs;

		heap_no = rec_get_heap_no_new(rec);
		ut_a(heap_no >= PAGE_HEAP_NO_USER_LOW);
		ut_a(heap_no < n_heap);

		ut_a(!rec[-REC_N_NEW_EXTRA_BYTES]); /* info_bits and n_owned */
		ut_a(rec_get_status(rec) == status);

		mach_write_to_2(buf - PAGE_ZIP_DIR_SLOT_SIZE * ++i, offs);

		if (UNIV_LIKELY_NULL(recs)) {
			/* Ensure that each heap_no occurs at most once. */
			ut_a(!recs[heap_no - PAGE_HEAP_NO_USER_LOW]);
			/* exclude infimum and supremum */
			recs[heap_no - PAGE_HEAP_NO_USER_LOW] = rec;
		}

		offs = rec_get_next_offs(rec, TRUE);
	}

	/* Ensure that each heap no occurs at least once. */
	ut_a(i + PAGE_HEAP_NO_USER_LOW == n_heap);
}

/**********************************************************************//**
Populate the sparse page directory from the dense directory.
@return true on success, false on failure */
bool
page_zip_dir_decode(
/*================*/
	const page_zip_des_t*	page_zip,/*!< in: dense page directory on
					compressed page */
	page_t*			page,	/*!< in: compact page with valid header;
					out: trailer and sparse page directory
					filled in */
	rec_t**			recs,	/*!< out: dense page directory sorted by
					ascending address (and heap_no) */
	rec_t**			recs_aux,/*!< in/out: scratch area */
	ulint			n_dense)/*!< in: number of user records, and
					size of recs[] and recs_aux[] */
{
	ulint	i;
	ulint	n_recs;
	byte*	slot;

	n_recs = page_get_n_recs(page);

	if (UNIV_UNLIKELY(n_recs > n_dense)) {
		page_zip_fail("page_zip_dir_decode 1: %lu > %lu\n",
			       (ulong) n_recs, (ulong) n_dense);
		return false;
	}

	/* Traverse the list of stored records in the sorting order,
	starting from the first user record. */

	slot = page + (UNIV_PAGE_SIZE - PAGE_DIR - PAGE_DIR_SLOT_SIZE);
	UNIV_PREFETCH_RW(slot);

	/* Zero out the page trailer. */
	memset(slot + PAGE_DIR_SLOT_SIZE, 0, PAGE_DIR);

	mach_write_to_2(slot, PAGE_NEW_INFIMUM);
	slot -= PAGE_DIR_SLOT_SIZE;
	UNIV_PREFETCH_RW(slot);

	/* Initialize the sparse directory and copy the dense directory. */
	for (i = 0; i < n_recs; i++) {
		ulint	offs = page_zip_dir_get(page_zip, i);

		if (offs & PAGE_ZIP_DIR_SLOT_OWNED) {
			mach_write_to_2(slot, offs & PAGE_ZIP_DIR_SLOT_MASK);
			slot -= PAGE_DIR_SLOT_SIZE;
			UNIV_PREFETCH_RW(slot);
		}

		if (UNIV_UNLIKELY((offs & PAGE_ZIP_DIR_SLOT_MASK)
				  < PAGE_ZIP_START + REC_N_NEW_EXTRA_BYTES)) {
			page_zip_fail("page_zip_dir_decode 2: %u %u %lx\n",
				       (unsigned) i, (unsigned) n_recs,
				       (ulong) offs);
			return false;
		}

		recs[i] = page + (offs & PAGE_ZIP_DIR_SLOT_MASK);
	}

	mach_write_to_2(slot, PAGE_NEW_SUPREMUM);
	{
		const page_dir_slot_t*	last_slot = page_dir_get_nth_slot(
			page, page_dir_get_n_slots(page) - 1);

		if (UNIV_UNLIKELY(slot != last_slot)) {
			page_zip_fail("page_zip_dir_decode 3: %p != %p\n",
				       (const void*) slot,
				       (const void*) last_slot);
			return false;
		}
	}

	/* Copy the rest of the dense directory. */
	for (; i < n_dense; i++) {
		ulint	offs = page_zip_dir_get(page_zip, i);

		if (UNIV_UNLIKELY(offs & ~PAGE_ZIP_DIR_SLOT_MASK)) {
			page_zip_fail("page_zip_dir_decode 4: %u %u %lx\n",
				       (unsigned) i, (unsigned) n_dense,
				       (ulong) offs);
			return false;
		}

		recs[i] = page + offs;
	}

	if (UNIV_LIKELY(n_dense > 1)) {
		page_zip_dir_sort(recs, recs_aux, 0, n_dense);
	}
	return true;
}

