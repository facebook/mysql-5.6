/*****************************************************************************
Copyright (c) 2013, Facebook Inc.

Thin wrappers around lzma API to make it easy to use and make sure that innodb
uses the bundled lzma even when another lzma is linked while building mysql
or xtrabackup.
*****************************************************************************/
#include "snappy_embedded/snappy.h"
#include "comp0types.h"

/**********************************************************************//**
Allocate memory for snappy. */
static
void*
comp_snappy_alloc(
/*============*/
       void*   opaque, /*!< in/out: memory heap */
       size_t  size)   /*!< in: size of an item in bytes */
{
       void* m = mem_heap_alloc(
                       static_cast<mem_heap_t*>(opaque), size);
       UNIV_MEM_VALID(m, size);
       return(m);
}

/*****************************************************************************
Compress the contents of the buffer comp_state->in into the output buffer
comp_state->out using snappy. Update the avail_in, avail_out fields of
comp_state after compression.
@return 1 on success 0 on failure. */
int
comp_snappy_compress(
	comp_state_t* comp_state) /* in: in, avail_in, avail_out, level.
				     out: out, avail_out */
{
	size_t comp_len;
	void* out = NULL;
	size_t comp_len_max = innobase_snappy::MaxCompressedLength(
							comp_state->avail_in);
	/* We use two bytes to store the length of the compressed output */
	if (comp_state->avail_out <= 2) {
		return 0;
	}
	/* in case the compressed output may be larger than the available space,
	   use a temporary buffer. Note that comp_len_max is only an upperbound
	   on the size of the compressed output. The actual output size may be
	   smaller than this and in that case we copy the contents of out
	   into comp_state->out. */
	if (comp_len_max > comp_state->avail_out - 2) {
		out = mem_heap_alloc(comp_state->heap, comp_len_max);
	} else {
		out = comp_state->out + 2;
	}

	comp_len = comp_state->avail_out - 2;
	innobase_snappy::RawCompress((char*)comp_state->in,
				     comp_state->avail_in,
				     (char*)out, &comp_len, comp_snappy_alloc,
				     comp_state->heap);

	/* Return 0 if the length of the compressed output is greater than the
	   size of the output buffer minos the space needed to write the
	   compressed size */
	if (comp_len > comp_state->avail_out - 2) {
		return 0;
	}

	/* Record the compressed length in first two bytes */
	comp_state->out[0] = (unsigned char)(comp_len >> 8);
	comp_state->out[1] = (unsigned char)(comp_len);
	comp_state->avail_in = 0;
	comp_state->avail_out -= comp_len + 2;
	/* Copy the contents of out into comp_state->out + 2 if out was
	   allocated from the heap above */
	if (out != comp_state->out + 2) {
		memcpy(comp_state->out + 2, out, comp_len);
	}
	return 1;
}


/*****************************************************************************
Decompress the contents of the buffer comp_state->in into the output buffer
comp_state->out using snappy. Update the avail_in, avail_out fields of
comp_state after decompression. Failure of decompression means page corruption
or a bug so this function does not have a return value. */
void
comp_snappy_decompress(
	comp_state_t* comp_state) /* in: in, avail_in, avail_out, level.
				     out: out, avail_out, avail_in */
{
	size_t uncomp_len;
	size_t comp_len;
	ut_a(comp_state->avail_in >= 2);
	/* Read two bytes to recover the length of the compressed output */
	comp_len = (((size_t)comp_state->in[0]) << 8)
		   | ((size_t)comp_state->in[1]);
	ut_a(comp_len + 2 <= comp_state->avail_in);
	ut_a(innobase_snappy::GetUncompressedLength((char*)comp_state->in + 2,
						    comp_len,
						    &uncomp_len));
	ut_a(uncomp_len <= comp_state->avail_out);
	ut_a(innobase_snappy::RawUncompress((char*)comp_state->in + 2, comp_len,
					    (char*)comp_state->out));
	comp_state->avail_in -= comp_len + 2;
	comp_state->avail_out -= uncomp_len;
}

 /***************************************************************//**
Snappy decompression does not require additional memory. For the additional
memory needed by snappy's compression we compute the memory needed by
Compress() in snappy.cc below: Allocated variables are as follows:

  table = wmem.GetHashTable(...)
  // sizeof(uint16) * (1 << 10) + sizeof(uint16) * kMaxHashTableSize
  scratch = allocator->Alloc(..., num_to_read)
  // num_to_read is less than or equal to kBlockSize which is 1 << 15
  scratch_output = allocator->Alloc(..., max_output)
  // max_output = MaxCompressedLength(num_to_read)
  // = 32 + num_to_read + num_to_read / 6

In addition to the above, we need to reserve memory for the temporary
output buffer created in comp_snappy_compress(). The max size
for this variable is snappy::MaxCompressedLengt(UNIV_PAGE_SIZE) */
static comp_interface_t comp_snappy_obj = {
	comp_snappy_compress,
	comp_snappy_decompress,
	sizeof(uint16) * (1UL << 10) /* snappy::GetHashTable() */
	+ sizeof(uint16) * (1UL << 14) /* snappy::GetHashTable() */
	+ (1UL << 15) /* allocation for scratch in snappy::Compress() */
	+ innobase_snappy::MaxCompressedLength(1UL << 15)
	/* allocation for scratch_output in snappy::Compress() */
	+ innobase_snappy::MaxCompressedLength(UNIV_PAGE_SIZE)
	/* allocation for out in comp_snappy_compress() */,
	0 /* no additional memory needed for snappy's RawUncompress() */
};

comp_interface_t* comp_snappy = &comp_snappy_obj;
