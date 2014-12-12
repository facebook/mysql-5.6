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
@file page/page0zip_helper.h originally from page/page0zip.cc
Compressed page helper functions

Created June 2005 by Marko Makela, refactored by Rongrong Dec. 2014.
*******************************************************/
#ifndef PAGE0ZIP_HELPER_H
#define PAGE0ZIP_HELPER_H

#include "dict0dict.h"
#include "page0zip.h"

/* 2 bytes are used to store the number of blob pointers when compact metadata
is used*/
#define COMPACT_METADATA_N_BLOB_STORAGE_SIZE 2

/* Enable some extra debugging output. */
#include <cstdarg>
/**********************************************************************//**
Report a failure to decompress or compress.
@return	number of characters printed */
__attribute__((format (printf, 1, 2)))
inline
int
page_zip_fail(
/*===============*/
	const char*	fmt,	/*!< in: printf(3) format string */
	...)			/*!< in: arguments corresponding to fmt */
{
	int	res = 0;
#if defined UNIV_DEBUG
	va_list	ap;

	ut_print_timestamp(stderr);
	fputs("  InnoDB: ", stderr);
	va_start(ap, fmt);
	res = vfprintf(stderr, fmt, ap);
	va_end(ap);
#endif /* UNIV_DEBUG */
	return(res);
}

/** Assert that a block of memory is filled with zero bytes.
Compare at most sizeof(field_ref_zero) bytes.
@param b	in: memory block
@param s	in: size of the memory block, in bytes */
#define ASSERT_ZERO(b, s) \
	ut_ad(!memcmp(b, field_ref_zero, ut_min(s, sizeof field_ref_zero)))

/**********************************************************************//**
This function makes sure that the compressed page image and modification
log part of the page does not overlap with the uncompressed trailer of the
page. The uncompressed trailer grows backwards whereas the compressed page
image and modification log grow forward. */
inline
void
page_zip_size_check(
	const page_zip_des_t*	page_zip,	/* in: the page_zip
						object, we use
						page_zip->m_end and
						the page headers from
						page_zip->data */
	const dict_index_t*	index)		/* in: index object
						for the table */
{
	ulint	is_clust = dict_index_is_clust(index);
	ulint	trailer_len = page_zip_get_trailer_len(page_zip, is_clust);
	if (UNIV_UNLIKELY(page_zip->m_end + trailer_len
			  >= page_zip_get_size(page_zip))) {
		page_zip_fail("page_zip_size_check: %lu + %lu >= "
			       "%lu is_clust = %lu\n", (ulint) page_zip->m_end,
			       trailer_len, page_zip_get_size(page_zip),
			       is_clust);
		ut_error;
	}
}

/*************************************************************//**
Gets the number of elements in the dense page directory,
including deleted records (the free list).
@return	number of elements in the dense page directory */
inline
ulint
page_zip_dir_elems(
/*===============*/
	const page_zip_des_t*	page_zip)	/*!< in: compressed page */
{
	/* Exclude the page infimum and supremum from the record count. */
	return(page_dir_get_n_heap(page_zip->data) - PAGE_HEAP_NO_USER_LOW);
}

/*************************************************************//**
Gets the size of the compressed page trailer (the dense page directory),
including deleted records (the free list).
@return	length of dense page directory, in bytes */
inline
ulint
page_zip_dir_size(
/*==============*/
	const page_zip_des_t*	page_zip)	/*!< in: compressed page */
{
	return(PAGE_ZIP_DIR_SLOT_SIZE * page_zip_dir_elems(page_zip));
}

/*************************************************************//**
Gets a pointer to the compressed page trailer (the dense page directory),
including deleted records (the free list).
@param[in] page_zip	compressed page
@param[in] n_dense	number of entries in the directory
@return	pointer to the dense page directory */
inline
byte*
page_zip_dir_start(
/*====================*/
	const page_zip_des_t*	page_zip)	/*!< in: compressed page */

{
	ulint n_dense = page_zip_dir_elems(page_zip);
	ut_ad(n_dense * PAGE_ZIP_DIR_SLOT_SIZE < page_zip_get_size(page_zip));
	return (page_zip->data + page_zip_get_size(page_zip)
		- n_dense * PAGE_ZIP_DIR_SLOT_SIZE);
}

/*************************************************************//**
Gets the size of the compressed page trailer (the dense page directory),
only including user records (excluding the free list).
@return	length of dense page directory comprising existing records, in bytes */
inline
ulint
page_zip_dir_user_size(
/*===================*/
	const page_zip_des_t*	page_zip)	/*!< in: compressed page */
{
	ulint	size = PAGE_ZIP_DIR_SLOT_SIZE
		* page_get_n_recs(page_zip->data);
	ut_ad(size <= page_zip_dir_size(page_zip));
	return(size);
}

/*************************************************************//**
Find the slot of the given record in the dense page directory.
@return	dense directory slot, or NULL if record not found */
inline
byte*
page_zip_dir_find_low(
/*==================*/
	byte*	slot,			/*!< in: start of records */
	byte*	end,			/*!< in: end of records */
	ulint	offset)			/*!< in: offset of user record */
{
	ut_ad(slot <= end);

	for (; slot < end; slot += PAGE_ZIP_DIR_SLOT_SIZE) {
		if ((mach_read_from_2(slot) & PAGE_ZIP_DIR_SLOT_MASK)
		    == offset) {
			return(slot);
		}
	}

	return(NULL);
}

/*************************************************************//**
Find the slot of the given non-free record in the dense page directory.
@return	dense directory slot, or NULL if record not found */
inline
byte*
page_zip_dir_find(
/*==============*/
	page_zip_des_t*	page_zip,		/*!< in: compressed page */
	ulint		offset)			/*!< in: offset of user record */
{
	byte*	end	= page_zip->data + page_zip_get_size(page_zip);

	ut_ad(page_zip_simple_validate(page_zip));

	return(page_zip_dir_find_low(end - page_zip_dir_user_size(page_zip),
				     end,
				     offset));
}

/*************************************************************//**
Find the slot of the given free record in the dense page directory.
@return	dense directory slot, or NULL if record not found */
inline
byte*
page_zip_dir_find_free(
/*===================*/
	page_zip_des_t*	page_zip,		/*!< in: compressed page */
	ulint		offset)			/*!< in: offset of user record */
{
	byte*	end	= page_zip->data + page_zip_get_size(page_zip);

	ut_ad(page_zip_simple_validate(page_zip));

	return(page_zip_dir_find_low(end - page_zip_dir_size(page_zip),
				     end - page_zip_dir_user_size(page_zip),
				     offset));
}

/*************************************************************//**
Read a given slot in the dense page directory.
@return record offset on the uncompressed page, possibly ORed with
PAGE_ZIP_DIR_SLOT_DEL or PAGE_ZIP_DIR_SLOT_OWNED */
inline
ulint
page_zip_dir_get(
/*=============*/
	const page_zip_des_t*	page_zip,	/*!< in: compressed page */
	ulint			slot)		/*!< in: slot
						(0=first user record) */
{
	ut_ad(page_zip_simple_validate(page_zip));
	ut_ad(slot < page_zip_dir_size(page_zip) / PAGE_ZIP_DIR_SLOT_SIZE);
	return(mach_read_from_2(page_zip->data + page_zip_get_size(page_zip)
				- PAGE_ZIP_DIR_SLOT_SIZE * (slot + 1)));
}

/***************************************************************//**
Return the trx_id_col used in compression.
TODO: Having secondary index leaf return 0 and node_ptr return ULINT_UNDEFINED
      a hack. Remove this logic later.
@return cluster index leaf page: trx_id_col
        secondary index leaf page: 0
        node_ptr page: ULINT_UNDEFINED
*/
inline ulint
page_zip_get_trx_id_col_for_compression(
	const page_t*		page,	/*!< in: uncompressed page */
	const dict_index_t*	index)	/*!< in: index the page belongs to */
{
	ulint trx_id_col = ULINT_UNDEFINED;
	if (UNIV_LIKELY(page_is_leaf(page))) {
		if (dict_index_is_clust(index)) {
			trx_id_col = dict_index_get_sys_col_pos(index,
								DATA_TRX_ID);
			ut_ad(trx_id_col > 0);
			ut_ad(trx_id_col != ULINT_UNDEFINED);
		} else {
			/* Signal the absence of trx_id
			in page_zip_fields_encode() */
			ut_ad(dict_index_get_sys_col_pos(index, DATA_TRX_ID)
			      == ULINT_UNDEFINED);
			trx_id_col = 0;
		}
	}
	return trx_id_col;
}

/**********************************************************************//**
Return the size of trx_rbp storage space. */
inline ulint
page_zip_get_trx_rbp_storage_size(
	page_zip_des_t*	page_zip)	/*!< in: compressed page */
{
	/*TODO(rongrong): This will change when we implement compact metadata.*/
	return page_zip_dir_elems(page_zip) * DATA_TRX_RBP_LEN;
}

/**********************************************************************//**
Returns the bottom of blob ptrs storage space. blob ptrs storage space grows
backwards from here. For compact_metadata, n_blob is at the returned pointer
location. */
inline byte*
page_zip_get_blob_ptr_storage(
	page_zip_des_t*	page_zip)	/*!< in: compressed page */
{
	byte* blob_ptr_storage = page_zip_dir_start(page_zip);
	if (page_zip->compact_metadata) {
		blob_ptr_storage -= COMPACT_METADATA_N_BLOB_STORAGE_SIZE;
	} else {
		blob_ptr_storage -=
			page_zip_get_trx_rbp_storage_size(page_zip);
	}
	return blob_ptr_storage;
}

/**********************************************************************//**
Writes n_blob to the beginning of blob_ptr storage. */
inline void
page_zip_write_n_blob(
	page_zip_des_t*	page_zip,	/*!< in: compressed page */
	ulint		n_blob)		/*!< in: n_blob */
{
	/* We only need to write n_blob when using compact metatdata format. */
	ut_ad(page_zip->compact_metadata);
	byte* blob_ptr_storage = page_zip_get_blob_ptr_storage(page_zip);
	mach_write_to_2(blob_ptr_storage, n_blob);
}

/**********************************************************************//**
Reads n_blob from the beginning of blob_ptr storage. */
inline ulint
page_zip_read_n_blob(
	page_zip_des_t*	page_zip)	/*!< in: compressed page */
{
	/* We only need to write n_blob when using compact metatdata format. */
	ut_ad(page_zip->compact_metadata);
	byte* blob_ptr_storage = page_zip_get_blob_ptr_storage(page_zip);
	return mach_read_from_2(blob_ptr_storage);
}

/**********************************************************************//**
Return the size of blob_ptr storage space. */
inline ulint
page_zip_get_blob_ptr_storage_size(
	page_zip_des_t*	page_zip,	/*!< in: compressed page */
	bool		read_n_blob)	/*!< in: don't use page_zip->n_blob,
					read from page. */
{
	ulint n_blobs = page_zip->n_blobs;
	if (read_n_blob) {
		n_blobs = page_zip_read_n_blob(page_zip);
	}
	ulint size = n_blobs * BTR_EXTERN_FIELD_REF_SIZE;
	if(page_zip->compact_metadata)
		size += COMPACT_METADATA_N_BLOB_STORAGE_SIZE;
	return size;
}

/**********************************************************************//**
Returns the bottom of trx_rbp storage space. trx_rbp storage space grows
backwards from here. */
inline byte*
page_zip_get_trx_rbp_storage(
	page_zip_des_t*	page_zip,	/*!< in: compressed page */
	bool		read_n_blob)	/*!< in: don't use page_zip->n_blob,
					read from page. */
{
	byte* trx_rbp_storage = page_zip_dir_start(page_zip);
	if (page_zip->compact_metadata) {
		trx_rbp_storage -=
			page_zip_get_blob_ptr_storage_size(page_zip,
							   read_n_blob);
	}
	return trx_rbp_storage;
}

/**********************************************************************//**
Returns the bottom of node_ptr storage space. node_ptr storage space grows
backwards from here. */
inline void
page_zip_trailer_write_node_ptr(
	page_zip_des_t*	page_zip,/*!< in: compressed page */
	ulint		rec_no,	/*!< in: heap_no - PAGE_HEAP_NO_USER_LOW */
	const rec_t*	rec,	/*!< in: record */
	const ulint*	offsets)/*!< in: array returned by rec_get_offsets() */
{
	byte* node_ptr_storage = page_zip_dir_start(page_zip);
	memcpy(node_ptr_storage - REC_NODE_PTR_SIZE * (rec_no + 1),
	       rec_get_end((rec_t*)rec, offsets) - REC_NODE_PTR_SIZE,
	       REC_NODE_PTR_SIZE);
}
#endif /* PAGE0ZIP_HELPER_H */
