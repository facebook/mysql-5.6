/*****************************************************************************
Copyright (c) 2013, Facebook Inc.

These thin wrappers around bzip functions provide the following:
1- Callers of comp_bzip_compress/comp_bzip_decompress do not have to deal
with the initialization of the bz_stream objects or the bzip options other than
those specified in the parameters below.
2- Callers of comp_bzip_* functions are guaranteed to use the embedded
bzip copy under storage/innobase/include/bzip_embedded even when external
bzip libraries are linked for use in other parts of mysql or in xtrabackup.

*****************************************************************************/
#include "bzip_embedded/bzlib.h"
#include "comp0types.h"

/**********************************************************************//**
Allocate memory for bzip. The only difference from zlib is that the integer
arguments are signed rather than unsigned. */
static
void*
comp_bzip_alloc(
/*============*/
	void*   opaque, /*!< in/out: memory heap */
	int     items,  /*!< in: number of items to allocate */
	int     size)   /*!< in: size of an item in bytes */
{
	void* m = mem_heap_alloc(static_cast<mem_heap_t*>(opaque),
				 items * size);
	UNIV_MEM_VALID(m, items * size);
	return m;
}

/**********************************************************************//**
Free memory function which does nothing. */
static
void
comp_bzip_free(
	void* opaque __attribute__((unused)), /*!< in: memory heap */
	void* address __attribute__((unused))) /*!< in: object to free */
{
}

/*****************************************************************************
Compress the contents of the buffer comp_state->in into the output buffer
comp_state->out using bzip. Update the avail_in, avail_out fields of comp_state
after compression.
@return 1 on success 0 on failure. */
int
comp_bzip_compress(
	comp_state_t* comp_state) /* in: in, avail_in, avail_out, level.
				     out: out, avail_in, avail_out */
{
	bz_stream strm;
	int err;
	/* If level > 15 or level == 0 we default to level = 1 */
	uchar level = comp_state->level;
	level = ((level > 15) || (!level)) ? 1 : level;
	strm.next_in = (char*)comp_state->in;
	strm.avail_in = comp_state->avail_in;
	strm.next_out = (char*)comp_state->out;
	strm.avail_out = comp_state->avail_out;
	/* Tell bzip to allocate memory from heap */
	strm.bzalloc = comp_bzip_alloc;
	strm.bzfree = comp_bzip_free;
	strm.opaque = comp_state->heap;
	/* Initialize bzip. We use a 100k block because our pages are always
	   going to be smaller than 100k */
	ut_a(BZ_OK == BZ2_bzCompressInit(&strm, 1, 0, level));
	/* compress the serialized buffer */
	err = BZ2_bzCompress(&strm, BZ_FINISH);
	ut_a(BZ_OK == BZ2_bzCompressEnd(&strm));
	ut_a(strm.total_out_lo32 + strm.avail_out == comp_state->avail_out);
	comp_state->avail_out = strm.avail_out;
	comp_state->avail_in = strm.avail_in;
	return (err == BZ_STREAM_END);
}

/*****************************************************************************
Decompress the contents of the buffer comp_state->in into the output buffer
comp_state->out using bzip. Update the avail_in, avail_out fields of comp_state
after decompression. Failure of decompression means page corruption or a bug so
this function does not have a return value. */
void
comp_bzip_decompress(
	comp_state_t* comp_state) /* in: in, avail_in, avail_out, level.
				     out: out, avail_in, avail_out */
{
	bz_stream strm;
	strm.next_in = (char*)comp_state->in;
	strm.avail_in = comp_state->avail_in;
	strm.next_out = (char*)comp_state->out;
	strm.avail_out = comp_state->avail_out;
	/* Tell bzip to allocate memory from heap */
	strm.bzalloc = comp_bzip_alloc;
	strm.bzfree = comp_bzip_free;
	strm.opaque = comp_state->heap;
	/* Initialize decompression stream. We set the parameter small to zero
	   because we want decompress to be fast. Memory saving is not that
	   important here because we cache the memory allocation used for
	   compression/decompression opearations */
	ut_a(BZ_OK == BZ2_bzDecompressInit(&strm, 0, 0));
	ut_a(BZ_STREAM_END == BZ2_bzDecompress(&strm));
	ut_a(BZ_OK == BZ2_bzDecompressEnd(&strm));
	ut_a(strm.total_in_lo32 + strm.avail_in == comp_state->avail_in);
	comp_state->avail_in = strm.avail_in;
	comp_state->avail_out = strm.avail_out;
}

/* For additional memory requirements for bzip see
   http://www.bzip.org/1.0.3/bzip2-manual-1.0.3.html#memory-management */
static comp_interface_t comp_bzip_obj = {
	comp_bzip_compress,
	comp_bzip_decompress,
	1200UL << 10,
	500UL << 10
};

comp_interface_t* comp_bzip = &comp_bzip_obj;
