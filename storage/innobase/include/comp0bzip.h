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

#ifndef comp0bzip_h
#define comp0bzip_h

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
	int workFactor); /* see bzlib.h */

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
	int verbosity); /* see bzlib.h */

#endif /* comp0bzip_h */
