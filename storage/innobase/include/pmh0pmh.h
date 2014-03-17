/* Copyright (C) 2013 Facebook, Inc.  All Rights Reserved.

   Dual licensed under BSD license and GPLv2.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY FACEBOOK, INC. ``AS IS'' AND ANY EXPRESS OR
   IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
   EVENT SHALL FACEBOOK, INC. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
   OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
   OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
   ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the Free
   Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   You should have received a copy of the GNU General Public License along with
   this program; if not, write to the Free Software Foundation, Inc., 59 Temple
   Place, Suite 330, Boston, MA  02111-1307  USA */

/**************************************************//**
A hash table in the trailer of the compressed pages that maps heap numbers to
transaction id and rollback pointers.

Created December 2013 by Nizameddin Ordulu
*******************************************************/
#ifndef pmh0pmh_h
#define pmh0pmh_h

#ifndef UNIV_INNOCHECKSUM

#include "univ.i"
#include "mem0mem.h"
#include "dict0types.h"


/* number of bits for the number of buckets in the hash table */
#define PMH_NUM_BUCKETS_SHIFT 6
#if PMH_NUM_BUCKETS_SHIFT < 6
#error "PMH_NUM_BUCKETS_SHIFT must be greater than or equal to 6, see \
	the comment in pmh0pmh.ic for why."
#endif
/* number of buckets in the hash table */
#define PMH_NUM_BUCKETS (1 << PMH_NUM_BUCKETS_SHIFT)
/* this mask is applied to a record number to find the corresponding bucket */
#define PMH_BUCKET_MASK (PMH_NUM_BUCKETS - 1)
/* number of bits needed to address the cell number of a cell */
#define PMH_NEXT_CELL_LEN 11
/* this mask is applied to the first two bytes of the cell to find the next
cell number */
#define PMH_NEXT_CELL_MASK (~((~(ulint)0) << PMH_NEXT_CELL_LEN))
/* This is the amount of shifting we need to do before storing the key_no
in the hash table. This is because the lower bits of key_no and the higher
bits of next_cell_no belong to the same byte. */
#define PMH_KEY_SHIFT PMH_NEXT_CELL_LEN
/* The number of bits for key_no. key_no is used to determine rec_no assuming
the bucket number is known. bucket_no = rec_no % num_buckets and
key_no = rec_no / num_buckets */
#define PMH_KEY_LEN 5
/* this mask is applied to the first two bytes of a cell to find the
corresponding key_no. */
#define PMH_KEY_MASK ((~((~(ulint)0) << PMH_KEY_LEN)) << PMH_KEY_SHIFT)
/* The length of the part of the storage where the headers for all buckets
are stored. 12 bits are used to store for each bucket header. The hardcoding
of 12 is intentional because the implementation of pmh_get_bucket_header()
and pmh_set_bucket_header() relies on this number. PMH_NEXT_CELL_LEN
of these bits are used to store the cell number for the first cell of this
bucket and 1 bit is used to indicate whether the the bucket is empty. */
#define PMH_BUCKET_HEADERS_LEN UT_BITS_IN_BYTES(12 * PMH_NUM_BUCKETS)
/* The number of bytes in the values that can be stored in this hash table.
This is equal to the number of bytes needed for trx id and the number of bytes
needed for rbp. */
#define PMH_VALUE_LEN DATA_TRX_RBP_LEN
/* The length of a cell in the hash table. Each cell stores the following:
The cell number for the next cell, the key_no for the current cell and the
value for the key_no. */
#define PMH_CELL_LEN \
	(UT_BITS_IN_BYTES(PMH_NEXT_CELL_LEN + PMH_KEY_LEN) + PMH_VALUE_LEN)
/* The header of the minihash consists of the number of cells which is stored
using two bytes */
#define PMH_HEADER_LEN 2
/* The length of the hash table with only one key-value pair */
#define PMH_MIN_LEN (PMH_HEADER_LEN + PMH_BUCKET_HEADERS_LEN + PMH_CELL_LEN)

/**************************************************//**
Initialize the hash table for transaction ids and rollback
pointers. */
UNIV_INLINE
void
pmh_init(
	byte*	storage);	/*!< in: Pointer to the start of the data for
				the hash table in a compressed page's trailer.
				The data is stored starting from higher memory
				addresses going to lower memory addresses. */

/**************************************************//**
Return the current size in bytes of the hash table.
@return the size of the hash table */
UNIV_INLINE
ulint
pmh_size(
	const byte*	storage);	/*!< in: Pointer to the start of the
					data for the hash table in a compressed
					page's trailer. The data is stored
					starting from higher memory addresses
					going to lower memory addresses. */

/**************************************************//**
Store the key-value pair (rec_no, value) in the hash table. The bytes of the
value is copied to the appropriate address inside storage. */
UNIV_INLINE
void
pmh_put(
	byte*	storage,	/*!< in: Pointer to the start of the data for
				the hash table in a compressed page's trailer.
				The data is stored starting from higher memory
				addresses going to lower memory addresses. */
	ulint	rec_no,		/*!< in: the rec_no for which the trx id and rbp
				are stored.
				rec_no = heap_no - PAGE_HEAP_NO_USER_LOW. */
	const byte*	value);	/*!< in: value is the concatenation of trx id
				and rbp */

/***********************************************************************//**
This function provides a way to iterate over the key-value pairs in the hash
table and restore the transaction id and rollback pointers efficiently. The
recs parameter must have the records ordered in heap_no order such that
recs[rec_no] == rec,
where rec's record number is rec_no. */
UNIV_INLINE
void
pmh_restore_trx_rbp(
	const byte*	storage,/*!< in: Pointer to the start of the
				data for the hash table in a compressed
				page's trailer. The data is stored
				starting from higher memory addresses
				going to lower memory addresses. */
	rec_t**		recs,	/*!< in: records sorted by record number */
	ulint**		offsets,/*!< out: pointer to memory allocated for record
				offsets */
	const dict_index_t*	index,	/*!< in: index needed to compute
					offsets */
	ulint		trx_id_col,	/*!< in: the column number for
					transaction id */
	mem_heap_t**	heap);	/*!< in: temporary memeory heap */

#ifdef UNIV_MATERIALIZE
# undef UNIV_INLINE
# define UNIV_INLINE	UNIV_INLINE_ORIGINAL
#endif

#ifndef UNIV_NONINL
# include "pmh0pmh.ic"
#endif

#endif /* !UNIV_INNOCHECKSUM */
#endif /* pmh0pmh_h */
