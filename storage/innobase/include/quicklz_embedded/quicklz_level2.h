#ifndef quicklz_level2_h
#define quicklz_level2_h
#include "string.h"

#ifdef __cplusplus
extern "C" {
#endif

size_t innobase_qlz_size_decompressed2(const char *source);
size_t innobase_qlz_size_compressed2(const char *source);
size_t innobase_qlz_compress2(const void *source, char *destination,
			      size_t size, void* state);
size_t innobase_qlz_decompress2(const char *source, void *destination,
				void* state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* quicklz_level2_h */
