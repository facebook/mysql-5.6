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
UNIV_INTERN
ulint
page_zip_calc_checksum(
/*===================*/
	const void*	data,	/*!< in: compressed page */
	ulint		size,	/*!< in: size of compressed page */
	srv_checksum_algorithm_t algo) /*!< in: algorithm to use */
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
UNIV_INTERN
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

#if FIL_PAGE_LSN % 8
#error "FIL_PAGE_LSN must be 64 bit aligned"
#endif

#ifndef UNIV_INNOCHECKSUM
	/* innochecksum doesn't compile with ut_d. Since we don't
	need to check for empty pages when running innochecksum,
	just don't include this code. */
	/* Check if page is empty */
	if (stored == 0
	    && *reinterpret_cast<const ib_uint64_t*>(static_cast<const char*>(
		data)
		+ FIL_PAGE_LSN) == 0) {
		/* make sure that the page is really empty */
		ulint i;
		for (i = 0; i < size; i++) {
			if (*((const char*) data + i) != 0) {
				return(FALSE);
			}
		}
		/* Empty page */
		return(TRUE);
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
