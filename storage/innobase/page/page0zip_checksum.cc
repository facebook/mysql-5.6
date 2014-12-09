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
@file page/page0zip_checksum.cc originally from page0zip.cc
checksum functions for compressed page.
*******************************************************/

#include "page0zip_checksum.h"
#include "fil0fil.h"
#include "ut0crc32.h"
#include "buf0checksum.h"
#include "zlib.h"

/**********************************************************************//**
Calculate the compressed page checksum.
@return	page checksum */
ulint
page_zip_calc_checksum(
/*===================*/
	const void*			data,	/*!< in: compressed page */
	ulint				size,	/*!< in: compressed page size */
	srv_checksum_algorithm_t	algo)	/*!< in: algorithm to use */
{
	uLong		adler;
	ib_uint32_t	crc32;
	const Bytef*	s = static_cast<const byte*>(data);

	/* Exclude FIL_PAGE_SPACE_OR_CHKSUM, FIL_PAGE_LSN,
	and FIL_PAGE_FILE_FLUSH_LSN from the checksum. */

	switch (algo) {
	case SRV_CHECKSUM_ALGORITHM_CRC32:
	case SRV_CHECKSUM_ALGORITHM_STRICT_CRC32:
	case SRV_CHECKSUM_ALGORITHM_FACEBOOK:

		ut_ad(size > FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID);

		crc32 = ut_crc32(s + FIL_PAGE_OFFSET,
				 FIL_PAGE_LSN - FIL_PAGE_OFFSET)
			^ ut_crc32(s + FIL_PAGE_TYPE, 2)
			^ ut_crc32(s + FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID,
				   size - FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID);

		return((ulint) crc32);
	case SRV_CHECKSUM_ALGORITHM_INNODB:
	case SRV_CHECKSUM_ALGORITHM_STRICT_INNODB:
		ut_ad(size > FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID);

		adler = adler32(0L, s + FIL_PAGE_OFFSET,
				FIL_PAGE_LSN - FIL_PAGE_OFFSET);
		adler = adler32(adler, s + FIL_PAGE_TYPE, 2);
		adler = adler32(
			adler, s + FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID,
			static_cast<uInt>(size)
			- FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID);

		return((ulint) adler);
	case SRV_CHECKSUM_ALGORITHM_NONE:
	case SRV_CHECKSUM_ALGORITHM_STRICT_NONE:
		return(BUF_NO_CHECKSUM_MAGIC);
	/* no default so the compiler will emit a warning if new enum
	is added and not handled here */
	}

	ut_error;
	return(0);
}

/**********************************************************************//**
Verify a compressed page's checksum.
@return	TRUE if the stored checksum is valid according to the value of
innodb_checksum_algorithm */
ibool
page_zip_verify_checksum(
/*=====================*/
	const void*	data,	/*!< in: compressed page */
	ulint		size)	/*!< in: size of compressed page */
{
	ib_uint32_t	stored;
	ib_uint32_t	calc;
	ib_uint32_t	crc32 = 0 /* silence bogus warning */;
	ib_uint32_t	innodb = 0 /* silence bogus warning */;

	stored = static_cast<ib_uint32_t>(mach_read_from_4(
		static_cast<const unsigned char*>(data) + FIL_PAGE_SPACE_OR_CHKSUM));

#ifndef UNIV_INNOCHECKSUM
	/* innochecksum doesn't compile with ut_d. Since we don't
	need to check for empty pages when running innochecksum,
	just don't include this code. */
	/* declare empty pages non-corrupted */
	if (stored == 0) {
		/* the checksum field is zero, so page might be empty */
		ulint i;
		for (i = 0; i < size; i++) {
			if (*((const char*) data + i) != 0) {
				/* Non-zero byte detected in page data.
				 * Break from the loop now (i < size) */
				break;
			}
		}

		if (i == size) {
			/* We have checked all bytes in the page and they are
			 * all zero. While the saved checksum (0) is technically
			 * not correct, the page shall still be considered
			 * not corrupted. */
			return(TRUE);
		}

		/* The page is not empty. Continue with regular checks, as
		 * 0 might just be the correct and valid checksum for this
		 * page. */
	}
#endif

	calc = static_cast<ib_uint32_t>(page_zip_calc_checksum(
		data, size, static_cast<srv_checksum_algorithm_t>(
			srv_checksum_algorithm)));

	if (stored == calc) {
		return(TRUE);
	}

	switch ((srv_checksum_algorithm_t) srv_checksum_algorithm) {
	case SRV_CHECKSUM_ALGORITHM_STRICT_CRC32:
	case SRV_CHECKSUM_ALGORITHM_STRICT_INNODB:
	case SRV_CHECKSUM_ALGORITHM_STRICT_NONE:
		return(stored == calc);
	case SRV_CHECKSUM_ALGORITHM_CRC32:
	case SRV_CHECKSUM_ALGORITHM_FACEBOOK:
		/* Facebook and crc32 can also accept an innodb-style
		 * checksum (aka adler32) */
		if (stored == BUF_NO_CHECKSUM_MAGIC) {
			return(TRUE);
		}
		crc32 = calc;
		innodb = static_cast<ib_uint32_t>(page_zip_calc_checksum(
			data, size, SRV_CHECKSUM_ALGORITHM_INNODB));
		break;
	case SRV_CHECKSUM_ALGORITHM_INNODB:
		if (stored == BUF_NO_CHECKSUM_MAGIC) {
			return(TRUE);
		}
		crc32 = static_cast<ib_uint32_t>(page_zip_calc_checksum(
			data, size, SRV_CHECKSUM_ALGORITHM_CRC32));
		innodb = calc;
		break;
	case SRV_CHECKSUM_ALGORITHM_NONE:
		return(TRUE);
	/* no default so the compiler will emit a warning if new enum
	is added and not handled here */
	}

	return(stored == crc32 || stored == innodb);
}
