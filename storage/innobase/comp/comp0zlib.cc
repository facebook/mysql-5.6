#include "comp0zlib.h"
#include "zlib_embedded/zlib.h"
#include <assert.h>

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
	int strategy) /* see zlib.h */
{
	int err;
	z_stream c_stream;
	c_stream.next_in = (unsigned char*)in;
	c_stream.avail_in = avail_in;
	c_stream.next_out = out;
	c_stream.avail_out = avail_out;
	c_stream.zalloc = zalloc;
	c_stream.zfree = zfree;
	c_stream.opaque = opaque;
	err = deflateInit2(&c_stream, level,
			   Z_DEFLATED, window_bits,
			   MAX_MEM_LEVEL, strategy);
	assert(err == Z_OK);
	err = deflate(&c_stream, Z_FINISH);
	assert(Z_OK == deflateEnd(&c_stream));
	*total_out = c_stream.total_out;
	return (err == Z_STREAM_END);
}

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
	int window_bits) /* see zlib.h */
{
	z_stream d_stream;
	int err;
	d_stream.next_in = (unsigned char*)in;
	d_stream.avail_in = avail_in;
	d_stream.next_out = out;
	d_stream.avail_out = avail_out;
	d_stream.zalloc = zalloc;
	d_stream.zfree = zfree;
	d_stream.opaque = opaque;
	err = inflateInit2(&d_stream, window_bits);
	assert(err == Z_OK);
	err = inflate(&d_stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		err = inflateEnd(&d_stream);
		assert(err == Z_OK);
		return 0;
	}
	err = inflateEnd(&d_stream);
	assert(err == Z_OK);
	*total_in = d_stream.total_in;
	return 1;
}
