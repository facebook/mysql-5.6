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
@file page/page0zip_mlog.h
This file hosts all functionalities related to write/parse mlog related to
compressed pages.

Originally in page/page0zip.h created June 2005 by Marko Makela.
Refactored by Rongrong Zhong.
*******************************************************/

#ifndef PAGE0ZIP_MLOG_H
#define PAGE0ZIP_MLOG_H

#include "page0types.h"
#include "mtr0mtr.h"

#ifndef UNIV_HOTBACKUP
/**********************************************************************//**
Write a log record of writing a BLOB pointer of a record. */
void
page_zip_write_blob_ptr_log(
/*======================*/
	const byte*	field,	/*!< in: filed with extern field */
	const byte*	externs,/*!< in: extern location in trailer */
	ulint		offset,	/*!< in: offsest of extern location on
				compressed page trailer */
	mtr_t*		mtr);	/*!< in: mini-transaction */

/**********************************************************************//**
Write a log record of writing the node pointer of a record on a non-leaf
compressed page. */
void
page_zip_write_node_ptr_log(
/*======================*/
	const byte*	field,	/*!< in: filed with child page no */
	ulint		offset,	/*!< in: offsest of node ptr location on
				compressed page trailer */
	mtr_t*		mtr);	/*!< in: mini-transaction */

/**********************************************************************//**
Write a log record of writing to the uncompressed header portion of a page. */
void
page_zip_write_header_log(
/*======================*/
	const byte*	data,	/*!< in: data on the uncompressed page */
	ulint		length,	/*!< in: length of the data */
	mtr_t*		mtr);	/*!< in: mini-transaction */

/**********************************************************************//**
Write a log record of compressing an index page. */
void
page_zip_compress_write_log(
/*========================*/
	const page_zip_des_t*	page_zip,/*!< in: compressed page */
	const page_t*		page,	/*!< in: uncompressed page */
	dict_index_t*		index,	/*!< in: index of the B-tree node */
	mtr_t*			mtr);	/*!< in: mini-transaction */

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
	page_zip_des_t*	page_zip);/*!< in/out: compressed page */

/***********************************************************//**
Parses a log record of writing the node pointer of a record.
@return	end of log record or NULL */
byte*
page_zip_parse_write_node_ptr(
/*==========================*/
	byte*		ptr,	/*!< in: redo log buffer */
	byte*		end_ptr,/*!< in: redo log buffer end */
	page_t*		page,	/*!< in/out: uncompressed page */
	page_zip_des_t*	page_zip);/*!< in/out: compressed page */

/***********************************************************//**
Parses a log record of writing to the header of a page.
@return	end of log record or NULL */
byte*
page_zip_parse_write_header(
/*========================*/
	byte*		ptr,	/*!< in: redo log buffer */
	byte*		end_ptr,/*!< in: redo log buffer end */
	page_t*		page,	/*!< in/out: uncompressed page */
	page_zip_des_t*	page_zip);/*!< in/out: compressed page */

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
	ulint		space_id)/*!< in: used to obtain compression
				 type and compression flags */
	__attribute__((nonnull(1,2)));

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
	__attribute__((nonnull(1,2)));

#endif /* PAGE0ZIP_MLOG_H */
