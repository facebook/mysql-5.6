/*****************************************************************************
Copyright (c) 2013, Facebook Inc.

These thin wrappers around compression libraries the following:
1- Callers of comp_compress/comp_decompress do not have to deal with the
initialization of the compression or decompression objects or other compression
parameters, other than input, output, and, compression level which are stored
in the comp_state variable.
2- Callers of comp_ functions are guaranteed to use the embedded compression
libraries under storage/innobase/<lib>_embedded even when external versions
of these libraries are linked for use in other parts of mysql or in xtrabackup.
*****************************************************************************/
#ifndef comp0comp_h
#define comp0comp_h
#include "comp0types.h"
/*****************************************************************************
Compress the contents of the buffer comp_state->in into the output buffer
comp_state->out using the compression comp_type which is one of
DICT_TF_COMP_ZLIB, DICT_TF_COMP_BZIP, DICT_TF_COMP_LZMA, DICT_TF_COMP_SNAPPY,
DICT_TF_COMP_QUICKLZ, or DICT_TF_COMP_LZ4. Update the avail_in, avail_out
fields of comp_state after compression.
@return 1 on success 0 on failure. */
unsigned int
comp_compress(
	uchar comp_type, /* compression type as defined in dict0mem.h */
	comp_state_t* comp_state); /* input/output and memory. see
				      comp0types.h */
/*****************************************************************************
Decompress the contents of the buffer comp_state->in into the output buffer
comp_state->out using the compression comp_type which is one of
DICT_TF_COMP_ZLIB, DICT_TF_COMP_BZIP, DICT_TF_COMP_LZMA, DICT_TF_COMP_SNAPPY,
DICT_TF_COMP_QUICKLZ, or DICT_TF_COMP_LZ4. Update the avail_in, avail_out
fields of comp_state after decompression. Failure of decompression means page
corruption or a bug so this function does not have a return value. */
void
comp_decompress(
	uchar comp_type, /* compression type as defined in dict0mem.h */
	comp_state_t* comp_state); /* input/output and memory. see
				      comp0types.h */
/*****************************************************************************
Return the amount of auxiliary memory needed by the specific compression type's
compress() function.
@return the amount of memory needed by compression for compression_type. */
unsigned long
comp_mem_compress(
	uchar comp_type);
/*****************************************************************************
Return the amount of auxiliary memory needed by the specific compression type's
decompress() function.
@return the amount of memory needed by decompression for compression_type. */
unsigned long
comp_mem_decompress(
	uchar comp_type);
#endif /* comp0comp_h */
