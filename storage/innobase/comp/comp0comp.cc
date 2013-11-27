#include "comp0comp.h"
#include "dict0mem.h"

extern comp_interface_t* comp_zlib; /* compression interface for zlib */
extern comp_interface_t* comp_bzip; /* compression interface for bzip */
extern comp_interface_t* comp_lzma; /* compression interface for lzma */
extern comp_interface_t* comp_snappy; /* compression interface for snappy */

/* The index of each compression type below must correspond to its definition in
   dict0mem.h */
static comp_interface_t* comp_interfaces[] = {
	NULL,
	comp_zlib, /* zlib */
	comp_bzip, /* bzip */
	comp_lzma, /* lzma */
	comp_snappy, /* snappy */
	comp_zlib, /* TODO: replace with comp_quicklz once implemented */
	comp_zlib}; /* TODO: replace with comp_lz4 once implemented */
/*****************************************************************************
Compress the contents of the buffer comp_state->in into the output buffer
comp_state->out using the compression comp_type which is one of
DICT_TF_COMP_ZLIB, DICT_TF_COMP_BZIP, DICT_TF_COMP_LZMA, DICT_TF_COMP_SNAPPY,
DICT_TF_COMP_QUICKLZ, or DICT_TF_COMP_LZ4. Update the avail_in, avail_out
fields of comp_state after compression.
@return 1 on success 0 on failure. */
unsigned int
comp_compress(
	uchar comp_type, /* in compression type from dict0mem.h */
	comp_state_t* comp_state) /* in: in, avail_in, avail_out.
				     out: out, avail_out */
{
	ut_a(comp_type <= DICT_TF_COMP_MAX);
	return comp_interfaces[comp_type]->compress(comp_state);
}
/*****************************************************************************
Decompress the contents of the buffer comp_state->in into the output buffer
comp_state->out using the compression comp_type which is one of
DICT_TF_COMP_ZLIB, DICT_TF_COMP_BZIP, DICT_TF_COMP_LZMA, DICT_TF_COMP_SNAPPY,
DICT_TF_COMP_QUICKLZ, or DICT_TF_COMP_LZ4. Update the avail_in, avail_out
fields of comp_state after decompression. Failure of decompression means page
corruption or a bug so this function does not have a return value. */
void
comp_decompress(
	uchar comp_type, /* in compression type from dict0mem.h */
	comp_state_t* comp_state) /* in: in, avail_in, avail_out.
				     out: out, avail_out, avail_in */
{
	ut_a(comp_type <= DICT_TF_COMP_MAX);
	comp_interfaces[comp_type]->decompress(comp_state);
}
/*****************************************************************************
Return the amount of auxiliary memory needed by the specific compression type's
compress() function.
@return the amount of memory needed by compression for compression_type. */
unsigned long
comp_mem_compress(
	uchar comp_type)
{
	ut_a(comp_type <= DICT_TF_COMP_MAX);
	return MEM_SPACE_NEEDED(comp_interfaces[comp_type]->mem_compress);
}
/*****************************************************************************
Return the amount of auxiliary memory needed by the specific compression type's
decompress() function.
@return the amount of memory needed by decompression for compression_type. */
unsigned long
comp_mem_decompress(
	uchar comp_type)
{
	ut_a(comp_type <= DICT_TF_COMP_MAX);
	return MEM_SPACE_NEEDED(comp_interfaces[comp_type]->mem_decompress);
}
