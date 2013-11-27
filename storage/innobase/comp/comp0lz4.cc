#include "lz4_embedded/lz4.h"
#include "lz4_embedded/lz4hc.h"
#include "comp0types.h"

/*****************************************************************************
The auxiliary memory required by LZ4_embedded_compress() is as follows:

  sizeof(struct refTables)
  = sizeof(HTYPE) * HASHTABLESIZE
  = 4 * (1 << (14 - 2))

See lz4.c for details */
#define COMP_LZ4_MEM_AUX (4UL * (1UL << (14 - 2)))

/*****************************************************************************
The auxiliary memory required by LZ4_embedded_compressHC() is as follows:

  sizeof(LZ4HC_Data_Structure)
  = sizeof(BYTE*) + sizeof(HTYPE) * HASHTABLESIZE + sizeof(U16) * MAXD
    + sizeof(BYTE*)
  = 2 * sizeof(char*) + 4 * (1 << 15) + 2 * (1 << 16)

See lz4hc.c for details */
#define COMP_LZ4HC_MEM_AUX \
        (2UL * sizeof(char*) + 4UL * (1UL << 15) + 2UL * (1UL << 16))

/*****************************************************************************
Compress the contents of the buffer comp_state->in into the output buffer
comp_state->out using LZ4. Update the avail_in, avail_out fields of
comp_state after compression.
@return 1 on success 0 on failure. */
int
comp_lz4_compress(
	comp_state_t* comp_state) /* in: in, avail_in, avail_out, level.
				     out: out, avail_in, avail_out */
{
	unsigned int comp_len; /* length of compressed output */
	uchar level = comp_state->level;
	char* out; /* compressed output buffer */
	void* ctx; /* scratch space for LZ4 */
	level = ((level > 2) || !level) ? 2 : level;
	ut_a(comp_state->avail_in <= 0xffffUL);
	/* first two bytes are used to store the length of
	   the uncompressed input */
	if (comp_state->avail_out <= 2)
		return 0;
	out = (char*)comp_state->out + 2;

	if (level == 2) {
		/* use LZ4HC which has better compression rate but is slower.
		   LZ4HC needs temporary buffer because it doesn't perform
		   boundary checking for the output buffer. */
		unsigned int comp_len_max = LZ4_COMPRESSBOUND(
						comp_state->avail_in);
		/* allocate memory for LZ4 to use as scratch space */
		ctx = mem_heap_alloc(comp_state->heap, COMP_LZ4HC_MEM_AUX);
		/* in case the compressed output may be larger than the
		   available space, use a temporary buffer. Note that
		   comp_len_max is only an upperbound on the size of the
		   compressed output. The actual output size may be smaller
		   than this and in that case we copy the contents of out
		   into comp_state->out. */
		if (comp_len_max > comp_state->avail_out - 2) {
			out = (char*)mem_heap_alloc(comp_state->heap,
						    comp_len_max);
		}
		comp_len = LZ4_embedded_compressHC((const char*)comp_state->in,
					  (char*)out,
					  (int)comp_state->avail_in, ctx);
		if (comp_len + 2 > (unsigned int)comp_state->avail_out) {
			ut_a(comp_len_max
			     > (unsigned int)comp_state->avail_out - 2);
			return 0;
		}
		/* copy the output to comp_state->out + 2 if it was allocated
		   from comp_state->heap */
		if (comp_len_max > comp_state->avail_out - 2) {
			memcpy(comp_state->out + 2, out, comp_len);
		}
	} else {
		/* use default LZ4 which is faster but has worse compression
		   rate. Also it doesn't require a temporary buffer because it
		   performs boundary checks itself */

		/* allocate memory for LZ4 to use as scratch space */
		ctx = mem_heap_alloc(comp_state->heap, COMP_LZ4_MEM_AUX);
		comp_len = LZ4_embedded_compress_limitedOutput(
				(char*)comp_state->in,
				(char*)comp_state->out + 2,
				(int)comp_state->avail_in,
				(int)(comp_state->avail_out - 2),
				ctx);
		if (!comp_len)
			return 0;
		ut_a(comp_len + 2 <= comp_state->avail_out);
	}
	/* store the uncompressed length in the output buffer */
	comp_state->out[0] = (unsigned char) (comp_state->avail_in >> 8);
	comp_state->out[1] = (unsigned char) (comp_state->avail_in);
	/* update avail_in and avail_out of comp_state */
	comp_state->avail_in = 0;
	comp_state->avail_out -= comp_len + 2;
	return 1;
}

/*****************************************************************************
Decompress the contents of the buffer comp_state->in into the output buffer
comp_state->out using LZ4. Update the avail_in, avail_out fields of
comp_state after decompression. Failure of decompression means page corruption
or a bug so this function does not have a return value. */
void
comp_lz4_decompress(
	comp_state_t* comp_state) /* in: in, avail_in, avail_out, level.
				     out: out, avail_out, avail_in */
{
	int comp_len;
	int uncomp_len = (((int)comp_state->in[0]) << 8)
			 | (int)comp_state->in[1];
	ut_a(uncomp_len <= (int)comp_state->avail_out);
	comp_len = LZ4_embedded_uncompress((char*)comp_state->in + 2,
				  (char*)comp_state->out,
				  uncomp_len);
	ut_a(comp_len > 0);
	ut_a(comp_len + 2 <= (int)comp_state->avail_in);
	comp_state->avail_in -= comp_len + 2;
	comp_state->avail_out -= uncomp_len;
}

/***************************************************************//**
LZ4 decompression does not need any additional memory.
LZ4 compression does not need an intermediate output buffer because it performs
the boundary checks on the output buffer itself.
LZ4HC compression needs an intermediate buffer of size
LZ4_COMPRESSBOUND(UNIV_PAGE_SIZE) because it does not perform
boundary checks on the output buffer.
Other than the intermediate buffer, the total memory required by
LZ4_embedded_compressHC() is COMP_LZ4HC_MEM_AUX.
The additional memory required by LZ4_embedded_compress() is COMP_LZ4_MEM_AUX */
static comp_interface_t comp_lz4_obj = {
	comp_lz4_compress,
	comp_lz4_decompress,
	ut_max(LZ4_COMPRESSBOUND(UNIV_PAGE_SIZE) + COMP_LZ4HC_MEM_AUX,
	       COMP_LZ4_MEM_AUX),
	0
};

comp_interface_t* comp_lz4 = &comp_lz4_obj;
