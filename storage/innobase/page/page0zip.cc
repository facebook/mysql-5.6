/*****************************************************************************

Copyright (c) 2005, 2015, Oracle and/or its affiliates. All Rights Reserved.
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
@file page/page0zip.cc
Compressed page interface

Created June 2005 by Marko Makela
*******************************************************/

// First include (the generated) my_config.h, to get correct platform defines.
#include "my_config.h"

#include <map>
using namespace std;

#define THIS_MODULE
#include "page0zip.h"
#ifdef UNIV_NONINL
# include "page0zip.ic"
#endif
#undef THIS_MODULE
#include "fil0fil.h"
#include "fsp0fsp.h"
#include "page0page.h"
#include "page0zip_dir.h"
#include "page0zip_fields.h"
#include "page0zip_helper.h"
#include "page0zip_stats.h"
#include "page0zip_trailer.h"
#include "mtr0log.h"
#include "dict0dict.h"
#include "btr0cur.h"
#include "log0recv.h"
#include "page0types.h"
#ifndef UNIV_HOTBACKUP
# include "buf0buf.h"
# include "btr0sea.h"
# include "dict0boot.h"
# include "lock0lock.h"
# include "srv0srv.h"
# include "zlib_embedded/zlib.h"
# include "comp0comp.h"
# include "buf0lru.h"
# include "srv0mon.h"
#else /* !UNIV_HOTBACKUP */
# define lock_move_reorganize_page(block, temp_block)	((void) 0)
# define buf_LRU_stat_inc_unzip()			((void) 0)
#endif /* !UNIV_HOTBACKUP */
#include "blind_fwrite.h"

#include "mach0data.h"

/* Compression level to be used by zlib. Settable by user. */
UNIV_INTERN uint	page_zip_level = DEFAULT_COMPRESSION_LEVEL;

/* Whether or not to log compressed page images to avoid possible
compression algorithm changes in zlib. */
UNIV_INTERN my_bool	page_zip_log_pages = false;

UNIV_INTERN my_bool page_zip_debug = FALSE;

/* Please refer to ../include/page0zip.ic for a description of the
compressed page format. */

/* The infimum and supremum records are omitted from the compressed page.
On compress, we compare that the records are there, and on uncompress we
restore the records. */
/** Extra bytes of an infimum record */
static const byte infimum_extra[] = {
	0x01,			/* info_bits=0, n_owned=1 */
	0x00, 0x02		/* heap_no=0, status=2 */
	/* ?, ?	*/		/* next=(first user rec, or supremum) */
};
/** Data bytes of an infimum record */
static const byte infimum_data[] = {
	0x69, 0x6e, 0x66, 0x69,
	0x6d, 0x75, 0x6d, 0x00	/* "infimum\0" */
};
/** Extra bytes and data bytes of a supremum record */
static const byte supremum_extra_data[] = {
	/* 0x0?, */		/* info_bits=0, n_owned=1..8 */
	0x00, 0x0b,		/* heap_no=1, status=3 */
	0x00, 0x00,		/* next=0 */
	0x73, 0x75, 0x70, 0x72,
	0x65, 0x6d, 0x75, 0x6d	/* "supremum" */
};

#ifndef UNIV_HOTBACKUP
/**********************************************************************//**
Determine the guaranteed free space on an empty page.
@return	minimum payload size on the page */
UNIV_INTERN
ulint
page_zip_empty_size(
/*================*/
	ulint	n_fields,	/*!< in: number of columns in the index */
	ulint	zip_size,	/*!< in: compressed page size in bytes */
	ibool	compact_metadata)	/*!< in: if TRUE calculate the minimum
					  size when compact metadata is used */
{
	lint	size = zip_size
		/* subtract the page header and the longest
		uncompressed data needed for one record */
		- (PAGE_DATA
		   + PAGE_ZIP_DIR_SLOT_SIZE
		   + DATA_TRX_RBP_LEN
		   + 1/* encoded heap_no==2 in page_zip_write_rec() */
		   + 1/* end of modification log */
		   + (compact_metadata ? 2 : 0) /* 2 bytes for n_blobs */
		   - REC_N_NEW_EXTRA_BYTES/* omitted bytes */)
		/* subtract the space for page_zip_fields_encode() */
		- compressBound(static_cast<uLong>(2 * (n_fields + 1)));
	return(size > 0 ? (ulint) size : 0);
}

#endif /* !UNIV_HOTBACKUP */

extern "C" {

/**********************************************************************//**
Allocate memory for zlib. */
static
void*
page_zip_zalloc(
/*============*/
	void*	opaque,	/*!< in/out: memory heap */
	uInt	items,	/*!< in: number of items to allocate */
	uInt	size)	/*!< in: size of an item in bytes */
{
	return(mem_heap_zalloc(static_cast<mem_heap_t*>(opaque), items * size));
}

/**********************************************************************//**
Deallocate memory for zlib. */
static
void
page_zip_free(
/*==========*/
	void*	opaque __attribute__((unused)),	/*!< in: memory heap */
	void*	address __attribute__((unused)))/*!< in: object to free */
{
}

} /* extern "C" */

/**********************************************************************//**
Configure the zlib allocator to use the given memory heap. */
UNIV_INTERN
void
page_zip_set_alloc(
/*===============*/
	void*		stream,		/*!< in/out: zlib stream */
	mem_heap_t*	heap)		/*!< in: memory heap to use */
{
	z_stream*	strm = static_cast<z_stream*>(stream);

	strm->zalloc = page_zip_zalloc;
	strm->zfree = page_zip_free;
	strm->opaque = heap;
}

#ifdef UNIV_DEBUG
/** Set this variable in a debugger to enable
excessive logging in page_zip_compress(). */
UNIV_INTERN ibool	page_zip_compress_dbg;
/** Set this variable in a debugger to enable
binary logging of the data passed to deflate().
When this variable is nonzero, it will act
as a log file name generator. */
UNIV_INTERN unsigned	page_zip_compress_log;

/**********************************************************************//**
Wrapper for deflate().  Log the operation if page_zip_compress_dbg is set.
@return	deflate() status: Z_OK, Z_BUF_ERROR, ... */
static
int
page_zip_compress_deflate(
/*======================*/
	FILE*		logfile,/*!< in: log file, or NULL */
	z_streamp	strm,	/*!< in/out: compressed stream for deflate() */
	int		flush)	/*!< in: deflate() flushing method */
{
	int	status;
	if (UNIV_UNLIKELY(page_zip_compress_dbg)) {
		ut_print_buf(stderr, strm->next_in, strm->avail_in);
	}
	if (UNIV_LIKELY_NULL(logfile)) {
		blind_fwrite(strm->next_in, 1, strm->avail_in, logfile);
	}
	status = deflate(strm, flush);
	if (UNIV_UNLIKELY(page_zip_compress_dbg)) {
		fprintf(stderr, " -> %d\n", status);
	}
	return(status);
}

/* Redefine deflate(). */
# undef deflate
/** Debug wrapper for the zlib compression routine deflate().
Log the operation if page_zip_compress_dbg is set.
@param strm	in/out: compressed stream
@param flush	in: flushing method
@return		deflate() status: Z_OK, Z_BUF_ERROR, ... */
# define deflate(strm, flush) page_zip_compress_deflate(logfile, strm, flush)
/** Declaration of the logfile parameter */
# define FILE_LOGFILE FILE* logfile,
/** The logfile parameter */
# define LOGFILE logfile,
#else /* UNIV_DEBUG */
/** Empty declaration of the logfile parameter */
# define FILE_LOGFILE
/** Missing logfile parameter */
# define LOGFILE
#endif /* UNIV_DEBUG */


/**********************************************************************//**
Store the blobs for the given record pn the blob storage. For compact metadata
format, the blobs are stored before the transaction ids and rollback pointers.
This means that normally we need to compute the number of blobs to compute the
size of the blob storage before writing the transaction id and rollback
pointers, however, typically most pages do not have any blob pointers so we do
not go through the records to compute the number of blobs until we encounter
the first blob pointer. */
static
void
page_zip_store_blobs_for_rec(
	page_zip_des_t* page_zip, /* compressed page and its metadata */
	dict_index_t* index, /* index object for the page */
	ulint rec_no, /* record number for the record whose blob pointers are
			 to be stored in the blob storage. */
	const rec_t* rec, /* pointer to the actual record */
	const ulint* offsets, /* offsets of the record */
	byte* blob_storage, /* pointer to the blob storage */
	byte** trx_rbp_storage_ptr) /* double pointer to trx rbp storage. can be
				       modified if compact metadata format is
				       used */
{
	ulint n_ext = rec_offs_n_extern(offsets);
	/* TODO(rongrong): when we stop compressing garbage, we won't have to
	   check whether the record was purged */
	if (!n_ext || page_zip_dir_find_free(page_zip, page_offset(rec))) {
		return;
	}

	if (page_zip->compact_metadata && !page_zip->n_blobs) {
		/* This is the first time we hit a record that has some blob
		   pointers. We now compute the number of blobs on the page to
		   make room for the blob pointer storage which comes before
		   trx id and rbps for compact metadata format. */
		ulint n_blobs = page_zip_get_n_blobs(page_zip, page_align(rec),
						     index);
		byte* trx_rbp_storage_end = (*trx_rbp_storage_ptr)
					    - (rec_no * DATA_TRX_RBP_LEN);
		/* move the storage for trx rbps */
		memmove(trx_rbp_storage_end
			- (n_blobs * BTR_EXTERN_FIELD_REF_SIZE),
			trx_rbp_storage_end,
			rec_no * DATA_TRX_RBP_LEN);
		*trx_rbp_storage_ptr -= n_blobs * BTR_EXTERN_FIELD_REF_SIZE;
		/* write the number of blobs to the beginning of blob storage */
		mach_write_to_2(blob_storage, n_blobs);
		/* zerofill the storage for blob pointers */
		memset(blob_storage - n_blobs * BTR_EXTERN_FIELD_REF_SIZE,
		       0,
		       n_blobs * BTR_EXTERN_FIELD_REF_SIZE);
	}

	page_zip_store_blobs(blob_storage, rec, offsets, page_zip->n_blobs);
	page_zip->n_blobs += n_ext;
}

/**********************************************************************//**
* Restore trx id and rollback pointer of a record from the trailer of the
* compressed page.
*/
static
void
page_zip_restore_trx_rbp(
	byte* trx_rbp_storage, /* in: storage for transaciton ids and rollback
				  pointers */
	rec_t* rec, /* in/out: the record whose uncompressed fields must be
		       restored from the trailer of page_zip->data */
	ulint rec_no, /* in: the index of rec in recs. This is basically
			 heap_no - PAGE_HEAP_NO_USER_LOW */
	const ulint* offsets, /* in: the offsets of the record as obtained by
				 rec_get_offsets() */
	ulint trx_id_col)
{
	ulint len;
	byte* dst;
	ut_ad(trx_id_col && (trx_id_col != ULINT_UNDEFINED));
	dst = rec_get_nth_field(rec, offsets, trx_id_col, &len);
	ut_ad(len >= DATA_TRX_RBP_LEN);
	memcpy(dst,
	       trx_rbp_storage - DATA_TRX_RBP_LEN * (rec_no + 1),
	       DATA_TRX_RBP_LEN);
}

/**********************************************************************//**
* Restore the blob pointers of a record from the trailer of the compressed
* page.
*/
static
void
page_zip_restore_blobs(
	byte* externs, /* in: storage for blob pointers */
	rec_t* rec, /* in/out: the record whose uncompressed fields must be
		       restored from the trailer of page_zip->data */
	const ulint* offsets, /* in: the offsets of the record as obtained by
				 rec_get_offsets() */
	ulint n_prev_blobs) /* number of blob pointers that belong to the
			       previous records on the same page */
{
	ulint i;
	ulint len;
	byte* dst;
	externs -= n_prev_blobs * BTR_EXTERN_FIELD_REF_SIZE;

	for (i = 0; i < rec_offs_n_fields(offsets); i++) {
		if (rec_offs_nth_extern(offsets, i)) {
			dst = rec_get_nth_field(rec, offsets, i, &len);
			if (UNIV_UNLIKELY(len < BTR_EXTERN_FIELD_REF_SIZE)) {
				page_zip_fail("length of a field with an "
					       "external pointer can not be "
					       "less than %d",
					       BTR_EXTERN_FIELD_REF_SIZE);
				ut_error;
			}
			dst += len - BTR_EXTERN_FIELD_REF_SIZE;
			/* restore the BLOB pointer */
			externs -= BTR_EXTERN_FIELD_REF_SIZE;
			memcpy(dst, externs, BTR_EXTERN_FIELD_REF_SIZE);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////

/**********************************************************************//**
Restore uncompressed fields (trx id, rollback ptr, blobs) of all of the
records on a page from the trailer of the compressed page */
static
void
page_zip_restore_uncompressed_fields_all(  
	page_zip_des_t*	page_zip,	/*!< in: page_zip->data trailer and
					page header. out: page_zip->n_blobs; */
	rec_t**	recs,			/*!< in: the records on the page
					obtained by page_zip_dir_decode().
					out: uncompressed parts of the
					records. */
	const dict_index_t*	index,	/*!< in: the index object describes the
					columns of the table */
	ulint	trx_id_col,		/*!< in: the column number for the
					transaction id */
	mem_heap_t*	heap)		/*!< in/out: temporary memory heap */
{
	uint rec_no;
	ulint n_dense = page_zip_dir_elems(page_zip);
	ulint* offsets = NULL;
	rec_t* rec;

	/* This must be a primary key leaf page or a node pointer page */
	ut_ad(!page_is_leaf(page_zip->data)
	      || (trx_id_col && trx_id_col < ULINT_UNDEFINED));

	if (UNIV_UNLIKELY(trx_id_col == ULINT_UNDEFINED)) {
		/* node pointer page */
		byte* node_ptrs_storage = page_zip_dir_start(page_zip);
		for (rec_no = 0; rec_no < n_dense; ++rec_no) {
			rec = recs[rec_no];
			ut_ad(rec_no + PAGE_HEAP_NO_USER_LOW
			      == rec_get_heap_no_new(rec));
			offsets = rec_get_offsets(rec, index, offsets,
						  ULINT_UNDEFINED, &heap);
			memcpy(rec_get_end(rec, offsets) - REC_NODE_PTR_SIZE,
			       node_ptrs_storage
			       - REC_NODE_PTR_SIZE * (rec_no + 1),
			       REC_NODE_PTR_SIZE);
		}
	} else {
		/* primary key leaf page */
		byte* trx_rbp_storage;
		byte* externs;
		ulint n_ext;
		/* page_zip->n_blobs must be zero before restoring uncompressed
		   fields */
		externs = page_zip_get_blob_ptr_storage(page_zip);
		trx_rbp_storage = page_zip_get_trx_rbp_storage(page_zip, true);
		for (rec_no = 0; rec_no < n_dense; ++rec_no) {
			rec = recs[rec_no];
			ut_ad(rec_no + PAGE_HEAP_NO_USER_LOW
			      == rec_get_heap_no_new(rec));
			offsets = rec_get_offsets(rec, index, offsets,
						  ULINT_UNDEFINED, &heap);
			page_zip_restore_trx_rbp(trx_rbp_storage, rec, rec_no,
						 offsets, trx_id_col);
			n_ext = rec_offs_n_extern(offsets);
			if (n_ext
			    && !page_zip_dir_find_free(page_zip,
						       page_offset(rec))) {
				page_zip_restore_blobs(
					externs, rec, offsets,
					page_zip->n_blobs);
				page_zip->n_blobs += n_ext;
			}
		}
	}
}

/**********************************************************************//**
Write the data portion of a clustered leaf record with external pointers to a
data stream in forward direction.
@return the pointer to the stream after the record is written. */
static
byte*
page_zip_serialize_clust_ext_rec(
#ifdef UNIV_DEBUG
	ibool mlog, /*!< in: If TRUE this function is called to write the record
		      to the modification log */
#endif
	byte*	data,	/*!< out: pointer to the data stream
			where the record is written */
	const rec_t*	rec,	/*!< in: the record that is written
				to the data stream */
	const ulint*	offsets,	/*!< in: offsets for the record as
					obtained by rec_get_offsets() */
	ulint	trx_id_col) /* in: the colum no. for the transaction id */
{
	uint	i;
	const byte*	src;
	ulint	len;
	const byte*	start = rec;
	for (i = 0; i < rec_offs_n_fields(offsets); ++i) {
		if (UNIV_UNLIKELY(i == trx_id_col)) {
			ut_a(!rec_offs_nth_extern(offsets, i));
			ut_a(!rec_offs_nth_extern(offsets, i + 1));
			/* Locate trx_id and roll_ptr. */
			src = rec_get_nth_field(rec, offsets, i, &len);
			ut_a(len == DATA_TRX_ID_LEN);
			ut_a(src + DATA_TRX_ID_LEN
			     == rec_get_nth_field(rec, offsets,
			     			  i + 1, &len));
			ut_a(len == DATA_ROLL_PTR_LEN);
#ifdef UNIV_DEBUG
			if (mlog)
				ASSERT_ZERO(data, src - start);
#endif
			/* Write the preceding fields. */
			memcpy(data, start, src - start);
			data += src - start;
			start = src + DATA_TRX_RBP_LEN;
			i++; /* skip also roll_ptr */
		} else if (rec_offs_nth_extern(offsets, i)) {
			src = rec_get_nth_field(rec, offsets, i, &len);

			ut_a(len >= BTR_EXTERN_FIELD_REF_SIZE);
			src += len - BTR_EXTERN_FIELD_REF_SIZE;

			memcpy(data, start, src - start);
			data += src - start;
			start = src + BTR_EXTERN_FIELD_REF_SIZE;
		}
	}

	/* Log the last bytes of the record. */
	len = rec_offs_data_size(offsets) - (start - rec);

#ifdef UNIV_DEBUG
	if (mlog)
		ASSERT_ZERO(data, len);
#endif
	memcpy(data, start, len);
	data += len;
	return (data);
}

/**********************************************************************//**
Write the data portion of a clustered leaf record with no external pointers
to a data stream in forward direction. Uncompressed fields are omitted.
@return the pointer to the stream after the record is written. */
static
byte*
page_zip_serialize_clust_rec(
#ifdef UNIV_DEBUG
	ibool mlog, /*!< in: If TRUE this function is called to write the record
		      to the modification log */
#endif
	byte*	data,	/*!< out: pointer to the data stream
			where the record is written. */
	const rec_t*	rec,	/*!< in: the record that is written
				to the data stream */
	const ulint*	offsets,	/*!< in: offsets for the record as
					obtained by rec_get_offsets() */
	ulint	trx_id_col)	/* in: the column no. for the
				transaction id column. */
{
	ulint	len;
	/* Locate trx_id and roll_ptr. */
	const byte*	src = rec_get_nth_field(rec, offsets,
						trx_id_col, &len);

	ut_ad(len == DATA_TRX_ID_LEN);
	ut_ad(src + DATA_TRX_ID_LEN
	      == rec_get_nth_field(rec, offsets, trx_id_col + 1, &len));
	ut_ad(len == DATA_ROLL_PTR_LEN);

	/* Write the preceding fields. */
#ifdef UNIV_DEBUG
	if (mlog)
		ASSERT_ZERO(data, src - rec);
#endif
	memcpy(data, rec, src - rec);
	data += src - rec;
	/* Advance the record pointer to skip trx_id and roll_ptr */
	src += DATA_TRX_RBP_LEN;
	/* Write the last bytes of the record */
	len = rec_offs_data_size(offsets) - (src - rec);
#ifdef UNIV_DEBUG
	if (mlog)
		ASSERT_ZERO(data, len);
#endif
	memcpy(data, src, len);
	data += len;
	return data;
}

/**********************************************************************//**
Write the data portion of a record of a secondary index leaf page to a data
stream in forward direction.
@return the pointer to the stream after the record is written. */
static
byte*
page_zip_serialize_sec_rec(
#ifdef UNIV_DEBUG
	ibool mlog, /*!< in: If TRUE this function is called to write the record
		      to the modification log. */
#endif
	byte*	data,	/*!< out: pointer to the data stream where the
			record is written */
	const rec_t*	rec,	/*!< in: the record that is written
				to the data stream */
	const ulint*	offsets)	/*!< in: offsets for the record as
					obtained by rec_get_offsets() */
{
	ulint	len = rec_offs_data_size(offsets);
	/* Leaf page of a secondary index: no externally stored columns */
	ut_ad(!rec_offs_any_extern(offsets));
#ifdef UNIV_DEBUG
	if (mlog)
		/* Write the entire record */
		ASSERT_ZERO(data, len);
#endif
	memcpy(data, rec, len);
	data += len;
	return data;
}

/**********************************************************************//**
Write the data portion of a record of a non-leaf page to a data stream in
forward direction.
@return the pointer to the stream after the record is written. */
static
byte*
page_zip_serialize_node_ptrs_rec(
#ifdef UNIV_DEBUG
	ibool mlog, /*!< in: If TRUE this function is called to write the record
		      to the modification log */
#endif
	byte*	data,	/*!< out: pointer to the data stream where
			the record is written */
	const rec_t*	rec,	/*!< in: the record that is written
				to the data stream */
	const ulint*	offsets)	/*!< in: offsets for the record as
					obtained by rec_get_offsets() */
{
	ulint	len = rec_offs_data_size(offsets) - REC_NODE_PTR_SIZE;
	/* Non-leaf nodes should not have any externally stored columns. */
	ut_ad(!rec_offs_any_extern(offsets));
	/* Copy the data bytes, except node_ptr. */
#ifdef UNIV_DEBUG
	if (mlog)
		ASSERT_ZERO(data, len);
#endif
	memcpy(data, rec, len);
	data += len;
	return data;
}

/**********************************************************************//**
Write a record to a data stream in forward direction. The record is written in
such a way that it can be recovered later if the data stream is read in forward
direction.
For leaf pages of clustered indexes and non-leaf pages for any index, the
uncompressed fields are not written to the data stream.
In particular, this means that the offsets of the record are reversed because
they are normally stored in backwards direction.
@return the pointer to the stream after the record is written. */
static
byte*
page_zip_serialize_rec(
#ifdef UNIV_DEBUG
	ibool mlog, /*!< in: If TRUE this function is called to write the record
		      to modification log */
#endif
	byte*	data,	/*!< out: pointer to the data stream where
			the record is written */
	const rec_t*	rec,	/*!< in: the record that is written to
				the data stream */
	const ulint*	offsets,	/*!< in: offsets for the record as
					obtained by rec_get_offsets() */
	ulint	trx_id_col)	/*!< in: the column no. for the
				transaction id column. trx_id_col = 0
				implies a secondary index leaf page, and
				trx_id_col = ULINT_UNDEFINED implies a non-leaf
				page. */
{
	const byte*	start = rec - rec_offs_extra_size(offsets);
	const byte*	b = rec - REC_N_NEW_EXTRA_BYTES;

	/* Write the extra bytes backwards, so rec_offs_extra_size() can be
 * 	easily computed by invoking rec_get_offsets_reverse(). */
	while (b != start) {
		*data++ = *--b;
		ut_ad(!mlog || !*data);
	}

	if (UNIV_LIKELY(trx_id_col != ULINT_UNDEFINED)) {
		if (trx_id_col) {
			/* leaf page in a clustered index */
			if (UNIV_UNLIKELY(rec_offs_any_extern(offsets))) {
#ifdef UNIV_DEBUG
				return page_zip_serialize_clust_ext_rec(
						mlog, data, rec,
						offsets, trx_id_col);
#else
				return page_zip_serialize_clust_ext_rec(
						data, rec, offsets, trx_id_col);
#endif
			} else {
#ifdef UNIV_DEBUG
				return page_zip_serialize_clust_rec(
						mlog, data, rec, offsets,
						trx_id_col);
#else
				return page_zip_serialize_clust_rec(
						data, rec, offsets,
						trx_id_col);
#endif
			}
		} else {
#ifdef UNIV_DEBUG
			return page_zip_serialize_sec_rec(
					mlog, data, rec, offsets);
#else
			/* leaf page in a secondary index */
			return page_zip_serialize_sec_rec(
					data, rec, offsets);
#endif
		}
	}
	else {
		/* non-leaf page */
		ut_ad(trx_id_col == ULINT_UNDEFINED);
#ifdef UNIV_DEBUG
		return page_zip_serialize_node_ptrs_rec(
				mlog, data, rec, offsets);
#else
		return page_zip_serialize_node_ptrs_rec(data, rec, offsets);
#endif
	}
}

/**********************************************************************//**
Read the data portion of a clustered leaf record with external pointers from
a data stream in forward direction.
@return the pointer to the stream after the record is read. */
static
const byte*
page_zip_deserialize_clust_ext_rec(
	const byte*	data,		/*!< in: pointer to the data stream
					where the record is to be read. */
	rec_t*		rec,		/*!< out: the record that is read
					from the data stream */
	const ulint*	offsets,	/*!< in: offsets for the record as
					obtained by rec_get_offsets() */
	ulint		trx_id_col,	/*!< in: the column no. for the
					transaction id column. */
	const byte*	end)		/*!< in: the end of the deserialization
					stream. Used only for validation. */
{
	ulint	i;
	ulint	len;
	byte*	next_out = rec;
	byte*	dst;

	/* Check if there are any externally stored columns.
	For each externally stored column, skip the BTR_EXTERN_FIELD_REF
	bytes used for blob pointers. */
	for (i = 0; i < rec_offs_n_fields(offsets); ++i) {
		if (UNIV_UNLIKELY(i == trx_id_col)) {
			/* Skip trx_id and roll_ptr */
			dst = rec_get_nth_field(rec, offsets, i, &len);
			/* zerofill the trx id and roll ptr of the record */
			memset(dst, 0, DATA_TRX_RBP_LEN);
			if (UNIV_UNLIKELY(dst - next_out >= end - data)) {
				page_zip_fail(
					"page_zip_deserialize_clust_ext_rec: "
					"trx_id len %lu, %p - %p >= %p - %p\n",
					(ulong) len,
					(const void*) dst,
					(const void*) next_out,
					(const void*) end,
					(const void*) data);
				ut_error;
			}
			if (UNIV_UNLIKELY(len < DATA_TRX_RBP_LEN)) {
				page_zip_fail(
					"page_zip_deserialize_clust_ext_rec: "
					"trx_id len %lu < %d\n",
					len,
					DATA_TRX_RBP_LEN);
				ut_error;
			}
			if (UNIV_UNLIKELY(rec_offs_nth_extern(offsets, i))) {
				page_zip_fail(
					"page_zip_serialize_clust_ext_rec: "
					"column %lu must be trx id column but "
					"it has blob pointers.\n", i);
				ut_error;
			}

			memcpy(next_out, data, dst - next_out);
			data += dst - next_out;
			next_out = dst + DATA_TRX_RBP_LEN;
		} else if (rec_offs_nth_extern(offsets, i)) {
			dst = rec_get_nth_field(rec, offsets, i, &len);
			ut_a(len >= BTR_EXTERN_FIELD_REF_SIZE);
			/* zerofill the blob pointer of the column */
			memset(dst + len - BTR_EXTERN_FIELD_REF_SIZE,
			       0,
			       BTR_EXTERN_FIELD_REF_SIZE);
			len += dst - next_out - BTR_EXTERN_FIELD_REF_SIZE;

			if (UNIV_UNLIKELY(data + len >= end)) {
				page_zip_fail(
					"page_zip_deserialize_clust_ext_rec: "
					"ext %p + %lu >= %p\n",
					(const void*) data,
					(ulong) len,
					(const void*) end);
				ut_error;
			}

			memcpy(next_out, data, len);
			data += len;
			next_out += len + BTR_EXTERN_FIELD_REF_SIZE;
		}
	}

	/* Copy the last bytes of the record */
	len = rec_get_end(rec, offsets) - next_out;
	if (UNIV_UNLIKELY(data + len >= end)) {
		page_zip_fail("page_zip_deserialize_clust_ext_rec: "
			       "last %p + %lu >= %p\n",
			       (const void*) data,
			       (ulong) len,
			       (const void*) end);
		ut_error;
	}
	memcpy(next_out, data, len);
	data += len;
	return (data);
}

/**********************************************************************//**
Read the data portion of a clustered leaf record with no external pointers
from a data stream in forward direction. Uncompressed fields are not read.
@return the pointer to the stream after the record is read. */
static
const byte*
page_zip_deserialize_clust_rec(
	const byte*	data,		/*!< in: pointer to the data stream
					where the record is to be read */
	rec_t*		rec,		/*!< out: the record that is read
					from the data stream */
	const ulint*	offsets,	/*!< in: offsets for the record as
					obtained by rec_get_offsets() */
	ulint		trx_id_col,	/*!< in: the column no for the
					transaction id column */
	const byte*	end)		/*!< in: the end of the deserialization
					stream. Used only for validation. */
{
	ulint	len;
	/* Skip DB_TRX_ID and DB_ROLL_PTR */
	ulint	l = rec_get_nth_field_offs(offsets, trx_id_col, &len);
	byte*	b;

	if (UNIV_UNLIKELY(data + l >= end)) {
		page_zip_fail("page_zip_deserialize_clust_rec: "
			       "%p + %lu >= %p\n",
			       (const void*) data,
			       (ulong) l,
			       (const void*) end);
		ut_error;
	}

	/* Copy any preceding data bytes */
	memcpy(rec, data, l);
	data += l;

	/* zerfoill DB_TRX_ID and DB_ROLL_PTR */
	memset(rec + l, 0, DATA_TRX_RBP_LEN);

	/* Copy any bytes following DB_TRX_ID, DB_ROLL_PTR */
	b = rec + l + DATA_TRX_RBP_LEN;
	len = rec_get_end(rec, offsets) - b;
	if (UNIV_UNLIKELY(data + len >= end)) {
		page_zip_fail("page_zip_deserialize_clust_rec: "
			       "%p + %lu >= %p\n",
			       (const void*) data,
			       (ulong) len,
			       (const void*) end);
		ut_error;
	}
	memcpy(b, data, len);
	data += len;
	return (data);
}

/**********************************************************************//**
Read the data portion of a record of a secondary index leaf page from a data
stream in forward direction.
@return the pointer to the stream after the record is read. */
static
const byte*
page_zip_deserialize_sec_rec(
	const byte*	data,		/*!< in: pointer to the data stream
					where the record is to be read */
	rec_t*		rec,		/*!< out: the record that is read
					from the data stream */
	const ulint*	offsets,	/*!< in: offsets for the record as
					obtained by rec_get_offsets() */
	const byte*	end)		/*!< in: the end of the deserialization
					stream. Used only for validation. */
{
	ulint	len = rec_offs_data_size(offsets);

	/* Copy all data bytes of a record in a secondary index */
	if (UNIV_UNLIKELY(data + len >= end)) {
		page_zip_fail("page_zip_deserialize_sec_rec :"
			       "sec %p + %lu >= %p\n",
			       (const void*) data,
			       (ulong) len,
			       (const void*) end);
		ut_error;
	}
	memcpy(rec, data, len);
	data += len;
	return (data);
}

/**********************************************************************//**
Read the data portion of a record of a non-leaf page to a data stream in
forward direction.
@return the pointer to the stream after the record is read */
static
const byte*
page_zip_deserialize_node_ptrs_rec(
	const byte*	data,		/*!< in: pointer to the data stream
					where the record is to be read */
	rec_t*		rec,		/*!< out: the record that is read from
					the data stream */
	const ulint*	offsets,	/*!< in: offsets for the record as
					obtained by rec_get_offsets() */
	const byte*	end)		/*!< in: the end of the deserialization
					stream. Used only for validation. */
{
	ulint	len = rec_offs_data_size(offsets) - REC_NODE_PTR_SIZE;
	/* Copy the data bytes, except node_ptr */
	if (UNIV_UNLIKELY(data + len >= end)) {
		page_zip_fail("page_zip_deserialize_sec_rec: "
			       "node_ptr %p + %lu >= %p\n",
			       (const void*) data,
			       (ulong) len,
			       (const void*) end);
		ut_error;
	}
	memcpy(rec, data, len);
	data += len;
	return (data);
}

/**********************************************************************//**
Read a record from a data stream in forward direction except for the
uncompressed fields.
@return the pointer to the stream after the record is read */
static
const byte*
page_zip_deserialize_rec(
	const byte*		data,		/*!< in: pointer to the data
						stream where the record is to
						be read */
	rec_t*			rec,		/*!< out: the record that is
						read from the data stream */
	const dict_index_t*	index,		/*!< in: the index object for
						the table */
	ulint*			offsets,	/*!< out: used for offsets for
						the record as obtained by
						rec_get_offsets_reverse() */
	ulint			trx_id_col,	/*!< in: the column no for
						transaction id column */
	ulint			heap_status,	/*!< in: heap_no and status bits
						for the record */
	const byte*		end)		/*!< in: the end of the
						deserialization stream. Used
						only for validation. */
{
	byte*	start;
	byte*	b;
	rec_get_offsets_reverse(data, index,
				heap_status & REC_STATUS_NODE_PTR, offsets);
	rec_offs_make_valid(rec, index, offsets);
	start = rec_get_start(rec, offsets);
	b = rec - REC_N_NEW_EXTRA_BYTES;

	/* Copy the extra bytes (backwards) */
	while (b != start) {
		*--b = *data++;
	}

	if (UNIV_LIKELY(trx_id_col != ULINT_UNDEFINED)) {
		if (trx_id_col) {
			/* Clustered leaf page */
			if (UNIV_UNLIKELY(rec_offs_any_extern(offsets))) {
				return page_zip_deserialize_clust_ext_rec(
						data,
						rec,
						offsets,
						trx_id_col,
						end);
			} else {
				return page_zip_deserialize_clust_rec(
						data,
						rec,
						offsets,
						trx_id_col,
						end);
			}
		} else {
			/* secondary leaf page */
			return page_zip_deserialize_sec_rec(data,
							    rec,
							    offsets,
							    end);
		}
	} else {
		/* non-leaf page */
		return page_zip_deserialize_node_ptrs_rec(data,
							  rec,
							  offsets,
							  end);
	}
}

/**********************************************************************//**
Compress the records of a node pointer page.
@return	Z_OK, or a zlib error code */
static
int
page_zip_compress_node_ptrs(
/*========================*/
	FILE_LOGFILE
	page_zip_des_t*	page_zip,	/*!< in: used for reading headers in
					page_zip->data. out: write uncompressed
					fields to the trailer of
					page_zip->data. */
	z_stream*	c_stream,	/*!< in/out: compressed page stream */
	const rec_t**	recs,		/*!< in: dense page directory
					sorted by address */
	dict_index_t*	index,		/*!< in: the index of the page */
	mem_heap_t*	heap)		/*!< in: temporary memory heap */
{
	int	err	= Z_OK;
	ulint*	offsets = NULL;
	ulint	n_dense = page_zip_dir_elems(page_zip);
	ulint rec_no;
	byte* node_ptrs_storage = page_zip_dir_start(page_zip);

	do {
		const rec_t*	rec = *recs++;

		offsets = rec_get_offsets(rec, index, offsets,
					  ULINT_UNDEFINED, &heap);
		/* Only leaf nodes may contain externally stored columns. */
		ut_ad(!rec_offs_any_extern(offsets));

		UNIV_MEM_ASSERT_RW(rec, rec_offs_data_size(offsets));
		UNIV_MEM_ASSERT_RW(rec - rec_offs_extra_size(offsets),
				   rec_offs_extra_size(offsets));

		/* Compress the extra bytes. */
		c_stream->avail_in = static_cast<uInt>(
			rec - REC_N_NEW_EXTRA_BYTES - c_stream->next_in);

		if (c_stream->avail_in) {
			err = deflate(c_stream, Z_NO_FLUSH);
			if (UNIV_UNLIKELY(err != Z_OK)) {
				break;
			}
		}
		ut_ad(!c_stream->avail_in);

		/* Compress the data bytes, except node_ptr. */
		c_stream->next_in = (byte*) rec;
		c_stream->avail_in = static_cast<uInt>(
			rec_offs_data_size(offsets) - REC_NODE_PTR_SIZE);

		if (c_stream->avail_in) {
			err = deflate(c_stream, Z_NO_FLUSH);
			if (UNIV_UNLIKELY(err != Z_OK)) {
				break;
			}
		}

		ut_ad(!c_stream->avail_in);

		c_stream->next_in += REC_NODE_PTR_SIZE;
		/* store the pointers uncompressed */
		rec_no = rec_get_heap_no_new(rec) - PAGE_HEAP_NO_USER_LOW;
		memcpy(node_ptrs_storage - REC_NODE_PTR_SIZE * (rec_no + 1),
		       rec_get_end((rec_t*)rec, offsets) - REC_NODE_PTR_SIZE,
		       REC_NODE_PTR_SIZE);
	} while (--n_dense);

	return(err);
}

/**********************************************************************//**
Compress the records of a leaf node of a secondary index.
@return	Z_OK, or a zlib error code */
static
int
page_zip_compress_sec(
/*==================*/
	FILE_LOGFILE
	z_stream*	c_stream,	/*!< in/out: compressed page stream */
	const rec_t**	recs,		/*!< in: dense page directory
					sorted by address */
	ulint		n_dense,	/*!< in: size of recs[] */
	const dict_index_t* index, /*!< the index of the page */
	mem_heap_t* heap) /*!< temporary memory heap */
{
	int		err	= Z_OK;
	const rec_t* rec = NULL;
	ulint *offsets = NULL;

	ut_ad(n_dense > 0);

	do {
		rec = *recs++;

		/* Compress everything up to this record. */
		c_stream->avail_in = static_cast<uInt>(
			rec - REC_N_NEW_EXTRA_BYTES
			- c_stream->next_in);

		if (UNIV_LIKELY(c_stream->avail_in)) {
			UNIV_MEM_ASSERT_RW(c_stream->next_in,
					   c_stream->avail_in);
			err = deflate(c_stream, Z_NO_FLUSH);
			if (UNIV_UNLIKELY(err != Z_OK)) {
				goto func_exit;
			}
		}

		ut_ad(!c_stream->avail_in);
		ut_ad(c_stream->next_in == rec - REC_N_NEW_EXTRA_BYTES);

		/* Skip the REC_N_NEW_EXTRA_BYTES. */

		c_stream->next_in = (byte*) rec;
	} while (--n_dense);

	/* Compress until the end of the last record */
	ut_ad(rec);
	offsets = rec_get_offsets(rec, index, offsets, ULINT_UNDEFINED, &heap);
	c_stream->avail_in = rec_get_end((rec_t*) rec, offsets)
	                     - c_stream->next_in;
	err = deflate(c_stream, Z_NO_FLUSH);

func_exit:
	return(err);
}

/**********************************************************************//**
Add one record with no blob pointers to the compression stream of a
clustered page. */
static
int
page_zip_compress_clust_rec(
/*========================*/
	FILE_LOGFILE
	z_stream*	c_stream,	/*!< in/out: compressed page stream */
	const rec_t*	rec,		/*!< in: record */
	const ulint*	offsets,	/*!< in: rec_get_offsets(rec) */
	ulint	trx_id_col)		/*!< in: the column number for the
					transaction id */
{
	int	err;
	ulint	len;
	const byte*	src;

	/* Compress the extra bytes. */
	c_stream->avail_in = static_cast<uInt>(
		rec - REC_N_NEW_EXTRA_BYTES
		- c_stream->next_in);

	if (c_stream->avail_in) {
		err = deflate(c_stream, Z_NO_FLUSH);
		if (UNIV_UNLIKELY(err != Z_OK)) {
			return err;
		}
	}

	ut_ad(!c_stream->avail_in);
	ut_ad(c_stream->next_in == rec - REC_N_NEW_EXTRA_BYTES);

	/* Compress the data bytes. */
	c_stream->next_in = (byte*) rec;

	/* Compress the part of the record up to trx_id. */
	src = rec_get_nth_field(rec, offsets,
				trx_id_col, &len);
	ut_ad(src + DATA_TRX_ID_LEN
	      == rec_get_nth_field(rec, offsets,
				   trx_id_col + 1, &len));
	ut_ad(len == DATA_ROLL_PTR_LEN);
	UNIV_MEM_ASSERT_RW(rec, rec_offs_data_size(offsets));
	UNIV_MEM_ASSERT_RW(rec - rec_offs_extra_size(offsets),
			   rec_offs_extra_size(offsets));

	/* Compress any preceding bytes */
	c_stream->avail_in = static_cast<uInt>(src - c_stream->next_in);

	if (c_stream->avail_in) {
		err = deflate(c_stream, Z_NO_FLUSH);
		if (UNIV_UNLIKELY(err != Z_OK)) {
			return (err);
		}
	}

	ut_ad(!c_stream->avail_in);
	ut_ad(c_stream->next_in == src);

	c_stream->next_in += DATA_TRX_RBP_LEN;

	/* Skip also roll_ptr */
	ut_ad(trx_id_col + 1 < rec_offs_n_fields(offsets));

	/* Compress the last bytes of the record. */
	c_stream->avail_in = static_cast<uInt>(
		rec + rec_offs_data_size(offsets) - c_stream->next_in);

	if (c_stream->avail_in) {
		err = deflate(c_stream, Z_NO_FLUSH);
		if (UNIV_UNLIKELY(err != Z_OK)) {
			return err;
		}
	}

	ut_ad(!c_stream->avail_in);
	return Z_OK;
}

/**********************************************************************//**
Compress a record of a leaf node of a clustered index that contains
externally stored columns.
@return	Z_OK, or a zlib error code */
static
int
page_zip_compress_clust_ext_rec(
/*========================*/
	FILE_LOGFILE
	page_zip_des_t*	page_zip,	/*!< in: used for reading headers in
					page_zip->data. out: write uncompressed
					fields to the trailer of
					page_zip->data. */
	z_stream*	c_stream,	/*!< in/out: compressed page stream */
	const rec_t*	rec,		/*!< in: record */
	const ulint*	offsets,	/*!< in: rec_get_offsets(rec) */
	ulint		trx_id_col)	/*!< in: position of of DB_TRX_ID */
{
	int	err;
	ulint	i;
	ibool	purged = !!page_zip_dir_find_free(page_zip, page_offset(rec));

	UNIV_MEM_ASSERT_RW(rec, rec_offs_data_size(offsets));
	UNIV_MEM_ASSERT_RW(rec - rec_offs_extra_size(offsets),
			   rec_offs_extra_size(offsets));

	/* Compress the extra bytes */
	c_stream->avail_in = static_cast<uInt>(
		rec - REC_N_NEW_EXTRA_BYTES
		- c_stream->next_in);

	if (c_stream->avail_in) {
		err = deflate(c_stream, Z_NO_FLUSH);
		if (UNIV_UNLIKELY(err != Z_OK)) {
			return err;
		}
	}
	ut_ad(!c_stream->avail_in);
	ut_ad(c_stream->next_in == rec - REC_N_NEW_EXTRA_BYTES);

	/* Compress the data bytes. */
	c_stream->next_in = (byte*) rec;

	for (i = 0; i < rec_offs_n_fields(offsets); i++) {
		ulint		len;
		const byte*	src;

		if (UNIV_UNLIKELY(i == trx_id_col)) {
			ut_ad(!rec_offs_nth_extern(offsets, i));
			/* Store trx_id and roll_ptr
			in uncompressed form. */
			src = rec_get_nth_field(rec, offsets, i, &len);
			ut_ad(src + DATA_TRX_ID_LEN
			      == rec_get_nth_field(rec, offsets,
						   i + 1, &len));
			ut_ad(len == DATA_ROLL_PTR_LEN);

			/* Compress any preceding bytes. */
			c_stream->avail_in = static_cast<uInt>(
				src - c_stream->next_in);

			if (c_stream->avail_in) {
				err = deflate(c_stream, Z_NO_FLUSH);
				if (UNIV_UNLIKELY(err != Z_OK)) {

					return(err);
				}
			}

			ut_ad(!c_stream->avail_in);
			ut_ad(c_stream->next_in == src);

			c_stream->next_in += DATA_TRX_RBP_LEN;

			/* Skip also roll_ptr */
			i++;
		} else if (rec_offs_nth_extern(offsets, i)) {
			src = rec_get_nth_field(rec, offsets, i, &len);
			ut_ad(len >= BTR_EXTERN_FIELD_REF_SIZE);
			src += len - BTR_EXTERN_FIELD_REF_SIZE;

			c_stream->avail_in = static_cast<uInt>(
				src - c_stream->next_in);
			if (UNIV_LIKELY(c_stream->avail_in)) {
				err = deflate(c_stream, Z_NO_FLUSH);
				if (UNIV_UNLIKELY(err != Z_OK)) {

					return(err);
				}
			}

			ut_a(!c_stream->avail_in);
			ut_a(c_stream->next_in == src);

			/* Reserve space for the data at
			the end of the space reserved for
			the compressed data and the page
			modification log. */

			if (UNIV_UNLIKELY
			    (c_stream->avail_out
			     <= BTR_EXTERN_FIELD_REF_SIZE)) {
				/* out of space */
				return(Z_BUF_ERROR);
			}

			c_stream->next_in
				+= BTR_EXTERN_FIELD_REF_SIZE;

			/* If the record is not purged, the available output
			size for compression stream should be reduced by the
			pointer size. */
			if (!purged) {
				c_stream->avail_out -=
					BTR_EXTERN_FIELD_REF_SIZE;
			}
		}
	}

	return(Z_OK);
}

/**********************************************************************//**
Compress the records of a leaf node of a clustered index.
@return	Z_OK, or a zlib error code */
static
int
page_zip_compress_clust(
/*====================*/
	FILE_LOGFILE
	page_zip_des_t*	page_zip,	/*!< in: used for reading headers in
					page_zip->data. out: write uncompressed
					fields to the trailer of
					page_zip->data. */
	z_stream*	c_stream,	/*!< in/out: compressed page stream */
	const rec_t**	recs,		/*!< in: dense page directory
					sorted by address */
	dict_index_t*	index,		/*!< in: the index of the page */
	mem_heap_t*	heap)		/*!< in: temporary memory heap */
{
	int	err	= Z_OK;
	ulint*	offsets	= NULL;
	ulint	n_dense = page_zip_dir_elems(page_zip);
	ulint	trx_id_col = dict_index_get_sys_col_pos(index, DATA_TRX_ID);
	const rec_t* rec;
	ulint rec_no;
	byte* trx_rbp_storage;
	byte* externs;

	ut_ad(page_zip->n_blobs == 0);

	externs = page_zip_get_blob_ptr_storage(page_zip);
	trx_rbp_storage = page_zip_get_trx_rbp_storage(page_zip, false);
	if (page_zip->compact_metadata) {
		mach_write_to_2(externs, 0);
	}

	for (rec_no = 0; rec_no < n_dense; ++rec_no) {
		rec = recs[rec_no];
		ut_ad(rec_no + PAGE_HEAP_NO_USER_LOW
		      == rec_get_heap_no_new(rec));
		offsets = rec_get_offsets(rec, index, offsets,
					  ULINT_UNDEFINED, &heap);
		ut_ad(rec_offs_n_fields(offsets)
		      == dict_index_get_n_fields(index));
		UNIV_MEM_ASSERT_RW(rec, rec_offs_data_size(offsets));
		UNIV_MEM_ASSERT_RW(rec - rec_offs_extra_size(offsets),
				   rec_offs_extra_size(offsets));

		if (rec_offs_any_extern(offsets)) {
			ut_ad(dict_index_is_clust(index));

			err = page_zip_compress_clust_ext_rec(
				LOGFILE
				page_zip, c_stream, rec, offsets, trx_id_col);

			if (UNIV_UNLIKELY(err != Z_OK)) {
				goto func_exit;
			}
		} else {
			err = page_zip_compress_clust_rec(
							  LOGFILE
							  c_stream, rec,
							  offsets,
							  trx_id_col);

			if (UNIV_UNLIKELY(err != Z_OK)) {
				goto func_exit;
			}
		}
		page_zip_store_blobs_for_rec(page_zip, index, rec_no,
					     rec, offsets, externs,
					     &trx_rbp_storage);
		page_zip_store_trx_rbp(trx_rbp_storage, rec, offsets, rec_no,
				       trx_id_col);
		page_zip_size_check(page_zip, index);
	}
func_exit:
	return(err);
}

/* Memory block caches used for caching the blocks used for
page_zip_compress() and page_zip_decompress() */
ulint malloc_cache_compress_len = 1000;
ulint malloc_cache_decompress_len = 1000;
mem_block_cache_t malloc_cache_compress_obj;
mem_block_cache_t malloc_cache_decompress_obj;
mem_block_cache_t* malloc_cache_compress;
mem_block_cache_t* malloc_cache_decompress;

/***************************************************************//**
Below are the amounts of memory used for different phases during
compression and decompression. */
/***************************************************************//**
Memory needed by page_zip_compress(). This doesn't include the memory used by
the functions called by page_zip_compress(). */
#define PZ_MEM_COMP_BASE MEM_SPACE_NEEDED(UNIV_PAGE_SIZE)
/***************************************************************//**
Memory needed for zlib stream compression. Below, we compute an
upperbound for the average amount of memory we need to allocate for
page_zip_compress_zlib_stream() assuming that the average record size of a
table is at least 16 and that there are at most 50 columns on a table. */
#ifdef UNIV_DEBUG
#define PZ_MEM_COMP_ZLIB_STREAM \
	(PZ_MEM_COMP_BASE \
	 + MEM_SPACE_NEEDED(50L * (2 + sizeof(ulint))) \
	 + MEM_SPACE_NEEDED((UNIV_PAGE_SIZE / 16) * sizeof(ulint)) \
	 + MEM_SPACE_NEEDED(DEFLATE_MEMORY_BOUND(UNIV_PAGE_SIZE_SHIFT, \
						 MAX_MEM_LEVEL)) \
	 + 2 * UNIV_PAGE_SIZE)
#else /* UNIV_DEBUG */
#define PZ_MEM_COMP_ZLIB_STREAM \
	(PZ_MEM_COMP_BASE \
	 + MEM_SPACE_NEEDED(50L * (2 + sizeof(ulint))) \
	 + MEM_SPACE_NEEDED((UNIV_PAGE_SIZE / 16) * sizeof(ulint)) \
	 + MEM_SPACE_NEEDED(DEFLATE_MEMORY_BOUND(UNIV_PAGE_SIZE_SHIFT, \
						 MAX_MEM_LEVEL)))
#endif /* UNIV_DEBUG */
/***************************************************************//**
Macros to define the maximum length of a page after its records
are serialized, and the size of the buffer that is used to store
the serialized buffer. Even though the serialization of records
should not require more than UNIV_PAGE_SIZE memory, we give more
just in case the serialization needs some extra space for metadata.
page_zip_serialize() will fail if the length of a page after its
records are serialized is more than PZ_SERIALIZED_LEN_MAX. The
buffer for serialization has to be a bit bigger than
PZ_SERIALIZED_LEN_MAX because we detect whether we exceeded the
length limit after serializing the record. */
#define PZ_SERIALIZED_LEN_MAX (2 * UNIV_PAGE_SIZE)
#define PZ_SERIALIZED_BUF_SIZE (5 * UNIV_PAGE_SIZE / 2)
/***************************************************************//**
The amount of space we would like to reserve on the compressed page
for the compression output. If the trailer of a compressed page occupies
too much space, it may make sense to split that page before even trying
to serialize its records. This helps us abort the compression early
in the process. */
#define PZ_COMPRESSED_LEN_MIN 128
/***************************************************************//**
Memory needed by page_zip_serialize(): memory needed for the serialization
buffer plus the memory needed for the record pointers (recs). We estimate
the number of records to be at most UNIV_PAGE_SIZE / 16, assuming each
record is at least 16 bytes. */
#define PZ_MEM_SERIALIZE \
	(MEM_SPACE_NEEDED(PZ_SERIALIZED_BUF_SIZE) \
	 + MEM_SPACE_NEEDED((UNIV_PAGE_SIZE / 16) * sizeof(ulint)))
/***************************************************************//**
Memory needed by page_zip_decompress(). memory used by the called
functions is not included. See the comment to PZ_MEM_COMP_ZLIB_STREAM for
why we take n_dense to be UNIV_PAGE_SIZE / 16. */
#define PZ_MEM_DECOMP_BASE \
	MEM_SPACE_NEEDED((UNIV_PAGE_SIZE / 16) * (3 * sizeof(rec_t*)))
/***************************************************************//**
Total memory needed by zlib stream decompression: memory needed to store
the offsets parameter, the encoding of the index and the decompression
operation. See page_zip_decompress_zlib_stream() for details. */
#define PZ_MEM_DECOMP_ZLIB_STREAM \
	(PZ_MEM_DECOMP_BASE \
	 + MEM_SPACE_NEEDED(50L * sizeof(ulint)) \
	 + MEM_SPACE_NEEDED(INFLATE_MEMORY_BOUND(UNIV_PAGE_SIZE_SHIFT)))
/***************************************************************//**
Memory needed by page_zip_deserialize. Note that PZ_SERIALIZED_LEN_MAX
is for the buffer that will be used by page_zip_deserialize(). This
buffer must be allocated by the caller but we count it towards
deserialization */
#define PZ_MEM_DESERIALIZE \
	(PZ_SERIALIZED_LEN_MAX \
	 + MEM_SPACE_NEEDED(50L * sizeof(ulint)))

/* Calculates block size as an upper bound required for memory, used for
page_zip_compress and page_zip_decompress, and calls mem_block_cache_init
which initializes the mem_block_cache with it */
UNIV_INTERN
void
page_zip_init(void)
{
	ulint mem_max_decompress;
	ulint mem_max_compress =
		max(max(max(comp_mem_compress(DICT_TF_COMP_ZLIB),
			    comp_mem_compress(DICT_TF_COMP_BZIP)),
			max(comp_mem_compress(DICT_TF_COMP_LZMA),
			    comp_mem_compress(DICT_TF_COMP_SNAPPY))),
		    max(comp_mem_compress(DICT_TF_COMP_QUICKLZ),
			comp_mem_compress(DICT_TF_COMP_LZ4)));
	mem_max_compress = max(PZ_MEM_COMP_BASE
			       + PZ_MEM_SERIALIZE
			       + mem_max_compress,
			       PZ_MEM_COMP_ZLIB_STREAM);
	malloc_cache_compress = &malloc_cache_compress_obj;
	mem_block_cache_init(malloc_cache_compress, mem_max_compress,
	                     &malloc_cache_compress_len);
	mem_max_decompress =
		max(max(max(comp_mem_decompress(DICT_TF_COMP_ZLIB),
			    comp_mem_decompress(DICT_TF_COMP_BZIP)),
			max(comp_mem_decompress(DICT_TF_COMP_LZMA),
			    comp_mem_decompress(DICT_TF_COMP_SNAPPY))),
		    max(comp_mem_decompress(DICT_TF_COMP_QUICKLZ),
			comp_mem_decompress(DICT_TF_COMP_LZ4)));
	mem_max_decompress = max(PZ_MEM_COMP_BASE
				 + PZ_MEM_SERIALIZE
				 + mem_max_decompress,
				 PZ_MEM_DECOMP_ZLIB_STREAM);
	malloc_cache_decompress = &malloc_cache_decompress_obj;
	mem_block_cache_init(malloc_cache_decompress, mem_max_decompress,
	                     &malloc_cache_decompress_len);
}

/* Frees the malloc_cache_compress, and malloc_cache_decompress
mem_blocks, and all the cached mem_blocks in the queue. */
UNIV_INTERN
void
page_zip_close(void)
{
	mem_block_cache_free(malloc_cache_compress);
	mem_block_cache_free(malloc_cache_decompress);
}

UNIV_INTERN
void
page_zip_clean_garbage(
	const dict_index_t* index,
	page_t* page,
	const rec_t** recs,
	ulint n_dense,
	mem_heap_t* heap)
{
	const rec_t* rec;
	ulint* offsets = NULL;
	byte* rec_end = page + PAGE_ZIP_START;
	while (n_dense) {
		--n_dense;
		rec = *recs++;
		offsets = rec_get_offsets(rec, index, offsets, ULINT_UNDEFINED, &heap);
		memset(rec_end, 0, rec - rec_offs_extra_size(offsets) - rec_end);
		rec_end = (byte*) rec + rec_offs_data_size(offsets);
	}
	memset(rec_end,
	       0,
				 page + page_header_get_field(page, PAGE_HEAP_TOP) - rec_end);
}

my_bool page_zip_zlib_wrap = FALSE;
uint page_zip_zlib_strategy = Z_DEFAULT_STRATEGY;

/***************************************************************//**
Write the records on the page to the output buffer in a format in which
they can be recovered one by one after decompression. The uncompressed
fields of the record -if any- are stored in the compressed page trailer
not in the output buffer.
@return the buffer that has the serialization of records on the page
except for the uncompressed fields of each record.
*/
UNIV_INTERN
byte*
page_zip_serialize(
	page_zip_des_t* page_zip, /* out:the uncompressed fields are written
	to the trailer that is the end of page_zip->data in addition to the
	dense directory where the offsets of records are stored */
	const page_t* page, /* in: records are read from the page */
	dict_index_t* index, /* in: index information is used to extract records
	from the page */
	uint* serialized_len, /* out: the length of the output buffer */
	mem_heap_t* heap) /* in/out: heap used for memory allocation */
{
	ulint		n_fields;/* number of index fields needed */
	ulint		n_dense;
	const rec_t**	recs;	/*!< dense page directory, sorted by address */
	const rec_t* rec;
	ulint* offsets = NULL;
	ulint		trx_id_col;
	ulint rec_no;
	byte* trx_rbp_storage = NULL;
	byte* externs = NULL;
	byte* buf = static_cast<byte*>(
			mem_heap_alloc(heap, PZ_SERIALIZED_BUF_SIZE));
	byte* buf_ptr = buf;
	byte* buf_end = buf + PZ_SERIALIZED_LEN_MAX;
	ulint len;
#ifdef UNIV_DEBUG
	memset(buf, 0xDB, PZ_SERIALIZED_BUF_SIZE);
#endif
	if (page_is_leaf(page)) {
		n_fields = dict_index_get_n_fields(index);
	} else {
		n_fields = dict_index_get_n_unique_in_tree(index);
	}

	n_dense = page_zip_dir_elems(page_zip);

	trx_id_col = page_zip_get_trx_id_col_for_compression(page, index);
	if (trx_id_col) {
		/* leaf page of a primary key, store transaction id,
		   rollback pointer, and blob pointers */
		if (page_zip->compact_metadata) {
			page_zip_write_n_blob(page_zip, 0);
		}
		trx_rbp_storage = page_zip_get_trx_rbp_storage(page_zip, false);
		externs = page_zip_get_blob_ptr_storage(page_zip);
	}

	/* Check if there is enough space on the compressed page for the
	header, trailer and the compressed page image. */
	if (PAGE_DATA /* page header */
	    + PZ_COMPRESSED_LEN_MIN /* minimum reserved size for the compressed
				       output */
	    + 1 /* null byte for the end of the modification log */
	    + page_zip_get_trailer_len(page_zip, dict_index_is_clust(index))
	    /* trailer length */
	    > page_zip_get_size(page_zip)) {
		return NULL;
	}

	recs = static_cast<const rec_t**> (
			mem_heap_zalloc(heap, n_dense * sizeof *recs));

	/* Encode the index information in serialization stream */
	/* page_zip_fields_encode() encodes the index to the buffer. During
	   deserialization, we first want to know the length of the serialized
	   buffer. Therefore the first 3 bytes are reserved for the length
	   of the serialized buffer. We use three bytes to encode this length
	   in case we use page sizes larger than 64K in the future */
	len = page_zip_fields_encode(n_fields, index, trx_id_col, buf_ptr + 3);
	mach_write_to_3(buf_ptr, len);
	buf_ptr += len + 3;
	/* Encode n_dense in serialization stream */
	mach_write_to_2(buf_ptr, n_dense);
	buf_ptr += 2;
	ut_ad(buf_ptr < buf_end);
	/* Serialize the records in heap_no order. */
	page_zip_dir_encode(page_zip, page, recs);
	for (rec_no = 0; rec_no < n_dense; ++rec_no) {
		rec = recs[rec_no];
		ut_ad(rec_no + PAGE_HEAP_NO_USER_LOW
		      == rec_get_heap_no_new(rec));
		offsets = rec_get_offsets(rec, index, offsets,
					  ULINT_UNDEFINED, &heap);
#ifdef UNIV_DEBUG
		buf_ptr = page_zip_serialize_rec(FALSE, buf_ptr,
		                                 rec, offsets, trx_id_col);
#else
		buf_ptr = page_zip_serialize_rec(buf_ptr, rec,
						 offsets, trx_id_col);
#endif
		if (buf_ptr >= buf_end) {
			*serialized_len = 0;
			return NULL;
		}

		if (UNIV_UNLIKELY(trx_id_col == ULINT_UNDEFINED)) {
			/* node pointer page */
			/* store the pointers uncompressed */
			page_zip_trailer_write_node_ptr(page_zip, rec_no,
							rec, offsets);
		} else if (trx_id_col) {
			page_zip_store_blobs_for_rec(page_zip, index, rec_no,
						     rec, offsets, externs,
						     &trx_rbp_storage);
			page_zip_store_trx_rbp(trx_rbp_storage, rec, offsets,
					       rec_no, trx_id_col);
			page_zip_size_check(page_zip, index);
		}
	}
	*serialized_len = buf_ptr - buf;
	return buf;
}

/**********************************************************************//**
Compress a page.
@return TRUE on success, FALSE on failure; page_zip will be left
intact on failure. */
UNIV_INTERN
ibool
page_zip_compress_zlib_stream(
/*==============*/
	page_zip_des_t*	page_zip,/*!< in: size; out: data, n_blobs,
				m_start, m_end, m_nonempty */
	const page_t*	page,	/*!< in: uncompressed page */
	dict_index_t*	index,	/*!< in: index of the B-tree node */
	uchar		compression_flags,	/*!< in: compression level
						         and other options */
	mem_heap_t*	heap)	/*< in/out: heap used for memory allocation */
{
	z_stream	c_stream;
	int		err;
	ulint		n_fields;/* number of index fields needed */
	byte*		fields;	/*!< index field information */
	ulint		n_dense;
	ulint		slot_size;/* amount of uncompressed bytes per record */
	const rec_t**	recs;	/*!< dense page directory, sorted by address */
	ulint		trx_id_col;
#ifndef UNIV_HOTBACKUP
	uint level;
	uint wrap;
	uint strategy;
	int window_bits;

#ifdef UNIV_DEBUG
	page_t* temp_page;
#endif
	page_zip_decode_compression_flags(compression_flags, &level,
					  &wrap, &strategy);
	window_bits = wrap ? UNIV_PAGE_SIZE_SHIFT
			   : - ((int) UNIV_PAGE_SIZE_SHIFT);
#endif /* !UNIV_HOTBACKUP */
#ifdef UNIV_DEBUG
	FILE*		logfile = NULL;
#endif

	if (page_is_leaf(page)) {
		n_fields = dict_index_get_n_fields(index);
	} else {
		n_fields = dict_index_get_n_unique_in_tree(index);
	}

	/* The dense directory excludes the infimum and supremum records. */
	n_dense = page_dir_get_n_heap(page) - PAGE_HEAP_NO_USER_LOW;
#ifdef UNIV_DEBUG
	if (UNIV_UNLIKELY(page_zip_compress_dbg)) {
		fprintf(stderr, "compress %p %p %lu %lu %lu\n",
			(void*) page_zip, (void*) page,
			(ibool) page_is_leaf(page),
			n_fields, n_dense);
	}
	if (UNIV_UNLIKELY(page_zip_compress_log)) {
		/* Create a log file for every compression attempt. */
		char	logfilename[9];
		ut_snprintf(logfilename, sizeof logfilename,
			    "%08x", page_zip_compress_log++);
		logfile = fopen(logfilename, "wb");

		if (logfile) {
			/* Write the uncompressed page to the log. */
			blind_fwrite(page, 1, UNIV_PAGE_SIZE, logfile);
			/* Record the compressed size as zero.
			This will be overwritten at successful exit. */
			putc(0, logfile);
			putc(0, logfile);
			putc(0, logfile);
			putc(0, logfile);
		}
	}
#endif /* UNIV_DEBUG */

	recs = static_cast<const rec_t**>(
			mem_heap_zalloc(heap, n_dense * sizeof *recs));

	fields = static_cast<byte*>(mem_heap_alloc(heap, (n_fields + 1) * 2));

	/* Compress the data payload. */
	page_zip_set_alloc(&c_stream, heap);

	err = deflateInit2(&c_stream, static_cast<int>(level),
			   Z_DEFLATED, window_bits,
			   MAX_MEM_LEVEL, strategy);
	ut_a(err == Z_OK);

	c_stream.next_out = page_zip->data + PAGE_DATA;
	/* Subtract the space reserved for uncompressed data. */
	/* Page header and the end marker of the modification log */
	c_stream.avail_out = page_zip_get_size(page_zip) - PAGE_DATA - 1;

	/* Dense page directory and uncompressed columns, if any */
	trx_id_col = page_zip_get_trx_id_col_for_compression(page, index);

	if (page_is_leaf(page)) {
		if (dict_index_is_clust(index)) {
			slot_size = PAGE_ZIP_DIR_SLOT_SIZE + DATA_TRX_RBP_LEN;
		} else {
			slot_size = PAGE_ZIP_DIR_SLOT_SIZE;
		}
	} else {
		slot_size = PAGE_ZIP_DIR_SLOT_SIZE + REC_NODE_PTR_SIZE;
	}

	if (UNIV_UNLIKELY(c_stream.avail_out <= n_dense * slot_size
			  + 6/* sizeof(zlib header and footer) */)) {
		goto zlib_error;
	}

	c_stream.avail_out -= static_cast<uInt>(n_dense * slot_size);
	c_stream.avail_in = static_cast<uInt>(
		page_zip_fields_encode(n_fields, index, trx_id_col, fields));
	c_stream.next_in = fields;
	if (UNIV_LIKELY(!trx_id_col)) {
		trx_id_col = ULINT_UNDEFINED;
	}

	UNIV_MEM_ASSERT_RW(c_stream.next_in, c_stream.avail_in);
	err = deflate(&c_stream, Z_FULL_FLUSH);
	if (err != Z_OK) {
		goto zlib_error;
	}

	ut_ad(!c_stream.avail_in);

#ifdef UNIV_DEBUG
	temp_page = static_cast <ib_page_t*> (mem_heap_zalloc(heap, 2 * UNIV_PAGE_SIZE));
	temp_page = page_align(temp_page + UNIV_PAGE_SIZE);
	memcpy(temp_page, page, UNIV_PAGE_SIZE);
	page_zip_dir_encode(page_zip, temp_page, recs);
	page_zip_clean_garbage(index, temp_page, recs, n_dense, heap);
	c_stream.next_in = (byte*) temp_page + PAGE_ZIP_START;
#else
	page_zip_dir_encode(page_zip, page, recs);
	c_stream.next_in = (byte*) page + PAGE_ZIP_START;
#endif

	/* Compress the records in heap_no order. */
	if (UNIV_UNLIKELY(!n_dense)) {
	} else if (!page_is_leaf(page)) {
		/* This is a node pointer page. */
		err = page_zip_compress_node_ptrs(LOGFILE
						  page_zip, &c_stream,
						  recs, index, heap);
		if (UNIV_UNLIKELY(err != Z_OK)) {
			goto zlib_error;
		}
	} else if (UNIV_LIKELY(trx_id_col == ULINT_UNDEFINED)) {
		/* This is a leaf page in a secondary index. */
		err = page_zip_compress_sec(LOGFILE
					    &c_stream, recs, n_dense, index, heap);
		if (UNIV_UNLIKELY(err != Z_OK)) {
			goto zlib_error;
		}
	} else {
		/* This is a leaf page in a clustered index. */
		err = page_zip_compress_clust(LOGFILE
					      page_zip, &c_stream,
					      recs, index, heap);
		if (UNIV_UNLIKELY(err != Z_OK)) {
			goto zlib_error;
		}
	}

	/* Finish the compression. */
	ut_ad(!c_stream.avail_in);
	/* Compress any trailing garbage, in case the last record was
	allocated from an originally longer space on the free list,
	or the data of the last record from page_zip_compress_sec(). */
#ifdef UNIV_DEBUG
	c_stream.avail_in = page_header_get_field(page, PAGE_HEAP_TOP)
			    - (c_stream.next_in - temp_page);
#else
	c_stream.avail_in = page_header_get_field(page, PAGE_HEAP_TOP)
			    - (c_stream.next_in - page);
#endif

	ut_a(c_stream.avail_in <= UNIV_PAGE_SIZE - PAGE_ZIP_START - PAGE_DIR);

	UNIV_MEM_ASSERT_RW(c_stream.next_in, c_stream.avail_in);
	err = deflate(&c_stream, Z_FINISH);

	if (UNIV_UNLIKELY(err != Z_STREAM_END)) {
zlib_error:
		deflateEnd(&c_stream);
#ifdef UNIV_DEBUG
		if (logfile) {
			fclose(logfile);
		}
#endif /* UNIV_DEBUG */
		return FALSE;
	}
	err = deflateEnd(&c_stream);
	ut_a(err == Z_OK);

	ut_ad(page_zip->data + PAGE_DATA + c_stream.total_out
	      == c_stream.next_out);
	ut_ad((ulint) (page_zip_dir_start(page_zip)
		       - c_stream.next_out) >= c_stream.avail_out);

	/* Valgrind believes that zlib does not initialize some bits
	in the last 7 or 8 bytes of the stream. Make Valgrind happy. */
	UNIV_MEM_VALID(page_zip->data + PAGE_DATA, c_stream.total_out);

#ifdef UNIV_DEBUG
	page_zip->m_start =
#endif /* UNIV_DEBUG */
	page_zip->m_end = PAGE_DATA + c_stream.total_out;
	page_zip->m_nonempty = FALSE;

#ifdef UNIV_DEBUG
	if (logfile) {
		/* Record the compressed size of the block. */
		byte sz[4];
		mach_write_to_4(sz, c_stream.total_out);
		fseek(logfile, UNIV_PAGE_SIZE, SEEK_SET);
		blind_fwrite(sz, 1, sizeof sz, logfile);
		fclose(logfile);
	}
#endif /* UNIV_DEBUG */

	return(TRUE);
}

/**********************************************************************//**
Compress a page.
@return TRUE on success, FALSE on failure; page_zip will be left
intact on failure. */
UNIV_INTERN
ibool
page_zip_compress(
/*==============*/
	page_zip_des_t* page_zip,/*!< in: size; out: data, n_blobs,
	m_start, m_end, m_nonempty */
	const page_t* page, /*!< in: uncompressed page */
	dict_index_t* index, /*!< in: index of the B-tree node */
	uchar global_compression_flags, /*!< in: zlib compression level and
	other options */
	mtr_t* mtr) /*!< in: mini-transaction, or NULL */
{
	page_zip_des_t new_page_zip;
	ulint fsp_flags;
	ibool ret;
	mem_heap_t* heap = NULL;
	uchar comp_level;
	uchar comp_type;
	ulint n_dense;
	uint serialized_len = 0;
	ulint compressed_len = 0;
	ulint trailer_len;
	lint avail_out = 0;
	byte* buf;
#ifndef UNIV_HOTBACKUP
	ulonglong time_diff;
	page_zip_stat_t* zip_stat = &page_zip_stat[page_zip->ssize - 1];
	ulonglong start = my_timer_now();
	ulint space_id = page_get_space_id(page);
#endif /* !UNIV_HOTBACKUP */

	/* A local copy of srv_cmp_per_index_enabled to avoid reading that
	variable multiple times in this function since it can be changed at
	anytime. */
	my_bool	cmp_per_index_enabled = srv_cmp_per_index_enabled;

	fsp_flags = fil_get_fsp_flags(space_id, ULINT_UNDEFINED);
	memset(&new_page_zip, 0, sizeof(new_page_zip));
	new_page_zip.ssize = page_zip->ssize;

	ut_a(page_is_comp(page));
	ut_a(fil_page_get_type(page) == FIL_PAGE_INDEX);
	ut_ad(page_simple_validate_new((page_t*) page));
	ut_ad(page_zip_simple_validate(page_zip));
	ut_ad(dict_table_is_comp(index->table));
	ut_ad(!dict_index_is_ibuf(index));

	UNIV_MEM_ASSERT_RW(page, UNIV_PAGE_SIZE);

	/* Check the data that will be omitted. */
	ut_a(!memcmp(page + (PAGE_NEW_INFIMUM - REC_N_NEW_EXTRA_BYTES),
		     infimum_extra, sizeof infimum_extra));
	ut_a(!memcmp(page + PAGE_NEW_INFIMUM,
		     infimum_data, sizeof infimum_data));
	ut_a(page[PAGE_NEW_SUPREMUM - REC_N_NEW_EXTRA_BYTES]
	     /* info_bits == 0, n_owned <= max */
	     <= PAGE_DIR_SLOT_MAX_N_OWNED);
	ut_a(!memcmp(page + (PAGE_NEW_SUPREMUM - REC_N_NEW_EXTRA_BYTES + 1),
		     supremum_extra_data, sizeof supremum_extra_data));

	if (page_is_empty(page)) {
		ut_a(rec_get_next_offs(page + PAGE_NEW_INFIMUM, TRUE)
		     == PAGE_NEW_SUPREMUM);
	}

	/* The dense directory excludes the infimum and supremum records. */
	n_dense = page_dir_get_n_heap(page) - PAGE_HEAP_NO_USER_LOW;

	if (UNIV_UNLIKELY(n_dense * PAGE_ZIP_DIR_SLOT_SIZE
			  >= page_zip_get_size(page_zip))) {

		goto err_exit;
	}

	MONITOR_INC(MONITOR_PAGE_COMPRESS);

	/* Simulate a compression failure with a probability determined by
	innodb_simulate_comp_failures, only if the page has 2 or more
	records. */
	if (srv_simulate_comp_failures
	    && zip_failure_threshold_pct == 0
	    && page_get_n_recs(page) >= 2
	    && ((ulint)(rand() % 100) < srv_simulate_comp_failures)) {
#ifdef UNIV_DEBUG
		fprintf(stderr,
			"InnoDB: Simulating a compression failure"
			" for table %s, index %s, page %lu (%s)\n",
			index->table_name,
			index->name,
			page_get_page_no(page),
			page_is_leaf(page) ? "leaf" : "non-leaf");
#endif
		goto err_exit;
	}

	/* asking a heap of size 0 will just give a memory block
	   with the default block size which is OK */
	heap = mem_heap_create_cached(0, malloc_cache_compress);
	comp_level = FSP_FLAGS_GET_COMP_LEVEL(fsp_flags);
	comp_type = FSP_FLAGS_GET_COMP_TYPE(fsp_flags);
	new_page_zip.data = static_cast<byte*> (
				mem_heap_alloc(
					heap, page_zip_get_size(page_zip)));

	/* UNIV_MEM_VALID() is needed here because we need to compute the
	trailer length here but page_zip_get_trailer_len() requires the
	entirety of the compressed page be initialized even though it does
	not use it. */
	UNIV_MEM_VALID(new_page_zip.data, page_zip_get_size(page_zip));
	ut_d(memset(new_page_zip.data, 0x85, page_zip_get_size(page_zip)));

	memcpy(new_page_zip.data, page, PAGE_DATA);
	page_zip->compact_metadata = new_page_zip.compact_metadata
				   = FSP_FLAGS_GET_COMPACT_METADATA(fsp_flags);

	if (comp_type == DICT_TF_COMP_ZLIB_STREAM) {
		ret = page_zip_compress_zlib_stream(
			&new_page_zip, page, index,
			global_compression_flags, heap);
		trailer_len = page_zip_get_trailer_len(
				&new_page_zip, dict_index_is_clust(index));
	} else {
		comp_state_t comp_state;
		buf = page_zip_serialize(&new_page_zip, page, index,
					 &serialized_len, heap);
		if (!buf) {
			ut_a(!serialized_len);
			goto err_exit;
		}

		trailer_len = page_zip_get_trailer_len(
				&new_page_zip, dict_index_is_clust(index));
		avail_out = page_zip_get_size(&new_page_zip)
			    - PAGE_DATA /* page header */
			    - trailer_len /* compressed page trailer */
			    - 1; /* end marker for the modification log */
		if (avail_out <= 0)
			goto err_exit;
		comp_state.in = buf;
		comp_state.out = new_page_zip.data + PAGE_DATA;
		comp_state.avail_in = serialized_len;
		comp_state.avail_out = avail_out;
		comp_state.level = comp_level;
		comp_state.heap = heap;
		ret = comp_compress(comp_type, &comp_state);
		if (ret) {
			ut_a(avail_out > (lint)comp_state.avail_out);
			compressed_len = avail_out - comp_state.avail_out;
			new_page_zip.m_nonempty = FALSE;
			new_page_zip.m_end = PAGE_DATA + compressed_len;
		}
	}

	if (UNIV_UNLIKELY(!ret)) {
err_exit:
		if (heap)
			mem_heap_free(heap);
#ifndef UNIV_HOTBACKUP
		if (page_is_leaf(page)) {
			dict_index_zip_failure(index);
		}

		time_diff = my_timer_since(start);
		page_zip_update_zip_stats_compress(zip_stat, time_diff, false,
						   dict_index_is_clust(index));

		page_zip_update_fil_comp_stats_compress(
			space_id, page_zip_get_size(page_zip),
			time_diff, false, dict_index_is_clust(index),
			UNIV_PAGE_SIZE
			- dict_index_zip_pad_optimal_page_size(index));

		if (cmp_per_index_enabled) {
			page_zip_update_per_index_stats_compress(
				index->id, time_diff, false);
		}
#endif /* !UNIV_HOTBACKUP */
		return(FALSE);
	}


#ifdef UNIV_DEBUG
	page_zip->m_start =
#endif /* UNIV_DEBUG */
	page_zip->m_end = new_page_zip.m_end;
	page_zip->m_nonempty = FALSE;
	page_zip->n_blobs = new_page_zip.n_blobs;
	/* Copy those header fields that will not be written
	in buf_flush_init_for_writing() */
	memcpy(page_zip->data + FIL_PAGE_PREV, page + FIL_PAGE_PREV,
	       FIL_PAGE_LSN - FIL_PAGE_PREV);
	memcpy(page_zip->data + FIL_PAGE_TYPE, page + FIL_PAGE_TYPE, 2);
	memcpy(page_zip->data + FIL_PAGE_DATA, page + FIL_PAGE_DATA,
	       PAGE_DATA - FIL_PAGE_DATA);
	/* Copy the rest of the compressed page */
	memcpy(page_zip->data + PAGE_DATA, new_page_zip.data + PAGE_DATA,
	       page_zip_get_size(page_zip) - PAGE_DATA);
	/* Done with new_page_zip.data, free the heap */
	ut_ad(heap);
	mem_heap_free(heap);

	/* Zero out the area reserved for the modification log. */
	memset(page_zip->data + page_zip->m_end,
	       0,
	       page_zip_get_size(page_zip) - page_zip->m_end - trailer_len);

	if (UNIV_UNLIKELY(page_zip_debug)) {
		ut_a(page_zip_validate(page_zip, page, index));
	}

	if (mtr) {
#ifndef UNIV_HOTBACKUP
		page_zip_compress_write_log(page_zip, page, index, mtr);
#endif /* !UNIV_HOTBACKUP */
	}

	UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));

#ifndef UNIV_HOTBACKUP
	time_diff = my_timer_since(start);
	page_zip_update_zip_stats_compress(zip_stat, time_diff, true,
					   dict_index_is_clust(index));
	page_zip_update_fil_comp_stats_compress(
		space_id, page_zip_get_size(page_zip),
		time_diff, true, dict_index_is_clust(index),
		UNIV_PAGE_SIZE - dict_index_zip_pad_optimal_page_size(index));

	if (cmp_per_index_enabled) {
		page_zip_update_per_index_stats_compress(
			index->id, time_diff, true);
	}

	if (page_is_leaf(page)) {
		dict_index_zip_success(index);
	}
#endif /* !UNIV_HOTBACKUP */

	return(TRUE);
}

/**********************************************************************//**
Initialize the REC_N_NEW_EXTRA_BYTES of each record.
@return	TRUE on success, FALSE on failure */
static
ibool
page_zip_set_extra_bytes(
/*=====================*/
	const page_zip_des_t*	page_zip,/*!< in: compressed page */
	page_t*			page,	/*!< in/out: uncompressed page */
	ulint			info_bits)/*!< in: REC_INFO_MIN_REC_FLAG or 0 */
{
	ulint	n;
	ulint	i;
	ulint	n_owned = 1;
	ulint	offs;
	rec_t*	rec;

	n = page_get_n_recs(page);
	rec = page + PAGE_NEW_INFIMUM;

	for (i = 0; i < n; i++) {
		offs = page_zip_dir_get(page_zip, i);

		if (offs & PAGE_ZIP_DIR_SLOT_DEL) {
			info_bits |= REC_INFO_DELETED_FLAG;
		}
		if (UNIV_UNLIKELY(offs & PAGE_ZIP_DIR_SLOT_OWNED)) {
			info_bits |= n_owned;
			n_owned = 1;
		} else {
			n_owned++;
		}
		offs &= PAGE_ZIP_DIR_SLOT_MASK;
		if (UNIV_UNLIKELY(offs < PAGE_ZIP_START
				  + REC_N_NEW_EXTRA_BYTES)) {
			page_zip_fail("page_zip_set_extra_bytes 1:"
				       " %u %u %lx\n",
				       (unsigned) i, (unsigned) n,
				       (ulong) offs);
			return(FALSE);
		}

		rec_set_next_offs_new(rec, offs);
		rec = page + offs;
		rec[-REC_N_NEW_EXTRA_BYTES] = (byte) info_bits;
		info_bits = 0;
	}

	/* Set the next pointer of the last user record. */
	rec_set_next_offs_new(rec, PAGE_NEW_SUPREMUM);

	/* Set n_owned of the supremum record. */
	page[PAGE_NEW_SUPREMUM - REC_N_NEW_EXTRA_BYTES] = (byte) n_owned;

	/* The dense directory excludes the infimum and supremum records. */
	n = page_dir_get_n_heap(page) - PAGE_HEAP_NO_USER_LOW;

	if (i >= n) {
		if (UNIV_LIKELY(i == n)) {
			return(TRUE);
		}

		page_zip_fail("page_zip_set_extra_bytes 2: %u != %u\n",
			       (unsigned) i, (unsigned) n);
		return(FALSE);
	}

	offs = page_zip_dir_get(page_zip, i);

	/* Set the extra bytes of deleted records on the free list. */
	for (;;) {
		if (UNIV_UNLIKELY(!offs)
		    || UNIV_UNLIKELY(offs & ~PAGE_ZIP_DIR_SLOT_MASK)) {

			page_zip_fail("page_zip_set_extra_bytes 3: %lx\n",
				       (ulong) offs);
			return(FALSE);
		}

		rec = page + offs;
		rec[-REC_N_NEW_EXTRA_BYTES] = 0; /* info_bits and n_owned */

		if (++i == n) {
			break;
		}

		offs = page_zip_dir_get(page_zip, i);
		rec_set_next_offs_new(rec, offs);
	}

	/* Terminate the free list. */
	rec[-REC_N_NEW_EXTRA_BYTES] = 0; /* info_bits and n_owned */
	rec_set_next_offs_new(rec, 0);

	return(TRUE);
}

/**********************************************************************//**
Apply the modification log to an uncompressed page.
Do not copy the fields that are stored separately.
@return	pointer to end of modification log, or NULL on failure */
static
const byte*
page_zip_apply_log(
/*===============*/
	const byte*	data,	/*!< in: modification log */
	ulint		size,	/*!< in: maximum length of the log, in bytes */
	rec_t**		recs,	/*!< in: dense page directory,
				sorted by address (indexed by
				heap_no - PAGE_HEAP_NO_USER_LOW) */
	ulint		n_dense,/*!< in: size of recs[] */
	ulint		trx_id_col,/*!< in: column number of trx_id in the index,
				or ULINT_UNDEFINED if none */
	ulint		heap_status,
				/*!< in: heap_no and status bits for
				the next record to uncompress */
	dict_index_t*	index,	/*!< in: index of the page */
	ulint*		offsets)/*!< in/out: work area for
				rec_get_offsets_reverse() */
{
	const byte* const end = data + size;

	for (;;) {
		ulint	val;
		rec_t*	rec;
		ulint	hs;

		val = *data++;
		if (UNIV_UNLIKELY(!val)) {
			return(data - 1);
		}
		if (val & 0x80) {
			val = (val & 0x7f) << 8 | *data++;
			if (UNIV_UNLIKELY(!val)) {
				page_zip_fail("page_zip_apply_log:"
					       " invalid val %x%x\n",
					       data[-2], data[-1]);
				return(NULL);
			}
		}
		if (UNIV_UNLIKELY(data >= end)) {
			page_zip_fail("page_zip_apply_log: %p >= %p\n",
				       (const void*) data,
				       (const void*) end);
			return(NULL);
		}
		if (UNIV_UNLIKELY((val >> 1) > n_dense)) {
			page_zip_fail("page_zip_apply_log: %lu>>1 > %lu\n",
				       (ulong) val, (ulong) n_dense);
			return(NULL);
		}

		/* Determine the heap number and status bits of the record. */
		rec = recs[(val >> 1) - 1];

		hs = ((val >> 1) + 1) << REC_HEAP_NO_SHIFT;
		hs |= heap_status & ((1 << REC_HEAP_NO_SHIFT) - 1);

		/* This may either be an old record that is being
		overwritten (updated in place, or allocated from
		the free list), or a new record, with the next
		available_heap_no. */
		if (UNIV_UNLIKELY(hs > heap_status)) {
			page_zip_fail("page_zip_apply_log: %lu > %lu\n",
				       (ulong) hs, (ulong) heap_status);
			return(NULL);
		} else if (hs == heap_status) {
			/* A new record was allocated from the heap. */
			if (UNIV_UNLIKELY(val & 1)) {
				/* Only existing records may be cleared. */
				page_zip_fail("page_zip_apply_log:"
					       " attempting to create"
					       " deleted rec %lu\n",
					       (ulong) hs);
				return(NULL);
			}
			heap_status += 1 << REC_HEAP_NO_SHIFT;
		}

		mach_write_to_2(rec - REC_NEW_HEAP_NO, hs);

		if (val & 1) {
			/* Clear the data bytes of the record. */
			mem_heap_t*	heap	= NULL;
			ulint*		offs;
			offs = rec_get_offsets(rec, index, offsets,
					       ULINT_UNDEFINED, &heap);
			memset(rec, 0, rec_offs_data_size(offs));

			if (UNIV_LIKELY_NULL(heap)) {
				mem_heap_free(heap);
			}
			continue;
		}

#if REC_STATUS_NODE_PTR != TRUE
# error "REC_STATUS_NODE_PTR != TRUE"
#endif
		data = page_zip_deserialize_rec(data,
						rec,
						index,
						offsets,
						trx_id_col,
						hs,
						end);
	}
}

/**********************************************************************//**
Set the heap_no in a record, and skip the fixed-size record header
that is not included in the d_stream.
@return	TRUE on success, FALSE if d_stream does not end at rec */
static
ibool
page_zip_decompress_heap_no(
/*========================*/
	z_stream*	d_stream,	/*!< in/out: compressed page stream */
	rec_t*		rec,		/*!< in/out: record */
	ulint&		heap_status)	/*!< in/out: heap_no and status bits */
{
	if (d_stream->next_out != rec - REC_N_NEW_EXTRA_BYTES) {
		/* n_dense has grown since the page was last compressed. */
		return(FALSE);
	}

	/* Skip the REC_N_NEW_EXTRA_BYTES. */
	d_stream->next_out = rec;

	/* Set heap_no and the status bits. */
	mach_write_to_2(rec - REC_NEW_HEAP_NO, heap_status);
	heap_status += 1 << REC_HEAP_NO_SHIFT;
	return(TRUE);
}

/**********************************************************************//**
Decompress the trailing garbage for a page. Trailing garbage is compressed
to make sure that the compressed page is the same during crash recovery. */
UNIV_INLINE
void
page_zip_decompress_trailing_garbage(
	const page_zip_des_t*	page_zip,	/*!< in:
						compressed page */
	z_stream*		d_stream)	/*!< in/out: decompression
						stream */
{
	/* Decompress any trailing garbage, in case the last record was
	allocated from an originally longer space on the free list. */
	d_stream->avail_out = page_header_get_field(page_zip->data,
						    PAGE_HEAP_TOP)
			      - page_offset(d_stream->next_out);
	if (UNIV_UNLIKELY(d_stream->avail_out > UNIV_PAGE_SIZE
						- PAGE_ZIP_START - PAGE_DIR)) {
		page_zip_fail("page_zip_decompress_trailing_garbage: "
			       "avail_out = %u\n", d_stream->avail_out);
		ut_error;
	}
}

/**********************************************************************//**
Decompress the records of a node pointer page.
@return	TRUE on success, FALSE on failure */
static
ibool
page_zip_decompress_node_ptrs(
/*==========================*/
	page_zip_des_t*	page_zip,	/*!< in/out: compressed page */
	z_stream*	d_stream,	/*!< in/out: compressed page stream */
	rec_t**		recs,		/*!< in: dense page directory
					sorted by address */
	ulint		n_dense,	/*!< in: size of recs[] */
	dict_index_t*	index,		/*!< in: the index of the page */
	ulint*		offsets,	/*!< in/out: temporary offsets */
	ulint*		heap_status_ptr,	/*!< out: pointer to the
						integer where we will store
						the heap status of the last
						record */
	mem_heap_t*	heap)		/*!< in: temporary memory heap */
{
	ulint		heap_status = REC_STATUS_NODE_PTR
		| PAGE_HEAP_NO_USER_LOW << REC_HEAP_NO_SHIFT;
	ulint		slot;

	/* Subtract the space reserved for uncompressed data. */
	d_stream->avail_in -= static_cast<uInt>(
		n_dense * (PAGE_ZIP_DIR_SLOT_SIZE + REC_NODE_PTR_SIZE));

	/* Decompress the records in heap_no order. */
	for (slot = 0; slot < n_dense; slot++) {
		rec_t*	rec = recs[slot];

		d_stream->avail_out = static_cast<uInt>(
			rec - REC_N_NEW_EXTRA_BYTES - d_stream->next_out);

		ut_ad(d_stream->avail_out < UNIV_PAGE_SIZE
		      - PAGE_ZIP_START - PAGE_DIR);
		switch (inflate(d_stream, Z_SYNC_FLUSH)) {
		case Z_STREAM_END:
			page_zip_decompress_heap_no(
				d_stream, rec, heap_status);
			goto zlib_done;
		case Z_OK:
		case Z_BUF_ERROR:
			if (!d_stream->avail_out) {
				break;
			}
			/* fall through */
		default:
			page_zip_fail("page_zip_decompress_node_ptrs:"
				       " 1 inflate(Z_SYNC_FLUSH)=%s\n",
				       d_stream->msg);
			goto zlib_error;
		}

		if (!page_zip_decompress_heap_no(
			    d_stream, rec, heap_status)) {
			ut_ad(0);
		}

		/* Read the offsets. The status bits are needed here. */
		offsets = rec_get_offsets(rec, index, offsets,
					  ULINT_UNDEFINED, &heap);

		/* Non-leaf nodes should not have any externally
		stored columns. */
		ut_ad(!rec_offs_any_extern(offsets));

		/* Decompress the data bytes, except node_ptr. */
		d_stream->avail_out =static_cast<uInt>(
			rec_offs_data_size(offsets) - REC_NODE_PTR_SIZE);

		switch (inflate(d_stream, Z_SYNC_FLUSH)) {
		case Z_STREAM_END:
			goto zlib_done;
		case Z_OK:
		case Z_BUF_ERROR:
			if (!d_stream->avail_out) {
				break;
			}
			/* fall through */
		default:
			page_zip_fail("page_zip_decompress_node_ptrs:"
				       " 2 inflate(Z_SYNC_FLUSH)=%s\n",
				       d_stream->msg);
			goto zlib_error;
		}

		/* Clear the node pointer in case the record
		will be deleted and the space will be reallocated
		to a smaller record. */
		memset(d_stream->next_out, 0, REC_NODE_PTR_SIZE);
		d_stream->next_out += REC_NODE_PTR_SIZE;

		ut_ad(d_stream->next_out == rec_get_end(rec, offsets));
	}

	page_zip_decompress_trailing_garbage(page_zip, d_stream);

	if (UNIV_UNLIKELY(inflate(d_stream, Z_FINISH) != Z_STREAM_END)) {
		page_zip_fail("page_zip_decompress_node_ptrs: "
			       "inflate(Z_FINISH) = %s\n", d_stream->msg);
zlib_error:
		inflateEnd(d_stream);
		return(FALSE);
	}

	/* Note that d_stream->avail_out > 0 may hold here
	if the modification log is nonempty. */

zlib_done:
	if (UNIV_UNLIKELY(inflateEnd(d_stream) != Z_OK)) {
		ut_error;
	}

	*heap_status_ptr = heap_status;
	return(TRUE);
}

/**********************************************************************//**
Decompress the records of a leaf node of a secondary index.
@return	TRUE on success, FALSE on failure */
static
ibool
page_zip_decompress_sec(
/*====================*/
	page_zip_des_t*	page_zip,	/*!< in/out: compressed page */
	z_stream*	d_stream,	/*!< in/out: compressed page stream */
	rec_t**		recs,		/*!< in: dense page directory
					sorted by address */
	ulint		n_dense,	/*!< in: size of recs[] */
	dict_index_t*	index,		/*!< in: the index of the page */
	ulint*		heap_status_ptr)	/*!< out: pointer to the
						integer where we will store the
						heap status of the last
						record */
{
	ulint	heap_status	= REC_STATUS_ORDINARY
		| PAGE_HEAP_NO_USER_LOW << REC_HEAP_NO_SHIFT;
	ulint	slot;

	ut_a(!dict_index_is_clust(index));

	/* Subtract the space reserved for uncompressed data. */
	d_stream->avail_in -= static_cast<uint>(
		n_dense * PAGE_ZIP_DIR_SLOT_SIZE);

	for (slot = 0; slot < n_dense; slot++) {
		rec_t*	rec = recs[slot];

		/* Decompress everything up to this record. */
		d_stream->avail_out = static_cast<uint>(
			rec - REC_N_NEW_EXTRA_BYTES - d_stream->next_out);

		if (UNIV_LIKELY(d_stream->avail_out)) {
			switch (inflate(d_stream, Z_SYNC_FLUSH)) {
			case Z_STREAM_END:
				page_zip_decompress_heap_no(
					d_stream, rec, heap_status);
				goto zlib_done;
			case Z_OK:
			case Z_BUF_ERROR:
				if (!d_stream->avail_out) {
					break;
				}
				/* fall through */
			default:
				page_zip_fail("page_zip_decompress_sec:"
					       " inflate(Z_SYNC_FLUSH)=%s\n",
					       d_stream->msg);
				goto zlib_error;
			}
		}

		if (!page_zip_decompress_heap_no(
			    d_stream, rec, heap_status)) {
			ut_ad(0);
		}
	}

	page_zip_decompress_trailing_garbage(page_zip, d_stream);

	if (UNIV_UNLIKELY(inflate(d_stream, Z_FINISH) != Z_STREAM_END)) {
		page_zip_fail("page_zip_decompress_sec: "
			       " inflate(Z_FINISH) = %s\n",
			       d_stream->msg);
zlib_error:
		inflateEnd(d_stream);
		return(FALSE);
	}

	/* Note that d_stream->avail_out > 0 may hold here
	if the modification log is nonempty. */

zlib_done:
	if (UNIV_UNLIKELY(inflateEnd(d_stream) != Z_OK)) {
		ut_error;
	}

	*heap_status_ptr = heap_status;
	return(TRUE);
}

/**********************************************************************//**
Decompress a record of a leaf node of a clustered index that contains
externally stored columns. */
static
void
page_zip_decompress_clust_ext_rec(
/*==========================*/
	z_stream*	d_stream,	/*!< in/out: compressed page stream */
	rec_t*		rec,		/*!< in/out: record */
	const ulint*	offsets,	/*!< in: rec_get_offsets(rec) */
	ulint		trx_id_col)	/*!< in: position of of DB_TRX_ID */
{
	ulint	i;

	for (i = 0; i < rec_offs_n_fields(offsets); i++) {
		ulint	len;
		byte*	dst;

		if (UNIV_UNLIKELY(i == trx_id_col)) {
			/* Skip trx_id and roll_ptr */
			dst = rec_get_nth_field(rec, offsets, i, &len);
			if (UNIV_UNLIKELY(len < DATA_TRX_RBP_LEN)) {

				page_zip_fail("page_zip_decompress_clust_ext_rec:1"
					       " len[%lu] = %lu\n",
					       (ulong) i, (ulong) len);
				ut_error;
			}

			if (rec_offs_nth_extern(offsets, i)) {

				page_zip_fail("page_zip_decompress_clust_ext_rec:2"
					       " DB_TRX_ID at %lu is ext\n",
					       (ulong) i);
				ut_error;
			}

			d_stream->avail_out = static_cast<uInt>(
				dst - d_stream->next_out);

			switch (inflate(d_stream, Z_SYNC_FLUSH)) {
			case Z_STREAM_END:
			case Z_OK:
			case Z_BUF_ERROR:
				if (UNIV_UNLIKELY(d_stream->avail_out)) {
					page_zip_fail("page_zip_decompress_clust_ext_rec:3"
									" inflate(Z_SYNC_FLUSH)=%s\n",
									d_stream->msg);
					ut_error;
				}
				break;
			default:
				page_zip_fail("page_zip_decompress_clust_ext_rec:4"
					       " inflate(Z_SYNC_FLUSH)=%s\n",
					       d_stream->msg);
				ut_error;
				break;
			}

			ut_ad(d_stream->next_out == dst);

			/* Clear DB_TRX_ID and DB_ROLL_PTR in order to
			avoid uninitialized bytes in case the record
			is affected by page_zip_apply_log(). */
			memset(dst, 0, DATA_TRX_RBP_LEN);

			d_stream->next_out += DATA_TRX_RBP_LEN;

		} else if (rec_offs_nth_extern(offsets, i)) {
			dst = rec_get_nth_field(rec, offsets, i, &len);
			ut_ad(len >= BTR_EXTERN_FIELD_REF_SIZE);
			dst += len - BTR_EXTERN_FIELD_REF_SIZE;

			d_stream->avail_out = static_cast<uInt>(
				dst - d_stream->next_out);
			switch (inflate(d_stream, Z_SYNC_FLUSH)) {
			case Z_STREAM_END:
			case Z_OK:
			case Z_BUF_ERROR:
				if (UNIV_UNLIKELY(d_stream->avail_out)) {
					page_zip_fail("page_zip_decompress_clust_ext_rec:5"
									" inflate(Z_SYNC_FLUSH)=%s\n",
									d_stream->msg);
					ut_error;
				}
				break;
			default:
				page_zip_fail("page_zip_decompress_clust_ext_rec:6"
					       " 2 inflate(Z_SYNC_FLUSH)=%s\n",
					       d_stream->msg);
				ut_error;
				break;
			}

			ut_ad(d_stream->next_out == dst);

			/* Clear the BLOB pointer in case
			the record will be deleted and the
			space will not be reused.  Note that
			the final initialization of the BLOB
			pointers (copying from "externs"
			or clearing) will have to take place
			only after the page modification log
			has been applied.  Otherwise, we
			could end up with an uninitialized
			BLOB pointer when a record is deleted,
			reallocated and deleted. */
			memset(d_stream->next_out, 0,
			       BTR_EXTERN_FIELD_REF_SIZE);
			d_stream->next_out
				+= BTR_EXTERN_FIELD_REF_SIZE;
		}
	}
	/* Decompress the last bytes of the record. */
	d_stream->avail_out = rec_get_end(rec, offsets)
					- d_stream->next_out;

	switch (inflate(d_stream, Z_SYNC_FLUSH)) {
	case Z_STREAM_END:
	case Z_OK:
	case Z_BUF_ERROR:
		if (UNIV_UNLIKELY(d_stream->avail_out)) {
			page_zip_fail("page_zip_decompress_clust_ext_rec:7"
							" inflate(Z_SYNC_FLUSH)=%s\n",
							d_stream->msg);
			ut_error;
		}
		break;
	default:
		page_zip_fail("page_zip_decompress_clust_ext_rec:8"
						" inflate(Z_SYNC_FLUSH)=%s\n",
						d_stream->msg);
		ut_error;
		break;
	}
}

/**********************************************************************//**
Decompress a record of a leaf node of a clustered index.
@return	TRUE if decompression stream ended, FALSE otherwise */
static
ibool
page_zip_decompress_clust_rec(
/*======================*/
	z_stream*	d_stream,	/*!< in/out: compressed page stream */
	const dict_index_t*	index,	/*!< in: the index of the page */
	rec_t*	rec,	/*!< in/out: record */
	ulint*	offsets,	/*!< in/out: temporary offsets */
	ulint	trx_id_col,	/*!< in: position of DB_TRX_ID */
	mem_heap_t*	heap)		/*!< in: temporary memory heap */
{
	int	err;
	ulint	len;
	byte*	dst;
	d_stream->avail_out = rec - REC_N_NEW_EXTRA_BYTES - d_stream->next_out;

	ut_ad(d_stream->avail_out < UNIV_PAGE_SIZE - PAGE_ZIP_START - PAGE_DIR);
	err = inflate(d_stream, Z_SYNC_FLUSH);
	switch (err) {
	case Z_STREAM_END:
		/* Apparently, n_dense has grown
		since the time the page was last compressed. */
		return TRUE;
	case Z_OK:
	case Z_BUF_ERROR:
		if (UNIV_UNLIKELY(d_stream->avail_out)) {
			page_zip_fail("page_zip_decompress_clust_rec:"
							" 1 inflate(Z_SYNC_FLUSH)=%s\n",
							d_stream->msg);
			ut_error;
		}
		break;
	default:
		page_zip_fail("page_zip_decompress_clust_rec:"
						" 2 inflate(Z_SYNC_FLUSH)=%s\n",
						d_stream->msg);
		ut_error;
		break;
	}

	ut_ad(d_stream->next_out == rec - REC_N_NEW_EXTRA_BYTES);
	/* Prepare to decompress the data bytes. */
	d_stream->next_out = rec;

	/* Read the offsets. The status bits are needed here. */
	offsets = rec_get_offsets(rec, index, offsets,
				  ULINT_UNDEFINED, &heap);

	/* Check if there are any externally stored columns.
	For each externally stored column, restore the
	BTR_EXTERN_FIELD_REF separately. */

	if (rec_offs_any_extern(offsets)) {
		page_zip_decompress_clust_ext_rec(d_stream, rec, offsets, trx_id_col);
		return FALSE;
	}

	/* Skip trx_id and roll_ptr */
	dst = rec_get_nth_field(rec, offsets, trx_id_col, &len);
	if (UNIV_UNLIKELY(len < DATA_TRX_RBP_LEN)) {
		page_zip_fail("page_zip_decompress_clust_rec:"
			       " 3 len = %lu\n", (ulong) len);
		ut_error;
	}

	d_stream->avail_out = dst - d_stream->next_out;

	switch (inflate(d_stream, Z_SYNC_FLUSH)) {
	case Z_STREAM_END:
	case Z_OK:
	case Z_BUF_ERROR:
		if (UNIV_UNLIKELY(d_stream->avail_out)) {
				page_zip_fail("page_zip_decompress_clust_rec:"
								" 4 inflate(Z_SYNC_FLUSH)=%s\n",
								d_stream->msg);
				ut_error;
		}
		break;
	default:
		page_zip_fail("page_zip_decompress_clust_rec:"
			       " 5 inflate(Z_SYNC_FLUSH)=%s\n",
			       d_stream->msg);
		ut_error;
		break;
	}

	ut_ad(d_stream->next_out == dst);

	/* Clear DB_TRX_ID and DB_ROLL_PTR in order to
	avoid uninitialized bytes in case the record
	is affected by page_zip_apply_log(). */
	memset(dst, 0, DATA_TRX_RBP_LEN);

	d_stream->next_out += DATA_TRX_RBP_LEN;

	/* Decompress the last bytes of the record. */
	d_stream->avail_out = rec_get_end(rec, offsets)
		- d_stream->next_out;

	switch (inflate(d_stream, Z_SYNC_FLUSH)) {
	case Z_STREAM_END:
	case Z_OK:
	case Z_BUF_ERROR:
		if (UNIV_UNLIKELY(d_stream->avail_out)) {
			page_zip_fail("page_zip_decompress_clust_rec:"
							" 6 inflate(Z_SYNC_FLUSH)=%s\n",
							d_stream->msg);
			ut_error;
		}
		break;
	default:
		page_zip_fail("page_zip_decompress_clust_rec:"
			       " 7 inflate(Z_SYNC_FLUSH)=%s\n",
			       d_stream->msg);
		ut_error;
		break;
	}
	return FALSE;
}

/**********************************************************************//**
Compress the records of a leaf node of a clustered index.
@return TRUE on success, FALSE on failure */
static
ibool
page_zip_decompress_clust(
/*======================*/
	page_zip_des_t*	page_zip,	/*!< in/out: compressed page */
	z_stream*	d_stream,	/*!< in/out: compressed page stream */
	rec_t**	recs,	/*!< in: dense page directory
					sorted by addresses */
	ulint	n_dense,	/*!< in: size of recs[] */
	dict_index_t*	index,	/*!< in: the index of the page */
	ulint	trx_id_col,	/*!< index of the trx_id column */
	ulint*	offsets,	/*!< in/out: temporary offsets */
	ulint*	heap_status_ptr,	/*!< out: pointer to the
					integer where we will store
					the heap status of the last
					record */
	mem_heap_t*	heap)	/*!< in: temporary memory heap */
{
	ulint	slot;
	ulint	heap_status = REC_STATUS_ORDINARY
					| PAGE_HEAP_NO_USER_LOW << REC_HEAP_NO_SHIFT;

	ut_a(dict_index_is_clust(index));

	/* Subtract the space reserved for uncompresesd data. */
	d_stream->avail_in -= n_dense * (PAGE_ZIP_DIR_SLOT_SIZE
					 + DATA_TRX_RBP_LEN);

	/* Decompress the records in heap_no order. */
	for (slot = 0; slot < n_dense; ++slot) {
		rec_t*	rec = recs[slot];

		/* Set heap_no and the status bits. */
		mach_write_to_2(rec - REC_NEW_HEAP_NO, heap_status);
		heap_status += 1 << REC_HEAP_NO_SHIFT;
		if (UNIV_UNLIKELY
				(page_zip_decompress_clust_rec(
					d_stream, index, rec, offsets, trx_id_col, heap))) {

			goto zlib_done;
		}
	}

	page_zip_decompress_trailing_garbage(page_zip, d_stream);

	if (UNIV_UNLIKELY(inflate(d_stream, Z_FINISH) != Z_STREAM_END)) {
		page_zip_fail("page_zip_decompress_clust: "
			       " inflate(Z_FINISH) = %s\n",
			       d_stream->msg);
		ut_error;
	}

	/* Note that d_stream->avail_out > 0 may hold here
	if the modification log is nonempty. */

zlib_done:
	if (UNIV_UNLIKELY(inflateEnd(d_stream) != Z_OK)) {
		ut_error;
	}

	*heap_status_ptr = heap_status;
	return(TRUE);
}

/**********************************************************************//**
This function determines the sign for window_bits and reads the zlib header
from the decompress stream. The data may have been compressed with a negative
(no adler32 headers) or a positive (with adler32 headers) window_bits.
Regardless of the current value of page_zip_zlib_wrap, we always
first try the positive window_bits then negative window_bits, because the
surest way to determine if the stream has adler32 headers is to see if the
stream begins with the zlib header together with the adler32 value of it.
This adds a tiny bit of overhead for the pages that were compressed without
adler32s. */
static
void
page_zip_init_d_stream(
	z_stream* strm)
{
	/* Save initial stream position, in case a reset is required. */
	Bytef* next_in = strm->next_in;
	Bytef* next_out = strm->next_out;
	ulint avail_in = strm->avail_in;
	ulint avail_out = strm->avail_out;

	/* initialization must always succeed regardless of the sign of
	   window_bits */
	ut_a(inflateInit2(strm, UNIV_PAGE_SIZE_SHIFT) == Z_OK);

	/* Try decoding zlib header assuming adler32. Reset the stream on
	   failure. */
	if (inflate(strm, Z_BLOCK) != Z_OK) {
		/* reset the stream */
		strm->next_in = next_in;
		strm->next_out = next_out;
		strm->avail_in = avail_in;
		strm->avail_out = avail_out;
		ut_a(inflateReset2(strm, -UNIV_PAGE_SIZE_SHIFT) == Z_OK);
		/* read the zlib header */
		ut_a(inflate(strm, Z_BLOCK) == Z_OK);
	}
}

/***************************************************************//**
Read the records from the serialized buffer into recs. The uncompressed
fields of the records -if any- are not recovered by this function because
the modification log is not applied yet.
@return True if all the records were recovered from the serialized buffer
successfully, False otherwise.
*/
UNIV_INTERN
ibool
page_zip_deserialize(
	page_t* page, /* in: the header of the page is used. out: *data_end_ptr
			 is set tot page + PAGE_ZIP_START if there were no
			 records on the page when it was serialized. */
	const byte* buf, /*in: buffer that has the serialization of records on
			   the page. */
	rec_t** recs, /* out: The array of records that are going to be
			 recovered from buf */
	ulint n_dense, /* in: The number of records in recs */
	dict_index_t** index_ptr, /* out: *index_ptr will be set to the index
				     object that is created by this function */
	ulint** offsets_ptr, /* out: the offsets object allocated by this
				function will be returned to the caller as
				scratch memory for rec_get_offsets() */
	ulint* trx_id_col_ptr, /* out: this will be set to the column number
				  for transaction id */
	ulint* heap_status_ptr, /* out: this will be set to the heap_status of
				   the last record */
	byte** data_end_ptr, /* out: end of data on the uncompressed page so
				that the caller can zero out the unused parts
				of the page. */
	mem_heap_t* heap) /* in/out: temporary memory heap */
{
	dict_index_t*	index	= NULL;
	ulint		trx_id_col = ULINT_UNDEFINED;
	ulint*		offsets;
	ulint	heap_status;
	const byte* buf_ptr = buf;
	const byte* buf_end = buf + PZ_SERIALIZED_LEN_MAX;
	ulint n_dense_old;
	rec_t* rec;
	ulint len = mach_read_from_3(buf_ptr);
	buf_ptr += 3;
	index = page_zip_fields_decode(
			buf_ptr,
			buf_ptr + len,
			page_is_leaf(page) ? &trx_id_col : NULL);

	if (trx_id_col == ULINT_UNDEFINED && page_is_leaf(page)) {
		trx_id_col = 0;
	}

	buf_ptr += len;
	ut_a(buf_ptr < buf_end);
	if (UNIV_UNLIKELY(!index)) {
		return FALSE;
	}

	{
		/* Pre-allocate the offsets for rec_get_offsets_reverse().
		   See rec_get_offsets_func() for an explanation of how
		   n is computed. */
		ulint	n = 1 + 1/* node ptr */ + REC_OFFS_HEADER_SIZE
			+ dict_index_get_n_fields(index);
		offsets = static_cast<ulint*>(
				mem_heap_alloc(heap, n * sizeof(ulint)));
		*offsets = n;
	}

	/* Deserialize the records in heap_no order. */
	n_dense_old = mach_read_from_2(buf_ptr);
	buf_ptr += 2;
	ut_a(buf_ptr < buf_end);
	ut_a(n_dense_old <= n_dense);
	if (UNIV_LIKELY(page_is_leaf(page))) {
		heap_status = REC_STATUS_ORDINARY
		              | (PAGE_HEAP_NO_USER_LOW << REC_HEAP_NO_SHIFT);
	} else {
		heap_status = REC_STATUS_NODE_PTR
		              | (PAGE_HEAP_NO_USER_LOW << REC_HEAP_NO_SHIFT);
	}
	if (n_dense_old) {
		while (n_dense_old) {
			rec = *recs++;
			mach_write_to_2(rec - REC_NEW_HEAP_NO, heap_status);
			buf_ptr = page_zip_deserialize_rec(
					buf_ptr, rec, index, offsets,
					trx_id_col, heap_status, buf_end);
			if (buf_ptr >= buf_end) {
				page_zip_fields_free(index);
				return FALSE;
			}
			heap_status += 1 << REC_HEAP_NO_SHIFT;
			--n_dense_old;
		}
		*data_end_ptr = rec_get_end(rec, offsets);
	} else {
		*data_end_ptr = page + PAGE_ZIP_START;
	}
	*index_ptr = index;
	*offsets_ptr = offsets;
	*heap_status_ptr = heap_status;
	*trx_id_col_ptr = trx_id_col;
	return TRUE;
}

/**********************************************************************//**
Decompress a page that was compressed using zlib's streaming interface.
@return TRUE on success, FALSE on failure */
UNIV_INTERN
ibool
page_zip_decompress_zlib_stream(
	page_zip_des_t* page_zip, /*!< in: data, ssize; out: m_start, m_end,
				    m_nonempty, n_blobs */
	page_t* page, /*!< out: uncompressed page, may be trashed */
	rec_t** recs, /*!< in: dense page directory sorted by address */
	ulint n_dense, /*!< in: size of recs[] */
	dict_index_t** index_ptr, /*!< out: *index_ptr is set to the index
				    object that's created by this function.
				    The caller is responsible for calling
				    dict_index_mem_free() on *index_ptr. */
	ulint** offsets_ptr, /*!< out: *offsets_ptr will point to an array of
			       offsets that can be used by the caller */
	ulint* trx_id_col_ptr, /*!< out: *trx_id_col_ptr will be set to the
				 column number for the transaction id column
				 if this is a leaf primary key page. Otherwise
				 it will be set to ULINT_UNDEFINED */
	ulint* heap_status_ptr, /*!< out: *heap_status_ptr will be set to the
				  heap status (heap number and metadata) of the
				  last record */
	byte** data_end_ptr, /*!< out: *data_end_ptr will point to the end of
			       the last record on the compressed page image. */
	mem_heap_t* heap) /*!< in: temporary memory heap */
{
	z_stream d_stream;
	dict_index_t* index = NULL;
	ulint trx_id_col = ULINT_UNDEFINED;
	ulint* offsets;
	ulint heap_status;

	page_zip_set_alloc(&d_stream, heap);

	d_stream.next_in = page_zip->data + PAGE_DATA;
	/* Subtract the space reserved for
	the page header and the end marker of the modification log. */
	d_stream.avail_in = page_zip_get_size(page_zip) - (PAGE_DATA + 1);
	d_stream.next_out = page + PAGE_ZIP_START;
	d_stream.avail_out = UNIV_PAGE_SIZE - PAGE_ZIP_START;

	page_zip_init_d_stream(&d_stream);

	/* Decode index */
	if (UNIV_UNLIKELY(inflate(&d_stream, Z_BLOCK) != Z_OK)) {

		page_zip_fail("page_zip_decompress_zlib_stream:"
			       " 1 inflate(Z_BLOCK)=%s\n", d_stream.msg);
		return FALSE;
	}

	index = page_zip_fields_decode(
		page + PAGE_ZIP_START, d_stream.next_out,
		page_is_leaf(page) ? &trx_id_col : NULL);

	if (UNIV_UNLIKELY(!index)) {

		return FALSE;
	}

	d_stream.next_out = page + PAGE_ZIP_START;

	{
		/* Pre-allocate the offsets for rec_get_offsets_reverse(). */
		ulint n = 1
			  + 1/* node ptr */
			  + REC_OFFS_HEADER_SIZE
			  + dict_index_get_n_fields(index);
		offsets = static_cast<ulint*> (
				mem_heap_alloc(heap, n * sizeof(ulint)));
		*offsets = n;
	}

	/* Decompress the records in heap_no order. */
	if (UNIV_UNLIKELY(!page_is_leaf(page))) {
		/* This is a node pointer page. */
		if (UNIV_UNLIKELY
		    (!page_zip_decompress_node_ptrs(page_zip, &d_stream,
						    recs, n_dense, index,
						    offsets, &heap_status,
						    heap))) {
			page_zip_fail("page_zip_decompress_zlib_stream:"
				       " 2 page_zip_decompress_node_ptrs"
				       " failed");
			page_zip_fields_free(index);
			return FALSE;
		}
	} else if (trx_id_col == ULINT_UNDEFINED) {
		/* This is a leaf page in a secondary index. */
		trx_id_col = 0;
		if (UNIV_UNLIKELY(!page_zip_decompress_sec(page_zip, &d_stream,
							   recs, n_dense,
							   index,
							   &heap_status))) {
			page_zip_fail("page_zip_decompress_zlib_stream:"
				       " 3 page_zip_decompress_sec failed");
			page_zip_fields_free(index);
			return FALSE;
		}
	} else {
		/* This is a leaf page in a clustered index. */
		if (UNIV_UNLIKELY(!page_zip_decompress_clust(page_zip,
							     &d_stream, recs,
							     n_dense, index,
							     trx_id_col,
							     offsets,
							     &heap_status,
							     heap))) {
			page_zip_fail("page_zip_decompress_zlib_stream:"
				       " 4 page_zip_decompress_clust failed");
			page_zip_fields_free(index);
			return FALSE;
		}
	}

	page_zip->m_end = PAGE_DATA + d_stream.total_in;
	*index_ptr = index;
	*trx_id_col_ptr = trx_id_col;
	*heap_status_ptr = heap_status;
	*data_end_ptr = d_stream.next_out;
	*offsets_ptr = offsets;
	return(TRUE);
}

/**********************************************************************//**
Decompress a page.  This function should tolerate errors on the compressed
page.  Instead of letting assertions fail, it will return FALSE if an
inconsistency is detected.
@return TRUE on success, FALSE on failure */
UNIV_INTERN
ibool
page_zip_decompress(
/*================*/
	page_zip_des_t*	page_zip,	/*!< in: data, ssize; out: m_start,
					m_end, m_nonempty, n_blobs */
	page_t*		page,		/*!< out: uncompressed page, may be
					trashed */
	ibool		all,		/*!< in: TRUE=decompress the whole page;
					FALSE=verify but do not copy some
					page header fields that should not
					change after page creation */
	ulint		space_id,	/*!< in: table space id */
	ulint		fsp_flags,	/*!< in: used to compute compression
					type and flags. If this is
					ULINT_UNDEFINED then fsp_flags is
					determined by other means. */
	mem_heap_t**	heap_ptr,	/*!< out: if index_ptr is not NULL then
					*heap_ptr is set to the heap that is
					allocated by this function. The caller
					is responsible for freeing the heap. */
	dict_index_t**	index_ptr)	/*!< out: if index_ptr is not NULL then
					*index_ptr is set to the index object
					that's created by this function. The
					caller is responsible for calling
					dict_index_mem_free().*/
{
	dict_index_t* index = NULL;
	rec_t** recs; /*!< dense page directory, sorted by address */
	ulint n_dense; /* number of user records on the page */
	ulint trx_id_col = ULINT_UNDEFINED;
	mem_heap_t* heap;
	ulint* offsets;
	const byte* mod_log_ptr;
	const byte* page_dir_start;
	ulint info_bits = 0;
	ulint heap_status;
	byte* data_end = NULL; /*!< pointer to the end of the data after the
				 compressed page image is decompressed from the
				 compressed page image */
	ulint trailer_len; /*!< length of the trailer after the compressed page
			     image is decompressed */
	ibool ret;
	uchar comp_level;
	uchar comp_type;
	byte* buf;
#ifndef UNIV_HOTBACKUP
	page_zip_stat_t* zip_stat = &page_zip_stat[page_zip->ssize - 1];
	ulonglong start = my_timer_now();
#endif /* !UNIV_HOTBACKUP */

	ut_ad(page_zip_simple_validate(page_zip));
	UNIV_MEM_ASSERT_W(page, UNIV_PAGE_SIZE);
	UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));

	fsp_flags = fil_get_fsp_flags(space_id, fsp_flags);

	/* The dense directory excludes the infimum and supremum records. */
	n_dense = page_dir_get_n_heap(page_zip->data) - PAGE_HEAP_NO_USER_LOW;
	if (UNIV_UNLIKELY(n_dense * PAGE_ZIP_DIR_SLOT_SIZE
			  >= page_zip_get_size(page_zip))) {
		page_zip_fail("page_zip_decompress 1: %lu %lu\n",
			       (ulong) n_dense,
			       (ulong) page_zip_get_size(page_zip));
		return(FALSE);
	}

	/* asking a heap of size 0 will just give a memory block
	   with the default block size which is OK */
	heap = mem_heap_create_cached(0, malloc_cache_decompress);

	recs = static_cast<rec_t**>(
		mem_heap_alloc(heap, n_dense * (2 * sizeof *recs)));

	if (all) {
		/* Copy the page header. */
		memcpy(page, page_zip->data, PAGE_DATA);
	} else {
		/* Check that the bytes that we skip are identical. */
		if (0
#ifdef UNIV_DEBUG
				|| 1
#endif
				|| UNIV_UNLIKELY(page_zip_debug)) {
		ut_a(!memcmp(FIL_PAGE_TYPE + page,
			     FIL_PAGE_TYPE + page_zip->data,
			     PAGE_HEADER - FIL_PAGE_TYPE));
		ut_a(!memcmp(PAGE_HEADER + PAGE_LEVEL + page,
			     PAGE_HEADER + PAGE_LEVEL + page_zip->data,
			     PAGE_DATA - (PAGE_HEADER + PAGE_LEVEL)));
		}

		/* Copy the mutable parts of the page header. */
		memcpy(page, page_zip->data, FIL_PAGE_TYPE);
		memcpy(PAGE_HEADER + page, PAGE_HEADER + page_zip->data,
		       PAGE_LEVEL - PAGE_N_DIR_SLOTS);

		if (0
#ifdef UNIV_DEBUG
				|| 1
#endif
				|| UNIV_UNLIKELY(page_zip_debug)) {
			/* Check that the page headers match after copying. */
			ut_a(!memcmp(page, page_zip->data, PAGE_DATA));
		}
	}

	if (UNIV_UNLIKELY(page_zip_debug)) {
		/* Clear the uncompressed page, except the header. */
		memset(PAGE_DATA + page, 0x55, UNIV_PAGE_SIZE - PAGE_DATA);
	}

	UNIV_MEM_INVALID(PAGE_DATA + page, UNIV_PAGE_SIZE - PAGE_DATA);

	/* Copy the page directory. */
	if (UNIV_UNLIKELY(!page_zip_dir_decode(page_zip, page, recs,
					       recs + n_dense, n_dense))) {
		mem_heap_free(heap);
		return(FALSE);
	}

	/* Copy the infimum and supremum records. */
	memcpy(page + (PAGE_NEW_INFIMUM - REC_N_NEW_EXTRA_BYTES),
	       infimum_extra, sizeof infimum_extra);
	if (page_is_empty(page)) {
		rec_set_next_offs_new(page + PAGE_NEW_INFIMUM,
				      PAGE_NEW_SUPREMUM);
	} else {
		rec_set_next_offs_new(page + PAGE_NEW_INFIMUM,
				      page_zip_dir_get(page_zip, 0)
				      & PAGE_ZIP_DIR_SLOT_MASK);
	}
	memcpy(page + PAGE_NEW_INFIMUM, infimum_data, sizeof infimum_data);
	memcpy(page + (PAGE_NEW_SUPREMUM - REC_N_NEW_EXTRA_BYTES + 1),
	       supremum_extra_data, sizeof supremum_extra_data);

	if (UNIV_UNLIKELY(!page_is_leaf(page))) {
		/* This is a node pointer page. */
		info_bits = mach_read_from_4(page + FIL_PAGE_PREV) == FIL_NULL
			    ? REC_INFO_MIN_REC_FLAG : 0;
	}

	/* Set number of blobs to zero */
	page_zip->n_blobs = 0;

	comp_level = FSP_FLAGS_GET_COMP_LEVEL(fsp_flags);
	comp_type = FSP_FLAGS_GET_COMP_TYPE(fsp_flags);
	page_zip->compact_metadata = FSP_FLAGS_GET_COMPACT_METADATA(fsp_flags);

	if (comp_type == DICT_TF_COMP_ZLIB_STREAM) {
		/* compress using zlib's streaming interface */
		ret = page_zip_decompress_zlib_stream(page_zip, page, recs,
						      n_dense, &index,
						      &offsets, &trx_id_col,
						      &heap_status, &data_end,
						      heap);
	} else {
		ulint compressed_len = 0;
		comp_state_t comp_state;
		ulint avail_in = page_zip_get_size(page_zip)
				 - PAGE_DATA - 1;
		/* buffer for storing the serialized page */
		buf = static_cast<byte*>(
				mem_heap_alloc(heap, 2 * UNIV_PAGE_SIZE));
		comp_state.in = page_zip->data + PAGE_DATA;
		comp_state.avail_in = avail_in;
		comp_state.out = buf;
		comp_state.avail_out = 2 * UNIV_PAGE_SIZE;
		comp_state.level = comp_level;
		comp_state.heap = heap;
		comp_decompress(comp_type, &comp_state);
		ut_a(avail_in > comp_state.avail_in);
		compressed_len = avail_in - comp_state.avail_in;
		page_zip->m_end = compressed_len + PAGE_DATA;
		ret = page_zip_deserialize(
			page, buf, recs, n_dense, &index, &offsets,
			&trx_id_col, &heap_status, &data_end, heap);
	}

	if (!ret)
		goto err_exit;

	/* Clear the unused heap space on the uncompressed page. */
	page_dir_start = page_dir_get_nth_slot(page,
					       page_dir_get_n_slots(page) - 1);
	memset(data_end, 0, page_dir_start - data_end);

	ut_d(page_zip->m_start = page_zip->m_end);

	/* Apply the modification log. At this point page_zip->m_end must have
	been set to where the compression output ends. */
	trailer_len = page_zip_get_trailer_len(page_zip,
					       trx_id_col
					       && (trx_id_col
						   != ULINT_UNDEFINED));

	mod_log_ptr = page_zip_apply_log(page_zip->data + page_zip->m_end,
					 page_zip_get_size(page_zip)
					 - page_zip->m_end
					 - trailer_len,
					 recs,
					 n_dense,
					 trx_id_col,
					 heap_status,
					 index,
					 offsets);
	if (UNIV_UNLIKELY(!mod_log_ptr)) {
		page_zip_fail("page_zip_decompress 2: applying "
			       "modification log failed");
		goto err_exit;
	}

	page_zip->m_nonempty = (mod_log_ptr != (page_zip->data
						+ page_zip->m_end));
	page_zip->m_end = mod_log_ptr - page_zip->data;

	/* size check */
	page_zip_size_check(page_zip, index);

	/* restore uncompressed fields if the page is a node pointer page or a
	   primary key leaf page */
	if (trx_id_col) {
		page_zip_restore_uncompressed_fields_all(page_zip, recs, index,
							 trx_id_col, heap);
	}

	/* size check */
	page_zip_size_check(page_zip, index);

	/* set the extra bytes */
	if (UNIV_UNLIKELY(!page_zip_set_extra_bytes(page_zip, page,
			  info_bits))) {
err_exit:
		if (index) {
			page_zip_fields_free(index);
		}
		mem_heap_free(heap);
		return (FALSE);
	}

	ut_a(page_is_comp(page));
	UNIV_MEM_ASSERT_RW(page, UNIV_PAGE_SIZE);

#ifndef UNIV_HOTBACKUP
	ulonglong time_diff = my_timer_since(start);
	page_zip_update_zip_stats_decompress(zip_stat, time_diff,
					     dict_index_is_clust(index));
	page_zip_update_fil_comp_stats_decompress(
		space_id, page_zip_get_size(page_zip), time_diff);
	index_id_t	index_id = btr_page_get_index_id(page);

	if (srv_cmp_per_index_enabled) {
		page_zip_update_per_index_stats_decompress(index_id, time_diff);
	}
#endif /* !UNIV_HOTBACKUP */

	if (heap_ptr) {
		if (index_ptr) {
			*index_ptr = index;
		}
		*heap_ptr = heap;
	} else {
		ut_ad(!index_ptr);
		page_zip_fields_free(index);
		mem_heap_free(heap);
	}

	/* Update the stat counter for LRU policy. */
	buf_LRU_stat_inc_unzip();

	MONITOR_INC(MONITOR_PAGE_DECOMPRESS);

	return(TRUE);
}

/** Flag: make page_zip_validate() compare page headers only */
UNIV_INTERN ibool	page_zip_validate_header_only = FALSE;

/**********************************************************************//**
Check that the compressed and decompressed pages match.
@return	TRUE if valid, FALSE if not */
UNIV_INTERN
ibool
page_zip_validate_low(
/*==================*/
	const page_zip_des_t*	page_zip,/*!< in: compressed page */
	const page_t*		page,	/*!< in: uncompressed page */
	const dict_index_t*	index,	/*!< in: index of the page, if known */
	ibool			sloppy)	/*!< in: FALSE=strict,
					TRUE=ignore the MIN_REC_FLAG */
{
	page_zip_des_t	temp_page_zip;
	byte*		temp_page_buf;
	page_t*		temp_page;
	ibool		valid;
	mem_heap_t* heap = NULL;
	dict_index_t**	index_ptr;
	if (index) {
		index_ptr = NULL;
	} else {
		index_ptr = (dict_index_t**)(&index);
	}

	ut_a(page_zip_debug);

	if (memcmp(page_zip->data + FIL_PAGE_PREV, page + FIL_PAGE_PREV,
		   FIL_PAGE_LSN - FIL_PAGE_PREV)
	    || memcmp(page_zip->data + FIL_PAGE_TYPE, page + FIL_PAGE_TYPE, 2)
	    || memcmp(page_zip->data + FIL_PAGE_DATA, page + FIL_PAGE_DATA,
		      PAGE_DATA - FIL_PAGE_DATA)) {
		page_zip_fail("page_zip_validate: page header\n");
		ut_hexdump(page_zip, sizeof *page_zip);
		ut_hexdump(page_zip->data, page_zip_get_size(page_zip));
		ut_hexdump(page, UNIV_PAGE_SIZE);
		return(FALSE);
	}

	ut_a(page_is_comp(page));

	if (page_zip_validate_header_only) {
		return(TRUE);
	}

	/* page_zip_decompress() expects the uncompressed page to be
	UNIV_PAGE_SIZE aligned. */
	temp_page_buf = static_cast<byte*>(ut_malloc(2 * UNIV_PAGE_SIZE));
	temp_page = static_cast<byte*>(ut_align(temp_page_buf, UNIV_PAGE_SIZE));

	UNIV_MEM_ASSERT_RW(page, UNIV_PAGE_SIZE);
	UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));

	temp_page_zip = *page_zip;
	valid = page_zip_decompress(
			&temp_page_zip, temp_page, TRUE,
			page_get_space_id(page), ULINT_UNDEFINED,
			&heap, index_ptr);
	if (!valid) {
		fputs("page_zip_validate(): failed to decompress\n", stderr);
		goto func_exit;
	}
	if (page_zip->n_blobs != temp_page_zip.n_blobs) {
		page_zip_fail("page_zip_validate: n_blobs: %u!=%u\n",
			       page_zip->n_blobs, temp_page_zip.n_blobs);
		valid = FALSE;
	}
#ifdef UNIV_DEBUG
	if (page_zip->m_start != temp_page_zip.m_start) {
		page_zip_fail("page_zip_validate: m_start: %u!=%u\n",
			       page_zip->m_start, temp_page_zip.m_start);
		valid = FALSE;
	}
#endif /* UNIV_DEBUG */
	if (page_zip->m_end != temp_page_zip.m_end) {
		page_zip_fail("page_zip_validate: m_end: %u!=%u\n",
			       page_zip->m_end, temp_page_zip.m_end);
		valid = FALSE;
	}
	if (page_zip->m_nonempty != temp_page_zip.m_nonempty) {
		page_zip_fail("page_zip_validate(): m_nonempty: %u!=%u\n",
			       page_zip->m_nonempty,
			       temp_page_zip.m_nonempty);
		valid = FALSE;
	}
	if (memcmp(page + PAGE_HEADER, temp_page + PAGE_HEADER,
		   UNIV_PAGE_SIZE - PAGE_HEADER - FIL_PAGE_DATA_END)) {

		/* In crash recovery, the "minimum record" flag may be
		set incorrectly until the mini-transaction is
		committed.  Let us tolerate that difference when we
		are performing a sloppy validation. */

		ulint*		offsets;
		const rec_t*	rec;
		const rec_t*	trec;
		byte		info_bits_diff;
		ulint		offset
			= rec_get_next_offs(page + PAGE_NEW_INFIMUM, TRUE);
		ut_a(offset >= PAGE_NEW_SUPREMUM);
		offset -= 5/*REC_NEW_INFO_BITS*/;

		info_bits_diff = page[offset] ^ temp_page[offset];

		if (info_bits_diff == REC_INFO_MIN_REC_FLAG) {
			temp_page[offset] = page[offset];

			if (!memcmp(page + PAGE_HEADER,
				    temp_page + PAGE_HEADER,
				    UNIV_PAGE_SIZE - PAGE_HEADER
				    - FIL_PAGE_DATA_END)) {

				/* Only the minimum record flag
				differed.  Let us ignore it. */
				page_zip_fail("page_zip_validate: "
					       "min_rec_flag "
					       "(%s"
					       "%lu,%lu,0x%02lx)\n",
					       sloppy ? "ignored, " : "",
					       page_get_space_id(page),
					       page_get_page_no(page),
                                              (ulong) page[offset]);
				valid = sloppy;
				goto func_exit;
			}
		}

		/* Compare the pointers in the PAGE_FREE list. */
		rec = page_header_get_ptr(page, PAGE_FREE);
		trec = page_header_get_ptr(temp_page, PAGE_FREE);

		while (rec || trec) {
			if (page_offset(rec) != page_offset(trec)) {
				page_zip_fail("page_zip_validate: "
					       "PAGE_FREE list: %u!=%u\n",
					       (unsigned) page_offset(rec),
					       (unsigned) page_offset(trec));
				valid = FALSE;
				goto func_exit;
			}

			rec = page_rec_get_next_low(rec, TRUE);
			trec = page_rec_get_next_low(trec, TRUE);
		}

		/* Compare the records. */
		offsets = NULL;
		rec = page_rec_get_next_low(
			page + PAGE_NEW_INFIMUM, TRUE);
		trec = page_rec_get_next_low(
			temp_page + PAGE_NEW_INFIMUM, TRUE);

		do {
			if (page_offset(rec) != page_offset(trec)) {
				page_zip_fail("page_zip_validate: "
					       "record list: 0x%02x!=0x%02x\n",
					       (unsigned) page_offset(rec),
					       (unsigned) page_offset(trec));
				valid = FALSE;
				break;
			}

			/* Compare the data. */
			offsets = rec_get_offsets(
				rec, index, offsets,
				ULINT_UNDEFINED, &heap);

			if (memcmp(rec - rec_offs_extra_size(offsets),
				   trec - rec_offs_extra_size(offsets),
				   rec_offs_size(offsets))) {
				page_zip_fail(
					"page_zip_validate: "
					"record content: 0x%02x",
					(unsigned) page_offset(rec));
				valid = FALSE;
				break;
			}

			rec = page_rec_get_next_low(rec, TRUE);
			trec = page_rec_get_next_low(trec, TRUE);
		} while (rec || trec);
	}

	if (heap) {
		mem_heap_free(heap);
	}

func_exit:
	if (!valid) {
		ut_hexdump(page_zip, sizeof *page_zip);
		ut_hexdump(page_zip->data, page_zip_get_size(page_zip));
		ut_hexdump(page, UNIV_PAGE_SIZE);
		ut_hexdump(temp_page, UNIV_PAGE_SIZE);
	}
	ut_free(temp_page_buf);
	return(valid);
}

/**********************************************************************//**
Check that the compressed and decompressed pages match.
@return	TRUE if valid, FALSE if not */
UNIV_INTERN
ibool
page_zip_validate(
/*==============*/
	const page_zip_des_t*	page_zip,/*!< in: compressed page */
	const page_t*		page,	/*!< in: uncompressed page */
	const dict_index_t*	index)	/*!< in: index of the page, if known */
{
	return(page_zip_validate_low(page_zip, page, index,
				     recv_recovery_is_on()));
}

#ifdef UNIV_DEBUG
/**********************************************************************//**
Assert that the compressed and decompressed page headers match.
@return	TRUE */
static
ibool
page_zip_header_cmp(
/*================*/
	const page_zip_des_t*	page_zip,/*!< in: compressed page */
	const byte*		page)	/*!< in: uncompressed page */
{
	ut_ad(!memcmp(page_zip->data + FIL_PAGE_PREV, page + FIL_PAGE_PREV,
		      FIL_PAGE_LSN - FIL_PAGE_PREV));
	ut_ad(!memcmp(page_zip->data + FIL_PAGE_TYPE, page + FIL_PAGE_TYPE,
		      2));
	ut_ad(!memcmp(page_zip->data + FIL_PAGE_DATA, page + FIL_PAGE_DATA,
		      PAGE_DATA - FIL_PAGE_DATA));

	return(TRUE);
}
#endif /* UNIV_DEBUG */

/**********************************************************************//**
Write the blob pointers of the given record on the blob storage. This function
must not be called during the compression/serialization of the page, for that
page_zip_store_blobs_for_rec() must be called. This function must be called
when a record is updated or created */
static
void
page_zip_write_rec_blobs(
	page_zip_des_t* page_zip, /* compressed page with meta data */
	dict_index_t* index, /* index for the page */
	const rec_t* rec, /* the records for which blob pointer are to be
			     stored */
	const ulint* offsets, /* column offsets for the record */
	ibool create) /* record is being inserted if create=TRUE, it is being
			 updated otherwise */
{
	byte* externs;
	ulint blob_no;
	ulint n_ext = rec_offs_n_extern(offsets);

	if (!n_ext) {
		return;
	}

	blob_no = page_zip_get_n_prev_extern(page_zip, rec, index);
	ut_ad(blob_no <= page_zip->n_blobs);

	externs = page_zip_get_blob_ptr_storage(page_zip);

	if (create && page_zip->compact_metadata) {
		/* memmove the trx rbp storage to make room for the blob
		   pointers */
		ulint trx_rbp_storage_size =
			page_zip_get_trx_rbp_storage_size(page_zip);
		byte* trx_rbp_storage_end =
			page_zip_get_trx_rbp_storage(page_zip, false)
			- trx_rbp_storage_size;
		memmove(trx_rbp_storage_end
			- n_ext * BTR_EXTERN_FIELD_REF_SIZE,
			trx_rbp_storage_end,
			trx_rbp_storage_size);
		/* store the number of blobs in the beginning of externs */
		ut_ad(page_zip->n_blobs == page_zip_read_n_blob(page_zip));
		page_zip_write_n_blob(page_zip, page_zip->n_blobs + n_ext);
	}

	if (create) {
		byte* ext_end = externs - (page_zip->n_blobs
					   * BTR_EXTERN_FIELD_REF_SIZE);
		byte* externs_ptr = externs - (blob_no
					       * BTR_EXTERN_FIELD_REF_SIZE);
		memmove(ext_end - n_ext * BTR_EXTERN_FIELD_REF_SIZE, ext_end,
			externs_ptr - ext_end);
		page_zip->n_blobs += n_ext;
	}

	page_zip_store_blobs(externs, rec, offsets, blob_no);
}

/**********************************************************************//**
Write an entire record on the compressed page.  The data must already
have been written to the uncompressed page. */
UNIV_INTERN
void
page_zip_write_rec(
/*===============*/
	page_zip_des_t*	page_zip,/*!< in/out: compressed page */
	const byte*	rec,	/*!< in: record being written */
	dict_index_t*	index,	/*!< in: the index the record belongs to */
	const ulint*	offsets,/*!< in: rec_get_offsets(rec, index) */
	ulint		create)	/*!< in: nonzero=insert, zero=update */
{
	const page_t*	page;
	byte*		data;
	ulint		rec_no;
	byte*		slot;
	ulint		trx_id_col;
	byte*		trx_rbp_storage;

	ut_ad(PAGE_ZIP_MATCH(rec, page_zip));
	ut_ad(page_zip_simple_validate(page_zip));
	ut_ad(page_zip_get_size(page_zip)
	      > PAGE_DATA + page_zip_dir_size(page_zip));
	ut_ad(rec_offs_comp(offsets));
	ut_ad(rec_offs_validate(rec, index, offsets));

	ut_ad(page_zip->m_start >= PAGE_DATA);

	page = page_align(rec);

	ut_ad(page_zip_header_cmp(page_zip, page));
	ut_ad(page_simple_validate_new((page_t*) page));

	UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));
	UNIV_MEM_ASSERT_RW(rec, rec_offs_data_size(offsets));
	UNIV_MEM_ASSERT_RW(rec - rec_offs_extra_size(offsets),
			   rec_offs_extra_size(offsets));

	slot = page_zip_dir_find(page_zip, page_offset(rec));
	ut_a(slot);
	/* Copy the delete mark. */
	if (rec_get_deleted_flag(rec, TRUE)) {
		*slot |= PAGE_ZIP_DIR_SLOT_DEL >> 8;
	} else {
		*slot &= ~(PAGE_ZIP_DIR_SLOT_DEL >> 8);
	}

	ut_ad(rec_get_start((rec_t*) rec, offsets) >= page + PAGE_ZIP_START);
	ut_ad(rec_get_end((rec_t*) rec, offsets) <= page + UNIV_PAGE_SIZE
	      - PAGE_DIR - PAGE_DIR_SLOT_SIZE
	      * page_dir_get_n_slots(page));

	rec_no = rec_get_heap_no_new(rec) - PAGE_HEAP_NO_USER_LOW;
	ut_ad(rec_no < page_dir_get_n_heap(page) - PAGE_HEAP_NO_USER_LOW);

	/* Append to the modification log. */
	data = page_zip->data + page_zip->m_end;
	ut_ad(!*data);

	/* Identify the record by writing its heap number - 1.
	0 is reserved to indicate the end of the modification log. */

	if (UNIV_UNLIKELY(rec_no + 1 >= 64)) {
		*data++ = (byte) (0x80 | (rec_no + 1) >> 7);
		ut_ad(!*data);
	}
	*data++ = (byte) ((rec_no + 1) << 1);
	ut_ad(!*data);

	trx_id_col = page_zip_get_trx_id_col_for_compression(page, index);

	/* Serialize the record into modification log */
#ifdef UNIV_DEBUG
	data = page_zip_serialize_rec(TRUE, data, rec, offsets, trx_id_col);
#else
	data = page_zip_serialize_rec(data, rec, offsets, trx_id_col);
#endif
	/* Store uncompressed fields of the record on the trailer of
	page_zip->data */
	if (UNIV_UNLIKELY(trx_id_col == ULINT_UNDEFINED)) {
		/* node ptr page */
		page_zip_trailer_write_node_ptr(page_zip, rec_no, rec, offsets);
	} else if (trx_id_col) {
		/* primary key page */
		trx_rbp_storage = page_zip_get_trx_rbp_storage(page_zip, false);
		page_zip_store_trx_rbp(trx_rbp_storage, rec, offsets, rec_no,
				       trx_id_col);
		page_zip_write_rec_blobs(page_zip, index, rec, offsets, create);
		page_zip_size_check(page_zip, index);
	}
	ut_a(!*data);
	ut_ad((ulint) (data - page_zip->data) < page_zip_get_size(page_zip));
	page_zip->m_end = data - page_zip->data;
	page_zip->m_nonempty = TRUE;

	if (UNIV_UNLIKELY(page_zip_debug)) {
		ut_a(page_zip_validate(page_zip, page_align(rec), index));
	}
}

/**********************************************************************//**
Write a BLOB pointer of a record on the leaf page of a clustered index.
The information must already have been updated on the uncompressed page. */
UNIV_INTERN
void
page_zip_write_blob_ptr(
/*====================*/
	page_zip_des_t*	page_zip,/*!< in/out: compressed page */
	const byte*	rec,	/*!< in/out: record whose data is being
				written */
	dict_index_t*	index,	/*!< in: index of the page */
	const ulint*	offsets,/*!< in: rec_get_offsets(rec, index) */
	ulint		n,	/*!< in: column index */
	mtr_t*		mtr)	/*!< in: mini-transaction handle,
				or NULL if no logging is needed */
{
	const byte*	field;
	byte*		externs;
	const page_t*	page	= page_align(rec);
	ulint		blob_no;
	ulint		len;

	ut_ad(PAGE_ZIP_MATCH(rec, page_zip));
	ut_ad(page_simple_validate_new((page_t*) page));
	ut_ad(page_zip_simple_validate(page_zip));
	ut_ad(page_zip_get_size(page_zip)
	      > PAGE_DATA + page_zip_dir_size(page_zip));
	ut_ad(rec_offs_comp(offsets));
	ut_ad(rec_offs_validate(rec, NULL, offsets));
	ut_ad(rec_offs_any_extern(offsets));
	ut_ad(rec_offs_nth_extern(offsets, n));

	ut_ad(page_zip->m_start >= PAGE_DATA);
	ut_ad(page_zip_header_cmp(page_zip, page));

	ut_ad(page_is_leaf(page));
	ut_ad(dict_index_is_clust(index));

	UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));
	UNIV_MEM_ASSERT_RW(rec, rec_offs_data_size(offsets));
	UNIV_MEM_ASSERT_RW(rec - rec_offs_extra_size(offsets),
			   rec_offs_extra_size(offsets));

	blob_no = page_zip_get_n_prev_extern(page_zip, rec, index)
		+ rec_get_n_extern_new(rec, index, n);
	ut_a(blob_no < page_zip->n_blobs);

	if (!page_zip->compact_metadata) {
		externs = page_zip_dir_start(page_zip)
			  - page_zip_dir_elems(page_zip) * DATA_TRX_RBP_LEN;
	} else {
		externs = page_zip_dir_start(page_zip) - 2;
	}

	field = rec_get_nth_field(rec, offsets, n, &len);

	externs -= (blob_no + 1) * BTR_EXTERN_FIELD_REF_SIZE;
	field += len - BTR_EXTERN_FIELD_REF_SIZE;

	memcpy(externs, field, BTR_EXTERN_FIELD_REF_SIZE);

	if (UNIV_UNLIKELY(page_zip_debug)) {
		ut_a(page_zip_validate(page_zip, page, index));
	}

	if (mtr) {
#ifndef UNIV_HOTBACKUP
		page_zip_write_blob_ptr_log(field, externs,
					    externs - page_zip->data, mtr);
#endif /* !UNIV_HOTBACKUP */
	}
}

/**********************************************************************//**
Write the node pointer of a record on a non-leaf compressed page. */
UNIV_INTERN
void
page_zip_write_node_ptr(
/*====================*/
	page_zip_des_t*	page_zip,/*!< in/out: compressed page */
	byte*		rec,	/*!< in/out: record */
	ulint		size,	/*!< in: data size of rec */
	ulint		ptr,	/*!< in: node pointer */
	mtr_t*		mtr)	/*!< in: mini-transaction, or NULL */
{
	byte*	field;
	byte*	storage;
#ifdef UNIV_DEBUG
	page_t*	page	= page_align(rec);
#endif /* UNIV_DEBUG */

	ut_ad(PAGE_ZIP_MATCH(rec, page_zip));
	ut_ad(page_simple_validate_new(page));
	ut_ad(page_zip_simple_validate(page_zip));
	ut_ad(page_zip_get_size(page_zip)
	      > PAGE_DATA + page_zip_dir_size(page_zip));
	ut_ad(page_rec_is_comp(rec));

	ut_ad(page_zip->m_start >= PAGE_DATA);
	ut_ad(page_zip_header_cmp(page_zip, page));

	ut_ad(!page_is_leaf(page));

	UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));
	UNIV_MEM_ASSERT_RW(rec, size);

	storage = page_zip_dir_start(page_zip)
		- (rec_get_heap_no_new(rec) - 1) * REC_NODE_PTR_SIZE;
	field = rec + size - REC_NODE_PTR_SIZE;

	if (0
#ifdef UNIV_DEBUG
			|| 1
#endif
			|| UNIV_UNLIKELY(page_zip_debug)) {
	ut_a(!memcmp(storage, field, REC_NODE_PTR_SIZE));
	}

#if REC_NODE_PTR_SIZE != 4
# error "REC_NODE_PTR_SIZE != 4"
#endif
	mach_write_to_4(field, ptr);
	memcpy(storage, field, REC_NODE_PTR_SIZE);

	if (mtr) {
#ifndef UNIV_HOTBACKUP
          page_zip_write_node_ptr_log(field, storage - page_zip->data, mtr);
#endif /* !UNIV_HOTBACKUP */
	}
}

/**********************************************************************//**
Write the trx_id and roll_ptr of a record on a B-tree leaf node page. */
UNIV_INTERN
void
page_zip_write_trx_id_and_roll_ptr(
/*===============================*/
	page_zip_des_t*	page_zip,/*!< in/out: compressed page */
	byte*		rec,	/*!< in/out: record */
	const ulint*	offsets,/*!< in: rec_get_offsets(rec, index) */
	ulint		trx_id_col,/*!< in: column number of TRX_ID in rec */
	trx_id_t	trx_id,	/*!< in: transaction identifier */
	roll_ptr_t	roll_ptr)/*!< in: roll_ptr */
{
	byte*	field;
	byte*	storage;
#ifdef UNIV_DEBUG
	page_t*	page	= page_align(rec);
#endif /* UNIV_DEBUG */
	ulint	len;

	ut_ad(PAGE_ZIP_MATCH(rec, page_zip));

	ut_ad(page_simple_validate_new(page));
	ut_ad(page_zip_simple_validate(page_zip));
	ut_ad(page_zip_get_size(page_zip)
	      > PAGE_DATA + page_zip_dir_size(page_zip));
	ut_ad(rec_offs_validate(rec, NULL, offsets));
	ut_ad(rec_offs_comp(offsets));

	ut_ad(page_zip->m_start >= PAGE_DATA);
	ut_ad(page_zip_header_cmp(page_zip, page));

	ut_ad(page_is_leaf(page));

	UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));

	storage = page_zip_get_trx_rbp_storage(page_zip, false)
		  - (rec_get_heap_no_new(rec) - 1) * DATA_TRX_RBP_LEN;

#if DATA_TRX_ID + 1 != DATA_ROLL_PTR
# error "DATA_TRX_ID + 1 != DATA_ROLL_PTR"
#endif
	field = rec_get_nth_field(rec, offsets, trx_id_col, &len);
	ut_ad(len == DATA_TRX_ID_LEN);
	ut_ad(field + DATA_TRX_ID_LEN
	      == rec_get_nth_field(rec, offsets, trx_id_col + 1, &len));
	ut_ad(len == DATA_ROLL_PTR_LEN);

	if (0
#ifdef UNIV_DEBUG
			|| 1
#endif
			|| UNIV_UNLIKELY(page_zip_debug)) {
	ut_a(!memcmp(storage, field, DATA_TRX_RBP_LEN));
}

#if DATA_TRX_ID_LEN != 6
# error "DATA_TRX_ID_LEN != 6"
#endif
	mach_write_to_6(field, trx_id);
#if DATA_ROLL_PTR_LEN != 7
# error "DATA_ROLL_PTR_LEN != 7"
#endif
	mach_write_to_7(field + DATA_TRX_ID_LEN, roll_ptr);
	memcpy(storage, field, DATA_TRX_RBP_LEN);

	UNIV_MEM_ASSERT_RW(rec, rec_offs_data_size(offsets));
	UNIV_MEM_ASSERT_RW(rec - rec_offs_extra_size(offsets),
			   rec_offs_extra_size(offsets));
	UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));
}

/**********************************************************************//**
Clear an area on the uncompressed and compressed page.
Do not clear the data payload, as that would grow the modification log. */
static
void
page_zip_clear_rec(
/*===============*/
	page_zip_des_t*	page_zip,	/*!< in/out: compressed page */
	byte*		rec,		/*!< in: record to clear */
	const dict_index_t*	index,	/*!< in: index of rec */
	const ulint*	offsets)	/*!< in: rec_get_offsets(rec, index) */
{
	ulint	heap_no;
	page_t*	page	= page_align(rec);
	byte*	storage;
	byte*	trx_rbp_storage;
	byte*	field;
	ulint	len;
	/* page_zip_validate() would fail here if a record
	containing externally stored columns is being deleted. */
	ut_ad(rec_offs_validate(rec, index, offsets));
	ut_ad(!page_zip_dir_find(page_zip, page_offset(rec)));
	ut_ad(page_zip_dir_find_free(page_zip, page_offset(rec)));
	ut_ad(page_zip_header_cmp(page_zip, page));

	heap_no = rec_get_heap_no_new(rec);
	ut_ad(heap_no >= PAGE_HEAP_NO_USER_LOW);

	UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));
	UNIV_MEM_ASSERT_RW(rec, rec_offs_data_size(offsets));
	UNIV_MEM_ASSERT_RW(rec - rec_offs_extra_size(offsets),
			   rec_offs_extra_size(offsets));

	if (!page_is_leaf(page)) {
		/* Clear node_ptr. On the compressed page,
		there is an array of node_ptr immediately before the
		dense page directory, at the very end of the page. */
		storage	= page_zip_dir_start(page_zip);
		ut_ad(dict_index_get_n_unique_in_tree(index) ==
		      rec_offs_n_fields(offsets) - 1);
		field	= rec_get_nth_field(rec, offsets,
					    rec_offs_n_fields(offsets) - 1,
					    &len);
		ut_ad(len == REC_NODE_PTR_SIZE);

		ut_ad(!rec_offs_any_extern(offsets));
		memset(field, 0, REC_NODE_PTR_SIZE);
		memset(storage - (heap_no - 1) * REC_NODE_PTR_SIZE,
		       0, REC_NODE_PTR_SIZE);
	} else if (dict_index_is_clust(index)) {
		/* Clear trx_id and roll_ptr. On the compressed page,
		there is an array of these fields immediately before the
		dense page directory, at the very end of the page. */
		const ulint	trx_id_pos
			= dict_col_get_clust_pos(
			dict_table_get_sys_col(
				index->table, DATA_TRX_ID), index);
		trx_rbp_storage = page_zip_get_trx_rbp_storage(page_zip, false);
		field	= rec_get_nth_field(rec, offsets, trx_id_pos, &len);
		ut_ad(len == DATA_TRX_ID_LEN);

		memset(field, 0, DATA_TRX_RBP_LEN);
		memset(trx_rbp_storage - (heap_no - 1) * DATA_TRX_RBP_LEN,
		       0,
		       DATA_TRX_RBP_LEN);

		if (rec_offs_any_extern(offsets)) {
			ulint	i;

			for (i = rec_offs_n_fields(offsets); i--; ) {
				/* Clear all BLOB pointers in order to make
				page_zip_validate() pass. */
				if (rec_offs_nth_extern(offsets, i)) {
					field = rec_get_nth_field(
						rec, offsets, i, &len);
					ut_ad(len
					      == BTR_EXTERN_FIELD_REF_SIZE);
					memset(field + len
					       - BTR_EXTERN_FIELD_REF_SIZE,
					       0, BTR_EXTERN_FIELD_REF_SIZE);
				}
			}
		}
	} else {
		ut_ad(!rec_offs_any_extern(offsets));
	}

	if (UNIV_UNLIKELY(page_zip_debug)) {
		ut_a(page_zip_validate(page_zip, page, index));
	}
}

/**********************************************************************//**
Write the "deleted" flag of a record on a compressed page.  The flag must
already have been written on the uncompressed page. */
UNIV_INTERN
void
page_zip_rec_set_deleted(
/*=====================*/
	page_zip_des_t*	page_zip,/*!< in/out: compressed page */
	const byte*	rec,	/*!< in: record on the uncompressed page */
	ulint		flag)	/*!< in: the deleted flag (nonzero=TRUE) */
{
	byte*	slot = page_zip_dir_find(page_zip, page_offset(rec));
	ut_a(slot);
	UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));
	if (flag) {
		*slot |= (PAGE_ZIP_DIR_SLOT_DEL >> 8);
	} else {
		*slot &= ~(PAGE_ZIP_DIR_SLOT_DEL >> 8);
	}
	if (UNIV_UNLIKELY(page_zip_debug)) {
		ut_a(page_zip_validate(page_zip, page_align(rec), NULL));
	}
}

/**********************************************************************//**
Write the "owned" flag of a record on a compressed page.  The n_owned field
must already have been written on the uncompressed page. */
UNIV_INTERN
void
page_zip_rec_set_owned(
/*===================*/
	page_zip_des_t*	page_zip,/*!< in/out: compressed page */
	const byte*	rec,	/*!< in: record on the uncompressed page */
	ulint		flag)	/*!< in: the owned flag (nonzero=TRUE) */
{
	byte*	slot = page_zip_dir_find(page_zip, page_offset(rec));
	ut_a(slot);
	UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));
	if (flag) {
		*slot |= (PAGE_ZIP_DIR_SLOT_OWNED >> 8);
	} else {
		*slot &= ~(PAGE_ZIP_DIR_SLOT_OWNED >> 8);
	}
}

/**********************************************************************//**
Insert a record to the dense page directory. */
UNIV_INTERN
void
page_zip_dir_insert(
/*================*/
	page_zip_des_t*	page_zip,	/*!< in/out: compressed page */
	const byte*	prev_rec,	/*!< in: record after which to insert */
	const byte*	free_rec,	/*!< in: record from which rec was
					allocated, or NULL */
	byte*		rec,		/*!< in: record to insert */
	bool		is_clustered)	/*!< in: clustered index? */
{
	ulint	n_dense;
	byte*	slot_rec;
	byte*	slot_free;

	ut_ad(prev_rec != rec);
	ut_ad(page_rec_get_next((rec_t*) prev_rec) == rec);
	ut_ad(page_zip_simple_validate(page_zip));

	UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));

	if (page_rec_is_infimum(prev_rec)) {
		/* Use the first slot. */
		slot_rec = page_zip->data + page_zip_get_size(page_zip);
	} else {
		slot_rec = page_zip_dir_find(page_zip,
					     page_offset(prev_rec));
		ut_a(slot_rec);
	}

	if (UNIV_LIKELY_NULL(free_rec)) {
		/* The record was allocated from the free list.
		The total number of heap records won't change in this case.
		Shift the dense directory only up to that slot.*/
		ut_ad(rec_get_heap_no_new(rec)
		      < page_dir_get_n_heap(page_zip->data));
		ut_ad(rec >= free_rec);
		slot_free = page_zip_dir_find_free(page_zip,
						   page_offset(free_rec));
		ut_ad(slot_free);
		slot_free += PAGE_ZIP_DIR_SLOT_SIZE;
	} else {
		page_zip_dir_add_slot(page_zip, is_clustered);
		/* The record was allocated from the heap.
		The newly added heap record should have the largest heap_no.
		Shift to the end of the dense page directory. */
		ut_ad(rec_get_heap_no_new(rec)
		      == page_dir_get_n_heap(page_zip->data) - 1);

		n_dense = page_zip_dir_elems(page_zip);
		slot_free = page_zip->data + page_zip_get_size(page_zip)
			    - PAGE_ZIP_DIR_SLOT_SIZE * (n_dense - 1);
	}

	/* Shift the dense directory to allocate place for rec. */
	memmove(slot_free - PAGE_ZIP_DIR_SLOT_SIZE, slot_free,
		slot_rec - slot_free);

	/* Write the entry for the inserted record.
	The "owned" and "deleted" flags must be zero. */
	mach_write_to_2(slot_rec - PAGE_ZIP_DIR_SLOT_SIZE, page_offset(rec));
}

/**********************************************************************//**
Shift the dense page directory and the array of BLOB pointers
when a record is deleted. */
UNIV_INTERN
void
page_zip_dir_delete(
/*================*/
	page_zip_des_t*		page_zip,	/*!< in/out: compressed page */
	byte*			rec,		/*!< in: deleted record */
	const dict_index_t*	index,		/*!< in: index of rec */
	const ulint*		offsets,	/*!< in: rec_get_offsets(rec) */
	const byte*		free)		/*!< in: previous start of
						the free list */
{
	byte*	slot_rec;
	byte*	slot_free;
	ulint	n_ext;
	page_t*	page	= page_align(rec);

	ut_ad(rec_offs_validate(rec, index, offsets));
	ut_ad(rec_offs_comp(offsets));

	UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));
	UNIV_MEM_ASSERT_RW(rec, rec_offs_data_size(offsets));
	UNIV_MEM_ASSERT_RW(rec - rec_offs_extra_size(offsets),
			   rec_offs_extra_size(offsets));

	slot_rec = page_zip_dir_find(page_zip, page_offset(rec));

	ut_a(slot_rec);

	/* This could not be done before page_zip_dir_find(). */
	page_header_set_field(page, page_zip, PAGE_N_RECS,
			      (ulint)(page_get_n_recs(page) - 1));

	if (UNIV_UNLIKELY(!free)) {
		/* Make the last slot the start of the free list. */
		slot_free = page_zip->data + page_zip_get_size(page_zip)
			- PAGE_ZIP_DIR_SLOT_SIZE
			* (page_dir_get_n_heap(page_zip->data)
			   - PAGE_HEAP_NO_USER_LOW);
	} else {
		slot_free = page_zip_dir_find_free(page_zip,
						   page_offset(free));
		ut_a(slot_free < slot_rec);
		/* Grow the free list by one slot by moving the start. */
		slot_free += PAGE_ZIP_DIR_SLOT_SIZE;
	}

	if (UNIV_LIKELY(slot_rec > slot_free)) {
		memmove(slot_free + PAGE_ZIP_DIR_SLOT_SIZE,
			slot_free,
			slot_rec - slot_free);
	}

	/* Write the entry for the deleted record.
	The "owned" and "deleted" flags will be cleared. */
	mach_write_to_2(slot_free, page_offset(rec));

	if (!page_is_leaf(page) || !dict_index_is_clust(index)) {
		ut_ad(!rec_offs_any_extern(offsets));
		goto skip_blobs;
	}

	n_ext = rec_offs_n_extern(offsets);
	if (UNIV_UNLIKELY(n_ext)) {
		ulint	blob_no;
		byte*	externs;
		byte*	ext_end;
		ulint n_dense = page_zip_dir_elems(page_zip);
		ulint old_n_blobs = page_zip->n_blobs;
		ulint new_n_blobs = page_zip->n_blobs - n_ext;

		blob_no = page_zip_get_n_prev_extern(page_zip, rec, index);
		ut_a(blob_no + n_ext <= page_zip->n_blobs);

		externs = page_zip_get_blob_ptr_storage(page_zip);
		if (page_zip->compact_metadata) {
			ut_ad(old_n_blobs == page_zip_read_n_blob(page_zip));
			page_zip_write_n_blob(page_zip, new_n_blobs);
		}

		ext_end = externs
			  - old_n_blobs * BTR_EXTERN_FIELD_REF_SIZE;
		externs -= blob_no * BTR_EXTERN_FIELD_REF_SIZE;

		/* Shift the blob pointers array. */
		memmove(ext_end + n_ext * BTR_EXTERN_FIELD_REF_SIZE,
			ext_end,
			((new_n_blobs - blob_no)
			 * BTR_EXTERN_FIELD_REF_SIZE));
		if (page_zip->compact_metadata) {
			byte*	trx_rbp_storage = NULL;
			byte*	trx_rbp_storage_end;
			/* shift the trx rbp storage because blob storage got
			   smaller */
			trx_rbp_storage = page_zip_get_trx_rbp_storage(page_zip,
								       false);
			trx_rbp_storage_end = trx_rbp_storage
					      - n_dense * DATA_TRX_RBP_LEN;
			memmove(trx_rbp_storage_end
				+ n_ext * BTR_EXTERN_FIELD_REF_SIZE,
				trx_rbp_storage_end,
				n_dense * DATA_TRX_RBP_LEN);
			/* zerofill the old unused tail of the
			   trx_rbp_storage */
			memset(trx_rbp_storage_end,
			       0,
			       n_ext * BTR_EXTERN_FIELD_REF_SIZE);
		} else {
			/* zerofill the remaining parts of the blob storage */
			memset(ext_end, 0, n_ext * BTR_EXTERN_FIELD_REF_SIZE);
		}
		page_zip->n_blobs = new_n_blobs;
	}

skip_blobs:
	/* The compression algorithm expects info_bits and n_owned
	to be 0 for deleted records. */
	rec[-REC_N_NEW_EXTRA_BYTES] = 0; /* info_bits and n_owned */

	page_zip_clear_rec(page_zip, rec, index, offsets);
}

/**********************************************************************//**
Reorganize and compress a page.  This is a low-level operation for
compressed pages, to be used when page_zip_compress() fails.
On success, a redo log entry MLOG_ZIP_PAGE_COMPRESS will be written.
The function btr_page_reorganize() should be preferred whenever possible.
IMPORTANT: if page_zip_reorganize() is invoked on a leaf page of a
non-clustered index, the caller must update the insert buffer free
bits in the same mini-transaction in such a way that the modification
will be redo-logged.
@return TRUE on success, FALSE on failure; page_zip will be left
intact on failure, but page will be overwritten. */
UNIV_INTERN
ibool
page_zip_reorganize(
/*================*/
	buf_block_t*	block,	/*!< in/out: page with compressed page;
				on the compressed page, in: size;
				out: data, n_blobs,
				m_start, m_end, m_nonempty */
	dict_index_t*	index,	/*!< in: index of the B-tree node */
	mtr_t*		mtr)	/*!< in: mini-transaction */
{
#ifndef UNIV_HOTBACKUP
	buf_pool_t*	buf_pool	= buf_pool_from_block(block);
#endif /* !UNIV_HOTBACKUP */
	page_zip_des_t*	page_zip	= buf_block_get_page_zip(block);
	page_t*		page		= buf_block_get_frame(block);
	buf_block_t*	temp_block;
	page_t*		temp_page;
	ulint		log_mode;

	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));
	ut_ad(page_is_comp(page));
	ut_ad(!dict_index_is_ibuf(index));
	/* Note that page_zip_validate(page_zip, page, index) may fail here. */
	UNIV_MEM_ASSERT_RW(page, UNIV_PAGE_SIZE);
	UNIV_MEM_ASSERT_RW(page_zip->data, page_zip_get_size(page_zip));

	/* Disable logging */
	log_mode = mtr_set_log_mode(mtr, MTR_LOG_NONE);

#ifndef UNIV_HOTBACKUP
	temp_block = buf_block_alloc(buf_pool);
	btr_search_drop_page_hash_index(block);
	block->check_index_page_at_flush = TRUE;
#else /* !UNIV_HOTBACKUP */
	ut_ad(block == back_block1);
	temp_block = back_block2;
#endif /* !UNIV_HOTBACKUP */
	temp_page = temp_block->frame;

	/* Copy the old page to temporary space */
	buf_frame_copy(temp_page, page);

	btr_blob_dbg_remove(page, index, "zip_reorg");

	/* Recreate the page: note that global data on page (possible
	segment headers, next page-field, etc.) is preserved intact */

	page_create(block, mtr, TRUE);

	/* Copy the records from the temporary space to the recreated page;
	do not copy the lock bits yet */

	page_copy_rec_list_end_no_locks(block, temp_block,
					page_get_infimum_rec(temp_page),
					index, mtr);

	if (!dict_index_is_clust(index) && page_is_leaf(temp_page)) {
		/* Copy max trx id to recreated page */
		trx_id_t	max_trx_id = page_get_max_trx_id(temp_page);
		page_set_max_trx_id(block, NULL, max_trx_id, NULL);
		ut_ad(max_trx_id != 0);
	}

	/* Restore logging. */
	mtr_set_log_mode(mtr, log_mode);

	if (!page_zip_compress(page_zip, page, index,
			       page_zip_compression_flags, mtr)) {

#ifndef UNIV_HOTBACKUP
		buf_block_free(temp_block);
#endif /* !UNIV_HOTBACKUP */
		return(FALSE);
	}

	lock_move_reorganize_page(block, temp_block);

#ifndef UNIV_HOTBACKUP
	buf_block_free(temp_block);
#endif /* !UNIV_HOTBACKUP */
	return(TRUE);
}

#ifndef UNIV_HOTBACKUP
/**********************************************************************//**
Copy the records of a page byte for byte.  Do not copy the page header
or trailer, except those B-tree header fields that are directly
related to the storage of records.  Also copy PAGE_MAX_TRX_ID.
NOTE: The caller must update the lock table and the adaptive hash index. */
UNIV_INTERN
void
page_zip_copy_recs(
/*===============*/
	page_zip_des_t*		page_zip,	/*!< out: copy of src_zip
						(n_blobs, m_start, m_end,
						m_nonempty, data[0..size-1]) */
	page_t*			page,		/*!< out: copy of src */
	const page_zip_des_t*	src_zip,	/*!< in: compressed page */
	const page_t*		src,		/*!< in: page */
	dict_index_t*		index,		/*!< in: index of the B-tree */
	mtr_t*			mtr)		/*!< in: mini-transaction */
{
	ut_ad(mtr_memo_contains_page(mtr, page, MTR_MEMO_PAGE_X_FIX));
	ut_ad(mtr_memo_contains_page(mtr, src, MTR_MEMO_PAGE_X_FIX));
	ut_ad(!dict_index_is_ibuf(index));

	if (UNIV_UNLIKELY(page_zip_debug)) {
		/* The B-tree operations that call this function may set
		FIL_PAGE_PREV or PAGE_LEVEL, causing a temporary min_rec_flag
		mismatch.  A strict page_zip_validate() will be executed later
		during the B-tree operations. */
		ut_a(page_zip_validate_low(src_zip, src, index, TRUE));
	}

	ut_a(page_zip_get_size(page_zip) == page_zip_get_size(src_zip));
	if (UNIV_UNLIKELY(src_zip->n_blobs)) {
		ut_a(page_is_leaf(src));
		ut_a(dict_index_is_clust(index));
	}

	/* The PAGE_MAX_TRX_ID must be set on leaf pages of secondary
	indexes.  It does not matter on other pages. */
	ut_a(dict_index_is_clust(index) || !page_is_leaf(src)
	     || page_get_max_trx_id(src));

	UNIV_MEM_ASSERT_W(page, UNIV_PAGE_SIZE);
	UNIV_MEM_ASSERT_W(page_zip->data, page_zip_get_size(page_zip));
	UNIV_MEM_ASSERT_RW(src, UNIV_PAGE_SIZE);
	UNIV_MEM_ASSERT_RW(src_zip->data, page_zip_get_size(page_zip));

	/* Copy those B-tree page header fields that are related to
	the records stored in the page.  Also copy the field
	PAGE_MAX_TRX_ID.  Skip the rest of the page header and
	trailer.  On the compressed page, there is no trailer. */
#if PAGE_MAX_TRX_ID + 8 != PAGE_HEADER_PRIV_END
# error "PAGE_MAX_TRX_ID + 8 != PAGE_HEADER_PRIV_END"
#endif
	memcpy(PAGE_HEADER + page, PAGE_HEADER + src,
	       PAGE_HEADER_PRIV_END);
	memcpy(PAGE_DATA + page, PAGE_DATA + src,
	       UNIV_PAGE_SIZE - PAGE_DATA - FIL_PAGE_DATA_END);
	memcpy(PAGE_HEADER + page_zip->data, PAGE_HEADER + src_zip->data,
	       PAGE_HEADER_PRIV_END);
	memcpy(PAGE_DATA + page_zip->data, PAGE_DATA + src_zip->data,
	       page_zip_get_size(page_zip) - PAGE_DATA);

	/* Copy all fields of src_zip to page_zip, except the pointer
	to the compressed data page. */
	{
		page_zip_t*	data = page_zip->data;
		memcpy(page_zip, src_zip, sizeof *page_zip);
		page_zip->data = data;
	}
	ut_ad(page_zip_get_trailer_len(page_zip, dict_index_is_clust(index))
	      + page_zip->m_end < page_zip_get_size(page_zip));

	if (!page_is_leaf(src)
	    && UNIV_UNLIKELY(mach_read_from_4(src + FIL_PAGE_PREV) == FIL_NULL)
	    && UNIV_LIKELY(mach_read_from_4(page
					    + FIL_PAGE_PREV) != FIL_NULL)) {
		/* Clear the REC_INFO_MIN_REC_FLAG of the first user record. */
		ulint	offs = rec_get_next_offs(page + PAGE_NEW_INFIMUM,
						 TRUE);
		if (UNIV_LIKELY(offs != PAGE_NEW_SUPREMUM)) {
			rec_t*	rec = page + offs;
			ut_a(rec[-REC_N_NEW_EXTRA_BYTES]
			     & REC_INFO_MIN_REC_FLAG);
			rec[-REC_N_NEW_EXTRA_BYTES] &= ~ REC_INFO_MIN_REC_FLAG;
		}
	}

	if (UNIV_UNLIKELY(page_zip_debug)) {
		ut_a(page_zip_validate(page_zip, page, index));
	}

	btr_blob_dbg_add(page, index, "page_zip_copy_recs");

	page_zip_compress_write_log(page_zip, page, index, mtr);
}
#endif /* !UNIV_HOTBACKUP */