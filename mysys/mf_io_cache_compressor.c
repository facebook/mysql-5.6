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

#include <mysys_priv.h>
#include <zstd.h>

static const int compression_level = 3; // ZSTD_CLEVEL_DEFAULT;

typedef struct compressor {
  uchar *zstd_in_buf;
  size_t zstd_in_buf_size;
  uchar *zstd_out_buf;
  size_t zstd_out_buf_size;
  ZSTD_CStream *cstream;
  IO_CACHE cache;
} compressor;

static void destroy_compressor(compressor *c) {
  if (c->cstream)
    ZSTD_freeCStream(c->cstream);
  if (c->zstd_in_buf)
    my_free(c->zstd_in_buf);
  if (c->zstd_out_buf)
    my_free(c->zstd_out_buf);
  my_free(c);
}

static int destroy_compressor_and_set_error(IO_CACHE *info, compressor *c) {
  info->error = -1;
  destroy_compressor(c);
  return 1;
}

static int init_io_cache_compressor(IO_CACHE *info) {
  compressor *c =
      (compressor *)my_malloc(sizeof(compressor), MYF(MY_WME | MY_ZEROFILL));
  if (!c) {
    info->error = -1;
    return 1;
  }
  c->zstd_in_buf_size = ZSTD_CStreamInSize();
  c->zstd_out_buf_size = ZSTD_CStreamOutSize();
  c->zstd_in_buf = (uchar *)my_malloc(c->zstd_in_buf_size, MYF(MY_WME));
  c->zstd_out_buf = (uchar *)my_malloc(c->zstd_out_buf_size, MYF(MY_WME));
  if (!c->zstd_in_buf || !c->zstd_out_buf)
    return destroy_compressor_and_set_error(info, c);

  c->cstream = ZSTD_createCStream();
  if (!c->cstream)
    return destroy_compressor_and_set_error(info, c);

  size_t zrc = ZSTD_initCStream(c->cstream, compression_level);
  if (ZSTD_isError(zrc))
    return destroy_compressor_and_set_error(info, c);

  info->compressor = c;
  return 0;
}

static int write_compressed_data(IO_CACHE *info, const uchar *buf,
                                 size_t size) {
  IO_CACHE *compressor_info = &info->compressor->cache;
  if (size > 0 && my_b_write(compressor_info, buf, size)) {
    info->error = compressor_info->error;
    return 1;
  }
  return 0;
}

static int flush_compressor(IO_CACHE *info) {
  compressor *c = info->compressor;
  uchar *zstd_out_buf = c->zstd_out_buf;
  size_t zstd_out_buf_size = c->zstd_out_buf_size;
  ZSTD_outBuffer output = {zstd_out_buf, zstd_out_buf_size, 0};
  size_t zrc = ZSTD_endStream(c->cstream, &output);
  if (zrc) {
    info->error = -1;
    return 1;
  }
  if (write_compressed_data(info, zstd_out_buf, output.pos))
    return 1;
  return 0;
}

static int compress_write_buffer(IO_CACHE *info,
                                 const uchar *buf, size_t buflen) {
  compressor *c = info->compressor;
  ZSTD_CStream *cstream = c->cstream;
  uchar *zstd_out_buf = c->zstd_out_buf;
  size_t zstd_out_buf_size = c->zstd_out_buf_size;

  ZSTD_inBuffer input = {buf, buflen, 0};
  while (input.pos < input.size) {
    ZSTD_outBuffer output = {zstd_out_buf, zstd_out_buf_size, 0};
    size_t zrc = ZSTD_compressStream(cstream, &output, &input);
    if (ZSTD_isError(zrc)) {
      info->error = -1;
      return 1;
    }
    if (write_compressed_data(info, zstd_out_buf, output.pos))
      return 1;
  }
  return 0;
}

static int io_cache_compressor_write(IO_CACHE *info, const uchar *buf,
                                     size_t length) {
  compressor *c = info->compressor;
  size_t buflen = info->write_pos - c->zstd_in_buf;

  if (compress_write_buffer(info, c->zstd_in_buf, buflen))
    return 1;

  info->write_pos = c->zstd_in_buf;

  if (length > c->zstd_in_buf_size) {
    if (compress_write_buffer(info, buf, length))
      return 1;
  } else {
    memcpy(info->write_pos, buf, length);
    info->write_pos += length;
  }
  return 0;
}

int end_io_cache_compressor(IO_CACHE *info) {
  compressor *c = info->compressor;
  size_t buflen = info->write_pos - c->zstd_in_buf;

  int rc1 = compress_write_buffer(info, c->zstd_in_buf, buflen);
  int rc2 = flush_compressor(info);
  int rc3 = end_io_cache(&c->cache);
  destroy_compressor(c);
  info->compressor = 0;
  return rc1 || rc2 || rc3;
}

int init_io_cache_ex(IO_CACHE *info, File file, size_t cachesize,
                     enum cache_type type, my_off_t seek_offset,
                     pbool use_async_io, myf cache_myflags,
                     my_bool compressed) {
  if (!compressed)
    return init_io_cache(info, file, cachesize, type, seek_offset, use_async_io,
                         cache_myflags);

  if (type != WRITE_CACHE || cachesize != 0L || seek_offset != 0L) {
    // we support compression only of WRITE_CACHE
    info->error = -1;
    return 1;
  }

  memset(info, 0, sizeof(IO_CACHE));

  if (init_io_cache_compressor(info))
    return 1;

  if (init_io_cache(&info->compressor->cache, file, cachesize, type,
                    seek_offset, use_async_io, cache_myflags)) {
    destroy_compressor(info->compressor);
    info->compressor = 0;
    return 1;
  }

  info->file = -1;
  info->type = type;
  info->end_of_file = ~(my_off_t)0;
  info->write_function = io_cache_compressor_write;
  compressor *c = info->compressor;
  info->write_pos = c->zstd_in_buf;
  info->write_end = c->zstd_in_buf + c->zstd_in_buf_size;
  return 0;
}
