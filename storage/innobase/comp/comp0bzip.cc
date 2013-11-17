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

#include "comp0bzip.h"
#include "bzip_embedded/bzlib.h"
#include <assert.h>

/*****************************************************************************
Compress the contents of the buffer "in" into the output buffer "out" using
bzip. Store the length of compressed output in *total_out. A failure of this
function means insufficient space for the compressed output.
@return 1 on success 0 on failure. */
int comp_bzip_compress(
	const unsigned char* in, /* input buffer with data to be compressed */
	unsigned int avail_in, /* size of the input buffer */
	unsigned char* out, /* output buffer */
	unsigned int avail_out, /* available space in the output buffer */
	unsigned long* total_out, /* size of the compressed output */
	void *(*bzalloc)(void *, int, int), /* used to allocate the internal
					     state */
	void (*bzfree)(void *, void *), /* used to free the internal state */
	void *opaque, /* private data object passed to bzalloc and bzfree */
	int verbosity, /* see bzlib.h */
	int workFactor) /* see bzlib.h */
{
	bz_stream strm;
	int err;
	strm.next_in = (char*)in;
	strm.avail_in = avail_in;
	strm.next_out = (char*)out;
	strm.avail_out = avail_out;
	/* Tell bzip to allocate memory from heap */
	strm.bzalloc = bzalloc;
	strm.bzfree = bzfree;
	strm.opaque = opaque;
	/* Initialize bzip. We use a 100k block because our pages are always
	   going to be smaller than 100k */
	assert(BZ_OK == BZ2_bzCompressInit(&strm, 1, verbosity, workFactor));
	/* compress the serialized buffer */
	err = BZ2_bzCompress(&strm, BZ_FINISH);
	assert(BZ_OK == BZ2_bzCompressEnd(&strm));
	*total_out = (unsigned long)strm.total_out_lo32;
	return (err == BZ_STREAM_END);
}

/*****************************************************************************
Decompress the contents of the buffer "in" into the output buffer "out" using
bzip. Store the length of the actual input used in *total_in. A failure of
this function may mean data corruption or some other bug.
@return 1 on success 0 on failure. */
int comp_bzip_decompress(
	const unsigned char* in, /* input buffer with data to be decompressed */
	unsigned int avail_in, /* max size of the input buffer */
	unsigned char* out, /* output buffer */
	unsigned int avail_out, /* available space in the output buffer */
	unsigned long* total_in, /* size of the input buffer used for
				    decompression */
	void *(*bzalloc)(void *, int, int), /* used to allocate the internal
					     state */
	void (*bzfree)(void *, void *), /* used to free the internal state */
	void *opaque, /* private data object passed to bzalloc and bzfree */
	int verbosity) /* see bzlib.h */
{
	bz_stream strm;
	int err;
	strm.next_in = (char*)in;
	strm.avail_in = avail_in;
	strm.next_out = (char*)out;
	strm.avail_out = avail_out;
	/* Tell bzip to allocate memory from heap */
	strm.bzalloc = bzalloc;
	strm.bzfree = bzfree;
	strm.opaque = opaque;
	/* Initialize decompression stream. We set the parameter small to zero
	   because we want decompress to be fast. Memory saving is not that
	   important here because we cache the memory allocation used for
	   compression/decompression opearations */
	assert(BZ_OK == BZ2_bzDecompressInit(&strm, verbosity, 0));
	err = BZ2_bzDecompress(&strm);
	assert(BZ_OK == BZ2_bzDecompressEnd(&strm));
	if (err != BZ_STREAM_END) {
		return 0;
	}
	*total_in = (unsigned long)strm.total_in_lo32;
	return 1;
}
