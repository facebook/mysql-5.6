/*
   Copyright (c) 2016, Facebook, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include <new>
#include <stdexcept>
#include <vector>

#include <zstd.h>

#include "my_sys.h"
#include "sql/malloc_allocator.h"

static constexpr int compression_level = 3;  // ZSTD_CLEVEL_DEFAULT;

using byte_buffer = std::vector<uchar, Malloc_allocator<uchar>>;

class compressor {
 public:
  compressor(IO_CACHE *info, File file, size_t cachesize, cache_type type,
             my_off_t seek_offset, bool use_async_io, myf cache_myflags)
      : zstd_in_buf(ZSTD_CStreamInSize(),
                    Malloc_allocator<uchar>(PSI_NOT_INSTRUMENTED)),
        zstd_out_buf(ZSTD_CStreamOutSize(),
                     Malloc_allocator<uchar>(PSI_NOT_INSTRUMENTED)),
        cstream{ZSTD_createCStream()} {
    if (!cstream) throw std::bad_alloc();

    size_t zrc = ZSTD_initCStream(cstream.get(), compression_level);
    if (ZSTD_isError(zrc)) throw std::bad_alloc();

    if (init_io_cache(&cache, file, cachesize, type, seek_offset, use_async_io,
                      cache_myflags))
      throw std::runtime_error("error initializing file cache");
    info->write_pos = zstd_in_buf.data();
    info->write_end = zstd_in_buf.data() + zstd_in_buf.size();
  }

  compressor(const compressor &) = delete;
  compressor(compressor &&) = delete;
  compressor &operator=(const compressor &) = delete;
  compressor &operator=(compressor &&) = delete;

  int write(IO_CACHE *info, const uchar *buf, size_t length) noexcept {
    size_t buflen = info->write_pos - zstd_in_buf.data();

    if (compress_write_buffer(info, zstd_in_buf.data(), buflen)) return 1;

    info->write_pos = zstd_in_buf.data();

    if (length > zstd_in_buf.size()) {
      if (compress_write_buffer(info, buf, length)) return 1;
    } else {
      memcpy(info->write_pos, buf, length);
      info->write_pos += length;
    }
    return 0;
  }

  int end(IO_CACHE *info) noexcept {
    size_t buflen = info->write_pos - zstd_in_buf.data();
    int rc1 = compress_write_buffer(info, zstd_in_buf.data(), buflen);
    int rc2 = flush(info);
    int rc3 = end_io_cache(&cache);

    return rc1 || rc2 || rc3;
  }

  static int io_cache_compressor_write(IO_CACHE *info, const uchar *buf,
                                       size_t length) {
    if (info->compressor->write(info, buf, length)) return 1;
    return 0;
  }

 private:
  int compress_write_buffer(IO_CACHE *info, const uchar *buf,
                            size_t buflen) noexcept {
    ZSTD_inBuffer input = {buf, buflen, 0};
    while (input.pos < input.size) {
      ZSTD_outBuffer output = {zstd_out_buf.data(), zstd_out_buf.size(), 0};
      size_t zrc = ZSTD_compressStream(cstream.get(), &output, &input);
      if (ZSTD_isError(zrc)) {
        info->error = -1;
        return 1;
      }
      if (write_compressed_data(info, zstd_out_buf.data(), output.pos))
        return 1;
    }
    return 0;
  }

  int write_compressed_data(IO_CACHE *info, const uchar *buf,
                            size_t size) noexcept {
    if (size > 0 && my_b_write(&cache, buf, size)) {
      info->error = cache.error;
      return 1;
    }
    return 0;
  }

  int flush(IO_CACHE *info) noexcept {
    ZSTD_outBuffer output = {zstd_out_buf.data(), zstd_out_buf.size(), 0};
    size_t zrc = ZSTD_endStream(cstream.get(), &output);
    if (ZSTD_isError(zrc)) {
      info->error = -1;
      return 1;
    }
    if (write_compressed_data(info, zstd_out_buf.data(), output.pos)) return 1;
    return 0;
  }

  byte_buffer zstd_in_buf;
  byte_buffer zstd_out_buf;
  struct ZSTD_CStream_deleter {
    void operator()(ZSTD_CStream *ptr) const noexcept { ZSTD_freeCStream(ptr); }
  };
  using ZSTD_CStream_ptr = std::unique_ptr<ZSTD_CStream, ZSTD_CStream_deleter>;
  ZSTD_CStream_ptr cstream;
  IO_CACHE cache;
};

int end_io_cache_compressor(IO_CACHE *info) {
  int r = info->compressor->end(info);
  delete info->compressor;
  info->compressor = nullptr;
  return r;
}

int init_io_cache_with_opt_compression(IO_CACHE *info, File file,
                                       size_t cachesize, cache_type type,
                                       my_off_t seek_offset, bool use_async_io,
                                       myf cache_myflags, bool compressed) {
  if (!compressed)
    return init_io_cache(info, file, cachesize, type, seek_offset, use_async_io,
                         cache_myflags);

  if (type != WRITE_CACHE || seek_offset != 0L) {
    // we support compression only of WRITE_CACHE
    info->error = -1;
    return 1;
  }

  *info = IO_CACHE{};

  try {
    cachesize = 0L;  // ignore the "select_into_buffer_size" parameter
    info->compressor = new compressor(info, file, cachesize, type, seek_offset,
                                      use_async_io, cache_myflags);
  } catch (const std::bad_alloc &) {
    info->error = -1;
    return 1;
  } catch (const std::exception &) {
    return 1;
  }

  info->file = -1;
  info->type = type;
  info->end_of_file = ~static_cast<my_off_t>(0);
  info->write_function = &compressor::io_cache_compressor_write;
  return 0;
}
