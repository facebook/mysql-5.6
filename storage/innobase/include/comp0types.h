#ifndef comp0types_h
#define comp0types_h
#include "mem0mem.h"

/* comp_state_t objects are used as input/output arguments to compression or
   decompression functions. Any compression/decompression function must read
   the contents of comp_state->in and compress/decompress it into
   comp_state->out. After this process comp_state->avail_in and
   comp_state->avail_out must be updated accordingly. comp_state->level
   is passed to the compression library as a parameter. Higher compression
   levels correspond to smaller compressed sizes but slower compressions. */
typedef struct {
	unsigned char* in; /* input buffer */
	unsigned long avail_in; /* size of the input buffer */
	unsigned char* out; /* output buffer */
	unsigned long avail_out; /* size of the output buffer */
	unsigned char level; /* the compression level which should be
				passed to the compression library */
	mem_heap_t* heap; /* temporary heap used by compression libraries
			     to allocate objects */
} comp_state_t;

/* The interface for all compression libraries. Each compression library must
   provide the following:
   - a compress() function which can fail if the output size is too small
   - a decompress() function failure of which means data corruption. So it
   can cause a crash if the decompression does not succeed.
   - the amount of auxilary memory needed for compression
   - the amount of auxilary memory needed for decompression */
typedef struct {
	int (*compress) (comp_state_t*); /* return 1 on success 0 on failure */
	void (*decompress) (comp_state_t*); /* decompress must always succeed */
	unsigned long mem_compress; /* amount of memory needed for compress() */
	unsigned long mem_decompress; /* amount of memory needed for
					 decompress() */
} comp_interface_t;

#endif /* comp0types_h */
