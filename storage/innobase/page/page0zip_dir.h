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
@file page/page0zip_dir.h
This file hosts all functionalities related to dense page directory.
*******************************************************/

#ifndef PAGE0ZIP_DIR_H
#define PAGE0ZIP_DIR_H

#include "page0types.h"
#include "rem0types.h"

/**********************************************************************//**
Populate the dense page directory from the sparse directory. */
void
page_zip_dir_encode(
/*================*/
	page_zip_des_t*	page_zip,	/*!< dense directory is encoded in the
					bottom of page_zip->data */
	const page_t*	page,	/*!< in: compact page */
	const rec_t**	recs);	/*!< in: pointer to an array of 0, or NULL;
				out: dense page directory sorted by ascending
				address (and heap_no) */

/**********************************************************************//**
Populate the sparse page directory from the dense directory.
@return	TRUE on success, FALSE on failure */
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
	ulint			n_dense);/*!< in: number of user records, and
					size of recs[] and recs_aux[] */

#endif
