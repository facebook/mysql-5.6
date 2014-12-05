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
@file page/page0zip_trailer.cc
This file hosts all functionalities related to read/write the page trailer,
including the dense page directory, blob pointers, trx_id and rollptrs,
everything that grows backward from the end of the page.

Originally in page/page0zip.cc created June 2005 by Marko Makela.
Refactored by Rongrong Zhong.
*******************************************************/
#include "page0zip_trailer.h"

/******************************************************//**
Determine how many externally stored columns are contained
on a page */
ulint
page_zip_get_n_blobs(
/*=======================*/
	const page_zip_des_t*	page_zip,	/*!< in: dense page directory on
						compressed page */
	const page_t*		page,		/*!< in: uncompressed page */
	const dict_index_t*	index)		/*!< in: record descriptor */
{
	ulint n_ext = 0;
	ulint i;
	ulint n_recs = page_get_n_recs(page_zip->data);
	const rec_t* rec;
	ut_ad(page_is_leaf(page));
	ut_ad(page_is_comp(page));
	ut_ad(dict_table_is_comp(index->table));
	ut_ad(dict_index_is_clust(index));
	ut_ad(!dict_index_is_ibuf(index));
	for (i = 0; i < n_recs; ++i) {
		rec = page + (page_zip_dir_get(page_zip, i)
			      & PAGE_ZIP_DIR_SLOT_MASK);
		n_ext += rec_get_n_extern_new(rec, index, ULINT_UNDEFINED);
	}
	return(n_ext);
}

/******************************************************//**
Determine how many externally stored columns are contained
in existing records with smaller heap_no than rec. */
ulint
page_zip_get_n_prev_extern(
/*=======================*/
	const page_zip_des_t*	page_zip,	/*!< in: dense page directory on
						compressed page */
	const rec_t*		rec,		/*!< in: compact physical record
						on a B-tree leaf page */
	const dict_index_t*	index)		/*!< in: record descriptor */
{
	const page_t*	page	= page_align(rec);
	ulint		n_ext	= 0;
	ulint		i;
	ulint		left;
	ulint		heap_no;
	ulint		n_recs	= page_get_n_recs(page_zip->data);

	ut_ad(page_is_leaf(page));
	ut_ad(page_is_comp(page));
	ut_ad(dict_table_is_comp(index->table));
	ut_ad(dict_index_is_clust(index));
	ut_ad(!dict_index_is_ibuf(index));

	heap_no = rec_get_heap_no_new(rec);
	ut_ad(heap_no >= PAGE_HEAP_NO_USER_LOW);
	left = heap_no - PAGE_HEAP_NO_USER_LOW;
	if (UNIV_UNLIKELY(!left)) {
		return(0);
	}

	for (i = 0; i < n_recs; i++) {
		const rec_t*	r	= page + (page_zip_dir_get(page_zip, i)
						  & PAGE_ZIP_DIR_SLOT_MASK);

		if (rec_get_heap_no_new(r) < heap_no) {
			n_ext += rec_get_n_extern_new(r, index,
						      ULINT_UNDEFINED);
			if (!--left) {
				break;
			}
		}
	}

	return(n_ext);
}

/**********************************************************************//**
Store the transaction id and rollback pointer of a record on the trailer. */
void
page_zip_store_trx_rbp(
	byte*		trx_rbp_storage,/*!< in: pointer to the storage where
					transaction ids and rollback pointers
					are stored */
	const rec_t*	rec,		/*!< in: The record for which
					uncompressed fields must be written */
	const ulint*	offsets,	/*!< in: offsets for the record
					obtained from rec_get_offsets */
	ulint		rec_no,		/*!< in: record no */
	ulint		trx_id_col)	/*!< in: column number for the trx_id */
{
	byte* src;
	ulint len;
	ut_ad(trx_id_col && (trx_id_col != ULINT_UNDEFINED));
	/* Copy trx id and roll ptr */
	src = rec_get_nth_field((rec_t*)rec, offsets, trx_id_col, &len);
	ut_ad(src + DATA_TRX_ID_LEN
	      == rec_get_nth_field(rec, offsets, trx_id_col + 1, &len));
	ut_ad(len == DATA_ROLL_PTR_LEN);
	memcpy(trx_rbp_storage - DATA_TRX_RBP_LEN * (rec_no + 1),
	       src,
	       DATA_TRX_RBP_LEN);
}

/**********************************************************************//**
Store the blob pointers of a record on the trailer of a compressed page.
For pages that use the compact metadata format, the blob pointers precede
the storage for transaction id and rollback pointers and 2 bytes are used to
store the number of blobs. For these pages, externs must point to the
beginning of the blob pointers. */
void
page_zip_store_blobs(
	byte*		externs,	/*!< in: pointer to the storage where
					the blob pointers are stored */
	const rec_t*	rec,		/*!< in: The record for which
					uncompressed fields must be written */
	const ulint*	offsets,	/*!< in: offsets for the record obtained
					from rec_get_offsets() */
	ulint		n_prev_blobs)	/*!< in: number of blob pointers that
					were already stored before */
{
	uint i;
	const byte* src;
	ulint len;

	externs -= n_prev_blobs * BTR_EXTERN_FIELD_REF_SIZE;
	/* Copy blob pointers of the record */
	for (i = 0; i < rec_offs_n_fields(offsets); ++i) {
		if (rec_offs_nth_extern(offsets, i)) {
			src = rec_get_nth_field(rec, offsets, i, &len);
			ut_ad(len >= BTR_EXTERN_FIELD_REF_SIZE);
			src += len - BTR_EXTERN_FIELD_REF_SIZE;
			externs -= BTR_EXTERN_FIELD_REF_SIZE;
			memcpy(externs, src, BTR_EXTERN_FIELD_REF_SIZE);
		}
	}
}
