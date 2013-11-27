#ifndef quicklz_level1_h
#define quicklz_level1_h
#include "string.h"

#ifdef __cplusplus
extern "C" {
#endif

size_t innobase_qlz_size_decompressed1(const char* source);
size_t innobase_qlz_size_compressed1(const char* source);
size_t innobase_qlz_compress1(const void* source, char *destination,
			      size_t size, void* state);
size_t innobase_qlz_decompress1(const char* source, void* destination,
				void* state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* quicklz_level1_h */
