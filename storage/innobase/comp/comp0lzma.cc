/*****************************************************************************
Copyright (c) 2013, Facebook Inc.

Thin wrappers around lzma API to make it easy to use and make sure that innodb
uses the bundled lzma even when another lzma is linked while building mysql
or xtrabackup.
*****************************************************************************/
#include "lzma_embedded/LzmaLib.h"
#include "comp0types.h"

/**********************************************************************//**
Allocate memory for lzma. */
static
void*
comp_lzma_alloc(
/*============*/
	void*   opaque, /*!< in/out: memory heap */
	size_t  size)   /*!< in: size of an item in bytes */
{
	void* m = mem_heap_alloc(
			static_cast<mem_heap_t*>(opaque), size);
	UNIV_MEM_VALID(m, size);
	return(m);
}

/**********************************************************************//**
Free memory function which does nothing. */
static
void
comp_lzma_free(
	void* opaque __attribute__((unused)), /*!< in: memory heap */
	void* address __attribute__((unused))) /*!< in: object to free */
{
}

/*****************************************************************************
Compress the contents of the buffer comp_state->in into the output buffer
comp_state->out using lzma. Update the avail_in, avail_out fields of comp_state
after compression.
@return 1 on success 0 on failure. */
int
comp_lzma_compress(
	comp_state_t* comp_state) /* in: in, avail_in, avail_out, level.
				     out: out, avail_out */
{
	int level = (comp_state->level == 2) ? 1 : 0;
	size_t props_size = LZMA_PROPS_SIZE;
	size_t destLen;
	ISzAlloc alloc = {comp_lzma_alloc, comp_lzma_free, comp_state->heap};

	if (comp_state->avail_out <= LZMA_PROPS_SIZE)
		return 0;

	destLen = (size_t)(comp_state->avail_out - LZMA_PROPS_SIZE);

	/* We will use the first LZMA_PROPS_SIZE bytes of comp_state->out to
	   encode the comression parameters by passing it to LzmaCompress as the
	   outProps parameter. The actual compressed output starts from
	   comp_state->out + LZMA_PROPS_SIZE */
	if (LzmaCompress(comp_state->out + LZMA_PROPS_SIZE, &destLen,
			 comp_state->in, (size_t)comp_state->avail_in,
			 comp_state->out, &props_size, level, 0, -1, -1, -1,
			 -1, 1, &alloc)
	    == SZ_OK) {
		ut_a(props_size == LZMA_PROPS_SIZE);
		ut_a(destLen + LZMA_PROPS_SIZE <= comp_state->avail_out);
		comp_state->avail_out -= destLen + LZMA_PROPS_SIZE;
		comp_state->avail_in = 0;
		return 1;
	}
	return 0;
}

/*****************************************************************************
Decompress the contents of the buffer comp_state->in into the output buffer
comp_state->out using lzma. Update the avail_in, avail_out fields of comp_state
after decompression. Failure of decompression means page corruption or a bug so
this function does not have a return value. */
void
comp_lzma_decompress(
	comp_state_t* comp_state) /* in: in, avail_in, avail_out, level.
				     out: out, avail_out, avail_in */
{
	size_t destLen = (size_t)comp_state->avail_out;
	SizeT srcLen;
	ISzAlloc alloc = {comp_lzma_alloc, comp_lzma_free, comp_state->heap};
	ut_a(comp_state->avail_in > LZMA_PROPS_SIZE);
	srcLen = comp_state->avail_in - LZMA_PROPS_SIZE;
	ut_a(LzmaUncompress(comp_state->out, &destLen,
			    comp_state->in + LZMA_PROPS_SIZE, &srcLen,
			    comp_state->in, LZMA_PROPS_SIZE, &alloc) == SZ_OK);
	ut_a(destLen <= comp_state->avail_out);
	comp_state->avail_out -= destLen;
	ut_a(srcLen + LZMA_PROPS_SIZE <= comp_state->avail_in);
	comp_state->avail_in -= srcLen + LZMA_PROPS_SIZE;
}

/***************************************************************//**
For additional memory needed by lzma, see LzmaLib.h. We let compression level
be 0 or 1. This means that for compression, the dictionary size is going to be
at most 64K. For decompression, we take dictSize=32k and state_size=16K.
The calculations below are based on the comments in LzmaLib.h. */
static comp_interface_t comp_lzma_obj = {
	comp_lzma_compress,
	comp_lzma_decompress,
	(64UL << 10) * 12UL + (6UL << 20) + (16UL << 10),
	(32UL << 10)  + (16UL << 10)
};

comp_interface_t* comp_lzma = &comp_lzma_obj;
