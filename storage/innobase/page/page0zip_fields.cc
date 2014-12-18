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
@file page/page0zip_fields.cc
This file hosts all functionalities related to read/write index fields to
the compressed page. The encoded index fields is at the beginning of the
compressed block right after page header. During decompressing it is used
to reconstruct the records.

*******************************************************/
#include "page0zip_fields.h"
#include "page0zip_helper.h"
#include "dict0dict.h"

/**********************************************************************//**
Encode the length of a fixed-length column.
@return	buf + length of encoded val */
static
byte*
page_zip_fixed_field_encode(
/*========================*/
	byte*	buf,	/*!< in: pointer to buffer where to write */
	ulint	val)	/*!< in: value to write */
{
	ut_ad(val >= 2);

	if (UNIV_LIKELY(val < 126)) {
		/*
		0 = nullable variable field of at most 255 bytes length;
		1 = not null variable field of at most 255 bytes length;
		126 = nullable variable field with maximum length >255;
		127 = not null variable field with maximum length >255
		*/
		*buf++ = (byte) val;
	} else {
		*buf++ = (byte) (0x80 | val >> 8);
		*buf++ = (byte) val;
	}

	return(buf);
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
	byte*		buf)	/*!< out: buffer of (n + 1) * 2 bytes */
{
	const byte*	buf_start	= buf;
	ulint		i;
	ulint		col;
	ulint		trx_id_col	= 0;
	/* sum of lengths of preceding non-nullable fixed fields, or 0 */
	ulint		fixed_sum	= 0;

	ut_ad(trx_id_pos == ULINT_UNDEFINED || trx_id_pos < n);

	for (i = col = 0; i < n; i++) {
		dict_field_t*	field = dict_index_get_nth_field(index, i);
		ulint		val;

		if (dict_field_get_col(field)->prtype & DATA_NOT_NULL) {
			val = 1; /* set the "not nullable" flag */
		} else {
			val = 0; /* nullable field */
		}

		if (!field->fixed_len) {
			/* variable-length field */
			const dict_col_t*	column
				= dict_field_get_col(field);

			if (UNIV_UNLIKELY(column->len > 255)
			    || UNIV_UNLIKELY(column->mtype == DATA_BLOB)) {
				val |= 0x7e; /* max > 255 bytes */
			}

			if (fixed_sum) {
				/* write out the length of any
				preceding non-nullable fields */
				buf = page_zip_fixed_field_encode(
					buf, fixed_sum << 1 | 1);
				fixed_sum = 0;
				col++;
			}

			*buf++ = (byte) val;
			col++;
		} else if (val) {
			/* fixed-length non-nullable field */

			if (fixed_sum && UNIV_UNLIKELY
			    (fixed_sum + field->fixed_len
			     > DICT_MAX_FIXED_COL_LEN)) {
				/* Write out the length of the
				preceding non-nullable fields,
				to avoid exceeding the maximum
				length of a fixed-length column. */
				buf = page_zip_fixed_field_encode(
					buf, fixed_sum << 1 | 1);
				fixed_sum = 0;
				col++;
			}

			if (i && UNIV_UNLIKELY(i == trx_id_pos)) {
				if (fixed_sum) {
					/* Write out the length of any
					preceding non-nullable fields,
					and start a new trx_id column. */
					buf = page_zip_fixed_field_encode(
						buf, fixed_sum << 1 | 1);
					col++;
				}

				trx_id_col = col;
				fixed_sum = field->fixed_len;
			} else {
				/* add to the sum */
				fixed_sum += field->fixed_len;
			}
		} else {
			/* fixed-length nullable field */

			if (fixed_sum) {
				/* write out the length of any
				preceding non-nullable fields */
				buf = page_zip_fixed_field_encode(
					buf, fixed_sum << 1 | 1);
				fixed_sum = 0;
				col++;
			}

			buf = page_zip_fixed_field_encode(
				buf, field->fixed_len << 1);
			col++;
		}
	}

	if (fixed_sum) {
		/* Write out the lengths of last fixed-length columns. */
		buf = page_zip_fixed_field_encode(buf, fixed_sum << 1 | 1);
	}

	if (trx_id_pos != ULINT_UNDEFINED) {
		/* Write out the position of the trx_id column */
		i = trx_id_col;
	} else {
		/* Write out the number of nullable fields */
		i = index->n_nullable;
	}

	if (i < 128) {
		*buf++ = (byte) i;
	} else {
		*buf++ = (byte) (0x80 | i >> 8);
		*buf++ = (byte) i;
	}

	ut_ad((ulint) (buf - buf_start) <= (n + 2) * 2);
	return((ulint) (buf - buf_start));
}

/**********************************************************************//**
Read the index information for the compressed page.
@return	own: dummy index describing the page, or NULL on error */
dict_index_t*
page_zip_fields_decode(
/*===================*/
	const byte*	buf,	/*!< in: index information */
	const byte*	end,	/*!< in: end of buf */
	ulint*		trx_id_col)/*!< in: NULL for non-leaf pages;
				for leaf pages, pointer to where to store
				the position of the trx_id column */
{
	const byte*	b;
	ulint		n;
	ulint		i;
	ulint		val;
	dict_table_t*	table;
	dict_index_t*	index;

	/* Determine the number of fields. */
	for (b = buf, n = 0; b < end; n++) {
		if (*b++ & 0x80) {
			b++; /* skip the second byte */
		}
	}

	n--; /* n_nullable or trx_id */

	if (UNIV_UNLIKELY(n > REC_MAX_N_FIELDS)) {

		page_zip_fail("page_zip_fields_decode: n = %lu\n",
			       (ulong) n);
		return(NULL);
	}

	if (UNIV_UNLIKELY(b > end)) {

		page_zip_fail("page_zip_fields_decode: %p > %p\n",
			       (const void*) b, (const void*) end);
		return(NULL);
	}

	table = dict_mem_table_create("ZIP_DUMMY", DICT_HDR_SPACE, n,
				      DICT_TF_COMPACT, 0);
	index = dict_mem_index_create("ZIP_DUMMY", "ZIP_DUMMY",
				      DICT_HDR_SPACE, 0, n);
	index->table = table;
	index->n_uniq = n;
	/* avoid ut_ad(index->cached) in dict_index_get_n_unique_in_tree */
	index->cached = TRUE;

	/* Initialize the fields. */
	for (b = buf, i = 0; i < n; i++) {
		ulint	mtype;
		ulint	len;

		val = *b++;

		if (UNIV_UNLIKELY(val & 0x80)) {
			/* fixed length > 62 bytes */
			val = (val & 0x7f) << 8 | *b++;
			len = val >> 1;
			mtype = DATA_FIXBINARY;
		} else if (UNIV_UNLIKELY(val >= 126)) {
			/* variable length with max > 255 bytes */
			len = 0x7fff;
			mtype = DATA_BINARY;
		} else if (val <= 1) {
			/* variable length with max <= 255 bytes */
			len = 0;
			mtype = DATA_BINARY;
		} else {
			/* fixed length < 62 bytes */
			len = val >> 1;
			mtype = DATA_FIXBINARY;
		}

		dict_mem_table_add_col(table, NULL, NULL, mtype,
				       val & 1 ? DATA_NOT_NULL : 0, len);
		dict_index_add_col(index, table,
				   dict_table_get_nth_col(table, i), 0);
	}

	val = *b++;
	if (UNIV_UNLIKELY(val & 0x80)) {
		val = (val & 0x7f) << 8 | *b++;
	}

	/* Decode the position of the trx_id column. */
	if (trx_id_col) {
		if (!val) {
			val = ULINT_UNDEFINED;
		} else if (UNIV_UNLIKELY(val >= n)) {
			page_zip_fields_free(index);
			index = NULL;
		} else {
			index->type = DICT_CLUSTERED;
		}

		*trx_id_col = val;
	} else {
		/* Decode the number of nullable fields. */
		if (UNIV_UNLIKELY(index->n_nullable > val)) {
			page_zip_fields_free(index);
			index = NULL;
		} else {
			index->n_nullable = val;
		}
	}

	ut_ad(b == end);

	return(index);
}
