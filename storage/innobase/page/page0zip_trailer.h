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
@file page/page0zip_trailer.h
This file hosts all functionalities related to read/write the page trailer,
including the dense page directory, blob pointers, trx_id and rollptrs,
everything that grows backward from the end of the page.

Originally in page/page0zip.cc created June 2005 by Marko Makela.
Refactored by Rongrong Zhong.
*******************************************************/
#ifndef PAGE0ZIP_TRAILER_H
#define PAGE0ZIP_TRAILER_H

#include "page0types.h"
#include "page0zip_helper.h"
#include "dict0mem.h"

/******************************************************//**
Determine how many externally stored columns are contained
on a page */
ulint
page_zip_get_n_blobs(
/*=======================*/
	const page_zip_des_t*	page_zip,	/*!< in: dense page directory on
						compressed page */
	const page_t*		page,		/*!< in: uncompressed page */
	const dict_index_t*	index);		/*!< in: record descriptor */

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
	const dict_index_t*	index);		/*!< in: record descriptor */

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
	ulint		trx_id_col);	/*!< in: column number for the trx_id */

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
	ulint		n_prev_blobs);	/*!< in: number of blob pointers that
					were already stored before */

#endif /* PAGE0ZIP_TRAILER_H */
