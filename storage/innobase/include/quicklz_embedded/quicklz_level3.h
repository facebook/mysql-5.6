#ifndef quicklz_level3_h
#define quicklz_level3_h
#include "string.h"

#ifdef __cplusplus
extern "C" {
#endif

size_t innobase_qlz_size_decompressed3(const char *source);
size_t innobase_qlz_size_compressed3(const char *source);
size_t innobase_qlz_compress3(const void *source, char *destination,
			      size_t size, void* state);
size_t innobase_qlz_decompress3(const char *source, void *destination,
				void* state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* quicklz_level1_h */
