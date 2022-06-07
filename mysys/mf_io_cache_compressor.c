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

typedef struct decompressor {
  uchar *zstd_out_buf;      // buffer for decompressed data
  size_t zstd_out_buf_size;
  ZSTD_DStream *dstream;    // decompressor
  ZSTD_inBuffer input;      // pointer to data source buffer
  my_bool internal_buffer_fully_flushed; // if EOF in data source,
                                // but decompressor's internal buffer
                                // still keep some data to decompress
                                // because output buffer is not big enough
  IO_CACHE cache;           // data source
} decompressor;

static void destroy_compressor(compressor *c) {
  if (c->cstream)
    ZSTD_freeCStream(c->cstream);
  if (c->zstd_in_buf)
    my_free(c->zstd_in_buf);
  if (c->zstd_out_buf)
    my_free(c->zstd_out_buf);
  my_free(c);
}

static void destroy_decompressor(decompressor *d) {
  if (d->dstream)
    ZSTD_freeDStream(d->dstream);
  if (d->zstd_out_buf)
    my_free(d->zstd_out_buf);
  my_free(d);
}


static int destroy_compressor_and_set_error(IO_CACHE *info, compressor *c) {
  info->error = -1;
  destroy_compressor(c);
  return 1;
}

static int destroy_decompressor_and_set_error(IO_CACHE *c, decompressor *d) {
  c->error = -1;
  destroy_decompressor(d);
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

static int io_cache_decompressor_read(IO_CACHE *info,
                                      uchar *Buffer, size_t Count);
static int init_io_cache_decompressor(IO_CACHE *info);

int init_io_cache_ex(IO_CACHE *info, File file, size_t cachesize,
                     enum cache_type type, my_off_t seek_offset,
                     pbool use_async_io, myf cache_myflags,
                     my_bool compressed) {
  if (!compressed)
    return init_io_cache(info, file, cachesize, type, seek_offset, use_async_io,
                         cache_myflags);

  memset(info, 0, sizeof(IO_CACHE));

  if (type == WRITE_CACHE && cachesize == 0L && seek_offset == 0L) {

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
    info->pos_in_file = info->compressor->cache.pos_in_file;
    info->request_pos = info->compressor->cache.request_pos;
    info->current_pos = info->compressor->cache.current_pos;

  } else if ((type == READ_NET || type == READ_FIFO || type == READ_CACHE)
            && cachesize == 0L && seek_offset == 0L) {

    if (init_io_cache_decompressor(info))
      return 1;

    if (init_io_cache(&info->decompressor->cache, file, cachesize, type,
                      seek_offset, use_async_io, cache_myflags)) {
      destroy_decompressor(info->decompressor);
      info->decompressor = 0;
      return 1;
    }

    info->file = -1;
    info->type = type;
    info->end_of_file = ~(my_off_t)0;
    info->read_function = io_cache_decompressor_read;
    info->request_pos = info->read_pos = info->read_end =
        info->decompressor->zstd_out_buf;

  } else {
    return 1;
  }
  return 0;
}

static int init_io_cache_decompressor(IO_CACHE *info) {
  decompressor *d =
      (decompressor *)my_malloc(sizeof(decompressor), MYF(MY_WME));
  if (!d) {
    info->error = -1;
    return 1;
  }
  d->dstream = ZSTD_createDStream();
  if (!d->dstream) {
    destroy_decompressor_and_set_error(info, d);
    return 1;
  }
  size_t zrc = ZSTD_initDStream(d->dstream);
  if (ZSTD_isError(zrc)) {
    destroy_decompressor_and_set_error(info, d);
    return 1;
  }
  d->zstd_out_buf_size = ZSTD_DStreamOutSize();
  d->zstd_out_buf = (uchar *)my_malloc(d->zstd_out_buf_size, MYF(MY_WME));
  if (!d->zstd_out_buf) {
    destroy_decompressor_and_set_error(info, d);
    return 1;
  }
  d->input.pos = 0;
  d->input.size = 0;
  d->input.src = NULL;
  d->internal_buffer_fully_flushed = TRUE;
  info->decompressor = d;
  return 0;
}

int end_io_cache_decompressor(IO_CACHE *info) {
  decompressor *d = info->decompressor;
  int rc = end_io_cache(&d->cache);
  destroy_decompressor(d);
  info->decompressor = 0;
  return rc;
}

static int io_cache_decompressor_cache_read(decompressor *d)
{
  IO_CACHE *dc = &d->cache;
  // read data into buffer
  int rc = _my_b_get(dc);
  if (rc == my_b_EOF) {
    return 1; // EOF
  }
  // put back one byte read by _my_b_get()
  dc->read_pos--;

  uchar *in_buf = dc->read_pos;
  size_t in_buf_size = dc->read_end - dc->read_pos;
  // set read buffer consumed
  dc->read_pos = dc->read_end;

  DBUG_ASSERT(in_buf_size > 0);

  // set input buffer for decompression
  d->input.pos = 0;
  d->input.size = in_buf_size;
  d->input.src = in_buf;
  return 0;
}
// io_cache_decompressor_read() called from _my_b_get().
// _m_b_get() reads one byte at time.
// If IOCACHE buffer is empty, _m_b_get() calls read_function.
// LOAD INFILE sets read_function = _my_b_net_read()
// LOAD INFILE LOCAL set read_function = _my_b_read()
// LOAD INFILE [ LOCAL ] COMPRESSED sets
// read_function = io_cache_decompressor_read()
// We assume count == 1 because the READ_INFO code
// only calls my_b_get via the GET macro, and _my_b_net_read
// and io_cache_decompressor_read is making use of that.
static int io_cache_decompressor_read(IO_CACHE *info,
                                      uchar *Buffer,
                                      size_t Count MY_ATTRIBUTE((unused))) {
  decompressor *d = info->decompressor;
  // _my_b_get() passes Count = 1
  DBUG_ASSERT(Count == 1);
  // output buffer must be empty, all data from there is consumed
  DBUG_ASSERT(info->read_pos == info->read_end);
  size_t read_data_length;
  while (1) {
    // if there is some data in decompressor's internal buffer, we flush it
    // if there is some data in input buffer, we decompress it
    // otherwise we read data from input source and decompress it

    // if decompressor internal empty && input buffers are empty, read input
    if (d->internal_buffer_fully_flushed && d->input.pos == d->input.size
        && io_cache_decompressor_cache_read(d))
      return 1; // EOF
    // set output buffer for decompression
    ZSTD_outBuffer output = {d->zstd_out_buf, d->zstd_out_buf_size, 0};
    size_t rc = ZSTD_decompressStream(d->dstream, &output, &d->input);
    if (ZSTD_isError(rc)) {
      info->error = -1;
      return 1;
    }
    // check decompressor's internal buffer is fully flushed
    d->internal_buffer_fully_flushed = output.pos < output.size;
    if (output.pos > 0) {
      read_data_length = output.pos;
      break;
    }
    // output.pos == 0, output buffer is empty
  }
  // set output buffer pointers
  info->request_pos = info->read_pos = d->zstd_out_buf;
  info->read_end = d->zstd_out_buf + read_data_length;
  DBUG_ASSERT(info->read_pos < info->read_end);
  // return first character
  Buffer[0] = info->read_pos[0];
  info->read_pos++;
  return 0;
}

void io_cache_set_read_function(IO_CACHE *cache,
                                read_function_type read_function)
{
  decompressor *d = cache->decompressor;
  if (d)
    d->cache.read_function = read_function;
  else
    cache->read_function = read_function;

}
void io_cache_set_preread_callback(IO_CACHE *cache,
                                   IO_CACHE_CALLBACK preread)
{
  decompressor *d = cache->decompressor;
  if (d)
    d->cache.pre_read = preread;
  else
    cache->pre_read = preread;
}
void io_cache_set_preclose_callback(IO_CACHE *cache,
                                    IO_CACHE_CALLBACK preclose)
{
  decompressor *d = cache->decompressor;
  if (d)
    d->cache.pre_close = preclose;
  else
    cache->pre_close = preclose;
}

void io_cache_set_arg(IO_CACHE *cache, void *arg)
{
  decompressor *d = cache->decompressor;
  if (d)
    d->cache.arg = arg;
  else
    cache->arg = arg;
}
