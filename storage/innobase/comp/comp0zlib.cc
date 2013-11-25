#include "zlib_embedded/zlib.h"
#include "comp0types.h"

/* Below macros are used to calculate the memory requirement for zlib's
compression/decompression streams */
#define DEFLATE_MEMORY_BOUND(windowBits, memLevel) \
    ((4UL << (windowBits)) + (512UL << (memLevel)) + (128UL << 10))

#define INFLATE_MEMORY_BOUND(windowBits) \
    ((1UL << (windowBits)) + (128UL << 10))

/**********************************************************************//**
Allocate memory for zlib. */
static
void*
comp_zlib_alloc(
/*============*/
	void* opaque, /*!< in/out: memory heap */
	uInt items, /*!< in: number of items to allocate */
	uInt size) /*!< in: size of an item in bytes */
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
comp_zlib_free(
	void* opaque __attribute__((unused)), /*!< in: memory heap */
	void* address __attribute__((unused))) /*!< in: object to free */
{
}
/*****************************************************************************
Compress the contents of the buffer comp_state->in into the output buffer
comp_state->out using zlib. Update the avail_in, avail_out fields of comp_state
after compression.
@return 1 on success 0 on failure. */
int
comp_zlib_compress(
	comp_state_t* comp_state) /* in: in, avail_in, avail_out, level.
				     out: out, avail_out */
{
	int err;
	z_stream c_stream;
	c_stream.next_in = comp_state->in;
	c_stream.avail_in = comp_state->avail_in;
	c_stream.next_out = comp_state->out;
	c_stream.avail_out = comp_state->avail_out;
	c_stream.zalloc = comp_zlib_alloc;
	c_stream.zfree = comp_zlib_free;
	c_stream.opaque = comp_state->heap;
	err = deflateInit2(&c_stream, comp_state->level,
			   Z_DEFLATED, -(int)UNIV_PAGE_SIZE_SHIFT,
			   MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
	ut_a(err == Z_OK);
	err = deflate(&c_stream, Z_FINISH);
	ut_a(Z_OK == deflateEnd(&c_stream));
	ut_a(c_stream.avail_out + c_stream.total_out == comp_state->avail_out);
	ut_a(err != Z_STREAM_END || c_stream.avail_in == 0);
	comp_state->avail_out = c_stream.avail_out;
	comp_state->avail_in = c_stream.avail_in;
	return (err == Z_STREAM_END);
}
/*****************************************************************************
Decompress the contents of the buffer comp_state->in into the output buffer
comp_state->out using zlib. Update the avail_in, avail_out fields of comp_state
after decompression. Failure of decompression means page corruption or a bug so
this function does not have a return value. */
void
comp_zlib_decompress(
	comp_state_t* comp_state) /* in: in, avail_in, avail_out, level.
				     out: out, avail_out, avail_in */
{
	z_stream d_stream;
	d_stream.next_in = comp_state->in;
	d_stream.avail_in = comp_state->avail_in;
	d_stream.next_out = comp_state->out;
	d_stream.avail_out = comp_state->avail_out;
	d_stream.zalloc = comp_zlib_alloc;
	d_stream.zfree = comp_zlib_free;
	d_stream.opaque = comp_state->heap;
	ut_a(Z_OK == inflateInit2(&d_stream, -(int)UNIV_PAGE_SIZE_SHIFT));
	ut_a(Z_STREAM_END  == inflate(&d_stream, Z_FINISH));
	ut_a(Z_OK == inflateEnd(&d_stream));
	ut_a(d_stream.avail_in + d_stream.total_in == comp_state->avail_in);
	comp_state->avail_in = d_stream.avail_in;
	comp_state->avail_out = d_stream.avail_out;
}

/* For additional memory needed by zlib see zlib manual
   http://www.zlib.net/manual.html */
static comp_interface_t comp_zlib_obj = {
	comp_zlib_compress,
	comp_zlib_decompress,
	DEFLATE_MEMORY_BOUND(UNIV_PAGE_SIZE_SHIFT,
			     MAX_MEM_LEVEL),
	INFLATE_MEMORY_BOUND(UNIV_PAGE_SIZE_SHIFT)
};

comp_interface_t* comp_zlib = &comp_zlib_obj;
