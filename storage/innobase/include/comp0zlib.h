/*****************************************************************************
Copyright (c) 2013, Facebook Inc.

These thin wrappers around zlib functions provide the following:
1- Callers of comp_zlib_compress/comp_zlib_decompress do not have to deal
with the initialization of the z_stream objects or the zlib options other than
those specified in the parameters below.
2- Callers of comp_zlib_* functions are guaranteed to use the embedded
zlib copy under storage/innobase/include/zlib_embedded even when external
zlib libraries are linked for use in other parts of mysql or in xtrabackup.

*****************************************************************************/

#ifndef comp0zlib_h
#define comp0zlib_h

int comp_zlib_compress(
	const unsigned char* in, /* input buffer with data to be compressed */
	unsigned int avail_in, /* size of the input buffer */
	unsigned char* out, /* output buffer */
	unsigned int avail_out, /* available space in the output buffer */
	unsigned long* total_out, /* size of the compressed output */
	void* (*zalloc) (void*, unsigned int, unsigned int), /* used to allocate
								the internal
								state */
	void (*zfree) (void*, void*), /* used to free the internal state */
	void* opaque, /* private data object passed to zalloc and zfree */
	int level, /* see zlib manual */
	int window_bits, /* see zlib.h */
	int strategy); /* see zlib.h */

int comp_zlib_decompress(
	const unsigned char* in, /* input buffer with compressed data */
	unsigned int avail_in, /* the length of the input buffer */
	unsigned char* out, /* output buffer */
	unsigned int avail_out, /* available space in the output buffer */
	unsigned long* total_in, /* size of the input buffer used for
				    decompression */
	void* (*zalloc) (void*, unsigned int, unsigned int), /* used to allocate
								the internal
								state */
	void (*zfree) (void*, void*), /* used to free the internal state */
	void* opaque, /* private data object passed to zalloc and zfree */
	int window_bits); /* see zlib.h */

#endif /* comp0zlib_h */
