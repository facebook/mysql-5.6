/*****************************************************************************

Copyright (c) 2005, 2013, Oracle and/or its affiliates. All Rights Reserved.
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
@file include/page0zip_checksum.h originally from page0zip.h
checksum functions for compressed page.
*******************************************************/

#ifndef PAGE0ZIP_CHECKSUM_H
#define PAGE0ZIP_CHECKSUM_H

#include "ut0rbt.h"
#include "buf0types.h"

/**********************************************************************//**
Calculate the compressed page checksum.
@return	page checksum */
ulint
page_zip_calc_checksum(
/*===================*/
	const void*			data,	/*!< in: compressed page */
	ulint				size,	/*!< in: compressed page size */
	srv_checksum_algorithm_t	algo)	/*!< in: algorithm to use */
	__attribute__((nonnull));

/**********************************************************************//**
Verify a compressed page's checksum.
@return	TRUE if the stored checksum is valid according to the value of
innodb_checksum_algorithm */
ibool
page_zip_verify_checksum(
/*=====================*/
	const void*	data,	/*!< in: compressed page */
	ulint		size);	/*!< in: size of compressed page */

#endif
