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
@file page/page0zip_mlog.cc
This file hosts all functionalities related to write/parse mlog related to
compressed pages.

Originally in page/page0zip.cc created June 2005 by Marko Makela.
Refactored by Rongrong Zhong.
*******************************************************/
#include "page0zip_mlog.h"
#include "page0zip_helper.h"

#ifndef UNIV_HOTBACKUP
/**********************************************************************//**
Write a log record of writing to the uncompressed header portion of a page. */
void
page_zip_write_header_log(
/*======================*/
	const byte*	data,	/*!< in: data on the uncompressed page */
	ulint		length,	/*!< in: length of the data */
	mtr_t*		mtr)	/*!< in: mini-transaction */
{
	byte*	log_ptr	= mlog_open(mtr, 11 + 1 + 1);
	ulint	offset	= page_offset(data);

	ut_ad(offset < PAGE_DATA);
	ut_ad(offset + length < PAGE_DATA);
#if PAGE_DATA > 255
# error "PAGE_DATA > 255"
#endif
	ut_ad(length < 256);

	/* If no logging is requested, we may return now */
	if (UNIV_UNLIKELY(!log_ptr)) {

		return;
	}

	log_ptr = mlog_write_initial_log_record_fast(
		(byte*) data, MLOG_ZIP_WRITE_HEADER, log_ptr, mtr);
	*log_ptr++ = (byte) offset;
	*log_ptr++ = (byte) length;
	mlog_close(mtr, log_ptr);

	mlog_catenate_string(mtr, data, length);
}

/**********************************************************************//**
Write a log record of writing the node pointer of a record on a non-leaf
compressed page. */
void
page_zip_write_node_ptr_log(
/*======================*/
	const byte*	field,	/*!< in: filed with child page no */
	ulint		offset,	/*!< in: offsest of node ptr location on
				compressed page trailer */
	mtr_t*		mtr)	/*!< in: mini-transaction */
{
	byte*	log_ptr	= mlog_open(mtr,
				    11 + 2 + 2 + REC_NODE_PTR_SIZE);
	if (UNIV_UNLIKELY(!log_ptr)) {
		return;
	}

	log_ptr = mlog_write_initial_log_record_fast(
		field, MLOG_ZIP_WRITE_NODE_PTR, log_ptr, mtr);
	mach_write_to_2(log_ptr, page_offset(field));
	log_ptr += 2;
	mach_write_to_2(log_ptr, offset);
	log_ptr += 2;
	memcpy(log_ptr, field, REC_NODE_PTR_SIZE);
	log_ptr += REC_NODE_PTR_SIZE;
	mlog_close(mtr, log_ptr);
}

/**********************************************************************//**
Write a log record of writing a BLOB pointer of a record. */
void
page_zip_write_blob_ptr_log(
/*======================*/
	const byte*	field,	/*!< in: filed with extern field */
	const byte*	externs,/*!< in: extern location in trailer */
	ulint		offset,	/*!< in: offsest of extern location on
				compressed page trailer */
	mtr_t*		mtr)	/*!< in: mini-transaction */
{
	byte*	log_ptr	= mlog_open(
		mtr, 11 + 2 + 2 + BTR_EXTERN_FIELD_REF_SIZE);
	if (UNIV_UNLIKELY(!log_ptr)) {
		return;
	}

	log_ptr = mlog_write_initial_log_record_fast(
		field, MLOG_ZIP_WRITE_BLOB_PTR, log_ptr, mtr);
	mach_write_to_2(log_ptr, page_offset(field));
	log_ptr += 2;
	mach_write_to_2(log_ptr, offset);
	log_ptr += 2;
	memcpy(log_ptr, externs, BTR_EXTERN_FIELD_REF_SIZE);
	log_ptr += BTR_EXTERN_FIELD_REF_SIZE;
	mlog_close(mtr, log_ptr);
}

/**********************************************************************//**
Write a log record of compressing an index page. */
void
page_zip_compress_write_log(
/*========================*/
	const page_zip_des_t*	page_zip,/*!< in: compressed page */
	const page_t*		page,	/*!< in: uncompressed page */
	dict_index_t*		index,	/*!< in: index of the B-tree node */
	mtr_t*			mtr)	/*!< in: mini-transaction */
{
	byte*	log_ptr;
	ulint	trailer_size;

	ut_ad(!dict_index_is_ibuf(index));

	log_ptr = mlog_open(mtr, 11 + 2 + 2);

	if (!log_ptr) {

		return;
	}

	/* Get the trailer size */
	trailer_size = page_zip_get_trailer_len(page_zip,
						dict_index_is_clust(index));
	ut_a(page_zip->m_end > PAGE_DATA);
#if FIL_PAGE_DATA > PAGE_DATA
# error "FIL_PAGE_DATA > PAGE_DATA"
#endif
	ut_a(page_zip->m_end + trailer_size <= page_zip_get_size(page_zip));

	log_ptr = mlog_write_initial_log_record_fast((page_t*) page,
						     MLOG_ZIP_PAGE_COMPRESS,
						     log_ptr, mtr);
	mach_write_to_2(log_ptr, page_zip->m_end - FIL_PAGE_TYPE);
	log_ptr += 2;
	mach_write_to_2(log_ptr, trailer_size);
	log_ptr += 2;
	mlog_close(mtr, log_ptr);

	/* Write FIL_PAGE_PREV and FIL_PAGE_NEXT */
	mlog_catenate_string(mtr, page_zip->data + FIL_PAGE_PREV, 4);
	mlog_catenate_string(mtr, page_zip->data + FIL_PAGE_NEXT, 4);
	/* Write most of the page header, the compressed stream and
	the modification log. */
	mlog_catenate_string(mtr, page_zip->data + FIL_PAGE_TYPE,
			     page_zip->m_end - FIL_PAGE_TYPE);
	/* Write the uncompressed trailer of the compressed page. */
	mlog_catenate_string(mtr, page_zip->data + page_zip_get_size(page_zip)
			     - trailer_size, trailer_size);
}
#endif /* !UNIV_HOTBACKUP */

/***********************************************************//**
Parses a log record of writing a BLOB pointer of a record.
@return	end of log record or NULL */
byte*
page_zip_parse_write_blob_ptr(
/*==========================*/
	byte*		ptr,	/*!< in: redo log buffer */
	byte*		end_ptr,/*!< in: redo log buffer end */
	page_t*		page,	/*!< in/out: uncompressed page */
	page_zip_des_t*	page_zip)/*!< in/out: compressed page */
{
	ulint	offset;
	ulint	z_offset;

	ut_ad(!page == !page_zip);

	if (UNIV_UNLIKELY
	    (end_ptr < ptr + (2 + 2 + BTR_EXTERN_FIELD_REF_SIZE))) {

		return(NULL);
	}

	offset = mach_read_from_2(ptr);
	z_offset = mach_read_from_2(ptr + 2);

	if (UNIV_UNLIKELY(offset < PAGE_ZIP_START)
	    || UNIV_UNLIKELY(offset >= UNIV_PAGE_SIZE)
	    || UNIV_UNLIKELY(z_offset >= UNIV_PAGE_SIZE)) {
corrupt:
		recv_sys->found_corrupt_log = TRUE;

		return(NULL);
	}

	if (page) {
		if (UNIV_UNLIKELY(!page_zip)
		    || UNIV_UNLIKELY(!page_is_leaf(page))) {

			goto corrupt;
		}

		if (UNIV_UNLIKELY(page_zip_debug)) {
			ut_a(page_zip_validate(page_zip, page, NULL));
		}

		memcpy(page + offset,
		       ptr + 4, BTR_EXTERN_FIELD_REF_SIZE);
		memcpy(page_zip->data + z_offset,
		       ptr + 4, BTR_EXTERN_FIELD_REF_SIZE);

		if (UNIV_UNLIKELY(page_zip_debug)) {
			ut_a(page_zip_validate(page_zip, page, NULL));
		}
	}

	return(ptr + (2 + 2 + BTR_EXTERN_FIELD_REF_SIZE));
}

/***********************************************************//**
Parses a log record of writing the node pointer of a record.
@return	end of log record or NULL */
byte*
page_zip_parse_write_node_ptr(
/*==========================*/
	byte*		ptr,	/*!< in: redo log buffer */
	byte*		end_ptr,/*!< in: redo log buffer end */
	page_t*		page,	/*!< in/out: uncompressed page */
	page_zip_des_t*	page_zip)/*!< in/out: compressed page */
{
	ulint	offset;
	ulint	z_offset;

	ut_ad(!page == !page_zip);

	if (UNIV_UNLIKELY(end_ptr < ptr + (2 + 2 + REC_NODE_PTR_SIZE))) {

		return(NULL);
	}

	offset = mach_read_from_2(ptr);
	z_offset = mach_read_from_2(ptr + 2);

	if (UNIV_UNLIKELY(offset < PAGE_ZIP_START)
	    || UNIV_UNLIKELY(offset >= UNIV_PAGE_SIZE)
	    || UNIV_UNLIKELY(z_offset >= UNIV_PAGE_SIZE)) {
corrupt:
		recv_sys->found_corrupt_log = TRUE;

		return(NULL);
	}

	if (page) {
		byte*	storage_end;
		byte*	field;
		byte*	storage;
		ulint	heap_no;

		if (UNIV_UNLIKELY(!page_zip)
		    || UNIV_UNLIKELY(page_is_leaf(page))) {

			goto corrupt;
		}

		if (UNIV_UNLIKELY(page_zip_debug)) {
			ut_a(page_zip_validate(page_zip, page, NULL));
		}

		field = page + offset;
		storage = page_zip->data + z_offset;

		storage_end = page_zip_dir_start(page_zip);

		heap_no = 1 + (storage_end - storage) / REC_NODE_PTR_SIZE;

		if (UNIV_UNLIKELY((storage_end - storage) % REC_NODE_PTR_SIZE)
		    || UNIV_UNLIKELY(heap_no < PAGE_HEAP_NO_USER_LOW)
		    || UNIV_UNLIKELY(heap_no >= page_dir_get_n_heap(page))) {

			goto corrupt;
		}

		memcpy(field, ptr + 4, REC_NODE_PTR_SIZE);
		memcpy(storage, ptr + 4, REC_NODE_PTR_SIZE);

		if (UNIV_UNLIKELY(page_zip_debug)) {
			ut_a(page_zip_validate(page_zip, page, NULL));
		}
	}

	return(ptr + (2 + 2 + REC_NODE_PTR_SIZE));
}

/***********************************************************//**
Parses a log record of writing to the header of a page.
@return	end of log record or NULL */
byte*
page_zip_parse_write_header(
/*========================*/
	byte*		ptr,	/*!< in: redo log buffer */
	byte*		end_ptr,/*!< in: redo log buffer end */
	page_t*		page,	/*!< in/out: uncompressed page */
	page_zip_des_t*	page_zip)/*!< in/out: compressed page */
{
	ulint	offset;
	ulint	len;

	ut_ad(ptr && end_ptr);
	ut_ad(!page == !page_zip);

	if (UNIV_UNLIKELY(end_ptr < ptr + (1 + 1))) {

		return(NULL);
	}

	offset = (ulint) *ptr++;
	len = (ulint) *ptr++;

	if (UNIV_UNLIKELY(!len) || UNIV_UNLIKELY(offset + len >= PAGE_DATA)) {
corrupt:
		recv_sys->found_corrupt_log = TRUE;

		return(NULL);
	}

	if (UNIV_UNLIKELY(end_ptr < ptr + len)) {

		return(NULL);
	}

	if (page) {
		if (UNIV_UNLIKELY(!page_zip)) {

			goto corrupt;
		}
		if (UNIV_UNLIKELY(page_zip_debug)) {
			ut_a(page_zip_validate(page_zip, page, NULL));
		}

		memcpy(page + offset, ptr, len);
		memcpy(page_zip->data + offset, ptr, len);

		if (UNIV_UNLIKELY(page_zip_debug)) {
			ut_a(page_zip_validate(page_zip, page, NULL));
		}
	}

	return(ptr + len);
}

/**********************************************************************//**
Parses a log record of compressing an index page.
@return	end of log record or NULL */
byte*
page_zip_parse_compress(
/*====================*/
	byte*		ptr,	/*!< in: buffer */
	byte*		end_ptr,/*!< in: buffer end */
	page_t*		page,	/*!< out: uncompressed page */
	page_zip_des_t*	page_zip,/*!< out: compressed page */
	ulint		space_id)/*!< in: id of the space the page belongs */
{
	ulint	size;
	ulint	trailer_size;

	ut_ad(ptr && end_ptr);
	ut_ad(!page == !page_zip);

	if (UNIV_UNLIKELY(ptr + (2 + 2) > end_ptr)) {

		return(NULL);
	}

	size = mach_read_from_2(ptr);
	ptr += 2;
	trailer_size = mach_read_from_2(ptr);
	ptr += 2;

	if (UNIV_UNLIKELY(ptr + 8 + size + trailer_size > end_ptr)) {

		return(NULL);
	}

	if (page) {
		if (UNIV_UNLIKELY(!page_zip)
		    || UNIV_UNLIKELY(page_zip_get_size(page_zip) < size)) {
corrupt:
			recv_sys->found_corrupt_log = TRUE;

			return(NULL);
		}

		memcpy(page_zip->data + FIL_PAGE_PREV, ptr, 4);
		memcpy(page_zip->data + FIL_PAGE_NEXT, ptr + 4, 4);
		memcpy(page_zip->data + FIL_PAGE_TYPE, ptr + 8, size);
		memset(page_zip->data + FIL_PAGE_TYPE + size, 0,
		       page_zip_get_size(page_zip) - trailer_size
		       - (FIL_PAGE_TYPE + size));
		memcpy(page_zip->data + page_zip_get_size(page_zip)
		       - trailer_size, ptr + 8 + size, trailer_size);

		if (UNIV_UNLIKELY(!page_zip_decompress(
					page_zip, page, TRUE,
					space_id, ULINT_UNDEFINED))) {

			goto corrupt;
		}
	}

	return(ptr + 8 + size + trailer_size);
}

/**********************************************************************//**
Parses a log record of compressing an index page without the data.
@return	end of log record or NULL */
byte*
page_zip_parse_compress_no_data(
/*============================*/
	byte*		ptr,		/*!< in: buffer */
	byte*		end_ptr,	/*!< in: buffer end */
	page_t*		page,		/*!< in: uncompressed page */
	page_zip_des_t*	page_zip,	/*!< out: compressed page */
	dict_index_t*	index)		/*!< in: index */
{
	uchar	compression_flags;
	if (end_ptr == ptr) {
		return(NULL);
	}

	compression_flags = mach_read_from_1(ptr);

	/* If page compression fails then there must be something wrong
	because a compress log record is logged only if the compression
	was successful. Crash in this case. */

	if (page
	    && !page_zip_compress(page_zip, page, index, compression_flags,
				  NULL)) {
		ut_error;
	}

	return(ptr + 1);
}
