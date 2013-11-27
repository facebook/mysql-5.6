#include "comp0types.h"
#include "quicklz_embedded/quicklz_level1.h"
#include "quicklz_embedded/quicklz_level2.h"
#include "quicklz_embedded/quicklz_level3.h"

#define COMP_QUICKLZ_MEM_COMPRESS 528392UL
#define COMP_QUICKLZ_MEM_DECOMPRESS 67592UL

#define QLZ_ASSIGN(level, val, f, args) \
	do { \
		switch (level) { \
		case 1: \
			val = innobase_ ## f ## 1 args; \
			break; \
		case 2: \
			val = innobase_ ## f ## 2 args; \
			break; \
		case 3: \
			val = innobase_ ## f ## 3 args; \
			break; \
		default: \
			val = -1; \
			ut_error; \
			break; \
		} \
	} while (0)

/*****************************************************************************
Compress the contents of the buffer comp_state->in into the output buffer
comp_state->out using quicklz. Update the avail_in, avail_out fields of
comp_state after compression.
@return 1 on success 0 on failure. */
int
comp_quicklz_compress(
	comp_state_t* comp_state) /* in: in, avail_in, avail_out, level.
				     out: out, avail_out */
{
	size_t comp_len = 0;
	char* out = (char*)comp_state->out;
	size_t comp_len_max = comp_state->avail_in + 400;
	uchar level = comp_state->level;
	void* state = mem_heap_alloc(comp_state->heap,
				     COMP_QUICKLZ_MEM_COMPRESS);
	level = ((level > 3) || !level) ? 3 : level;
	/* in the case when the compressed output may be larger than the
	   available space, use a temporary buffer for output. Note that
	   comp_len_max is only an upperbound on the size of the compressed
	   output. The actual output size may be smaller than this and in that
	   case we copy the contents of out into comp_state->out. */
	if (comp_len_max > comp_state->avail_out) {
		out = (char*)mem_heap_alloc(comp_state->heap, comp_len_max);
	}

	QLZ_ASSIGN(level, comp_len, qlz_compress, (comp_state->in, out,
						   comp_state->avail_in,
						   state));

	if (comp_len > comp_state->avail_out) {
		ut_a(out != (char*)comp_state->out);
		return 0;
	}

	/* Copy the contents of out if it was allocated from heap */
	if (out != (char*)comp_state->out) {
		memcpy(comp_state->out, out, comp_len);
	}

	comp_state->avail_out -= comp_len;
	comp_state->avail_in = 0;
	return 1;
}

/*****************************************************************************
Decompress the contents of the buffer comp_state->in into the output buffer
comp_state->out using quicklz. Update the avail_in, avail_out fields of
comp_state after decompression. Failure of decompression means page corruption
or a bug so this function does not have a return value. */
void
comp_quicklz_decompress(
	comp_state_t* comp_state) /* in: in, avail_in, avail_out, level.
				     out: out, avail_out, avail_in */
{
	size_t comp_len;
	size_t uncomp_len;
	uchar level = comp_state->level;
	void* state = mem_heap_alloc(comp_state->heap,
				     COMP_QUICKLZ_MEM_DECOMPRESS);
	level = ((level > 3) || !level) ? 3 : level;

	QLZ_ASSIGN(level, comp_len, qlz_size_compressed,
		   ((char*)comp_state->in));
	ut_a(comp_len <= comp_state->avail_in);
	QLZ_ASSIGN(level, uncomp_len, qlz_decompress,
		   ((char*)comp_state->in, comp_state->out, state));
	assert(uncomp_len <= comp_state->avail_out);
	comp_state->avail_out -= uncomp_len;
	comp_state->avail_in -= comp_len;
}


/***************************************************************//**
For the additional memory needed by quicklz_compress() and quicklz_decompress()
see http://www.quicklz.com/manual.html.
For compression we also require an additional memory of size
UNIV_PAGE_SIZE + 400 which we use for the temporary output buffer if there is a
chance that the compressed output would not fit into the provided output buffer.
*/
static comp_interface_t comp_quicklz_obj = {
	comp_quicklz_compress,
	comp_quicklz_decompress,
	COMP_QUICKLZ_MEM_COMPRESS /* for qlz_compress() */
	+ UNIV_PAGE_SIZE + 400, /* for out in comp_quicklz_compress() */
	COMP_QUICKLZ_MEM_DECOMPRESS /* for qlz_decompress() */
};

comp_interface_t* comp_quicklz = &comp_quicklz_obj;
