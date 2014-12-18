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
@file page/page0zip_fields.h
This file hosts all functionalities related to read/write index fields to
the compressed page. The encoded index fields is at the beginning of the
compressed block right after page header. During decompressing it is used
to reconstruct the records.

*******************************************************/
#ifndef PAGE0ZIP_FIELDS_H
#define PAGE0ZIP_FIELDS_H

#include "page0types.h"
#include "dict0mem.h"

/**********************************************************************//**
Deallocate the index information initialized by page_zip_fields_decode(). */
inline
void
page_zip_fields_free(
/*=================*/
	dict_index_t*	index)	/*!< in: dummy index to be freed */
{
	if (index) {
		dict_table_t*	table = index->table;
		dict_index_zip_pad_mutex_destroy(index);
		mem_heap_free(index->heap);

		dict_mem_table_free(table);
	}
}

/**********************************************************************//**
Write the index information for the compressed page.
@return	used size of buf */
ulint
page_zip_fields_encode(
/*===================*/
	ulint		n,	/*!< in: number of fields to compress */
	dict_index_t*	index,	/*!< in: index comprising at least n fields */
	ulint		trx_id_pos,/*!< in: position of the trx_id column
				in the index, or ULINT_UNDEFINED if
				this is a non-leaf page */
	byte*		buf);	/*!< out: buffer of (n + 1) * 2 bytes */

/**********************************************************************//**
Read the index information for the compressed page.
@return	own: dummy index describing the page, or NULL on error */
dict_index_t*
page_zip_fields_decode(
/*===================*/
	const byte*	buf,	/*!< in: index information */
	const byte*	end,	/*!< in: end of buf */
	ulint*		trx_id_col);/*!< in: NULL for non-leaf pages;
				for leaf pages, pointer to where to store
				the position of the trx_id column */

#endif
