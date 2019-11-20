/* Copyright (c) 2000, 2018, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   Without limiting anything contained in the foregoing, this file,
   which is part of C Driver for MySQL (Connector/C), is also subject to the
   Universal FOSS Exception, version 1.0, a copy of which can be found at
   http://oss.oracle.com/licenses/universal-foss-exception.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @file mysys/my_compress.cc
*/

#include <lz4frame.h>
#include <string.h>
#include <sys/types.h>
#include <zlib.h>
#include <zstd.h>
#include <algorithm>

#include "my_compiler.h"
#include "my_dbug.h"
#include "my_inttypes.h"
#include "my_sys.h"
#include "mysql/service_mysql_alloc.h"
#include "mysql_com.h"
#include "mysys/mysys_priv.h"

/* Number of times compression context was reset for streaming compression. */
ulonglong compress_ctx_reset = 0;
/* Number of in/out bytes for compression. */
ulonglong compress_input_bytes = 0;
ulonglong compress_output_bytes = 0;

/*
   This replaces the packet with a compressed packet

   SYNOPSIS
     my_compress()
     net	Contains options like compression lib and other context.
     packet	Data to compress. This is is replaced with the compressed data.
     len	Length of data to compress at 'packet'
     complen	out: 0 if packet was not compressed
     level	Compression level

   RETURN
     1   error. 'len' is not changed'
     0   ok.  In this case 'len' contains the size of the compressed packet

   The compression library can be modified using the compression_lib
   connection attribute. If a client requests a library that is not supported
   by the server, the server will default to zlib. If the initial client packet
   is not compressed then the client will be able to seamlessly fall back to
   zlib as well. Otherwise the client will receive an error.

   The slave thread can change the compression library it uses for replication
   through the 'slave_compression_lib' global variable. The slave IO thread
   needs to be restarted for the change to take effect.
*/

bool my_compress(NET *net, uchar *packet, size_t *len, size_t *complen,
                 int level) {
  DBUG_ENTER("my_compress");
  enum mysql_compression_lib comp_lib =
      net ? net->comp_lib : MYSQL_COMPRESSION_ZLIB;
  /*
    Checking MIN_COMPRESS_LENGTH only makes sense for nonstreaming compression
    because even small packets can compressed efficiently with streaming.
  */
  compress_input_bytes += *len;
  if (*len < MIN_COMPRESS_LENGTH && comp_lib != MYSQL_COMPRESSION_ZSTD_STREAM &&
      comp_lib != MYSQL_COMPRESSION_LZ4F_STREAM) {
    *complen = 0;
    DBUG_PRINT("note", ("Packet too short: Not compressed"));
  } else {
    uchar *compbuf = my_compress_alloc(net, packet, len, complen, level);
    if (!compbuf) {
      if (*complen == 0) {
        compress_output_bytes += *len;
        DBUG_RETURN(0);
      } else {
        DBUG_RETURN(1);
      }
    }
    memcpy(packet, compbuf, *len);
    if (net->compress_buf != compbuf) {
      my_free(compbuf);
    }
  }
  compress_output_bytes += *len;
  DBUG_RETURN(0);
}

uchar *zstd_compress_alloc(NET *net, const uchar *packet, size_t *len,
                           size_t *complen, int level) {
  DBUG_ASSERT(net != NULL);
  if (!net->cctx) {
    if (!(net->cctx = ZSTD_createCCtx())) {
      return NULL;
    }
  }
  size_t zstd_len = ZSTD_compressBound(*len);
  void *compbuf;
  size_t zstd_res;

  if (!(compbuf =
            my_malloc(key_memory_my_compress_alloc, zstd_len, MYF(MY_WME)))) {
    return NULL;
  }

  zstd_res = ZSTD_compressCCtx(net->cctx, compbuf, zstd_len,
                               (const void *)packet, *len, level);
  if (ZSTD_isError(zstd_res)) {
    DBUG_PRINT("error", ("Can't compress zstd packet, error: %zd, %s", zstd_res,
                         ZSTD_getErrorName(zstd_res)));
    my_free(compbuf);
    return NULL;
  }

  if (zstd_res > *len) {
    *complen = 0;
    my_free(compbuf);
    DBUG_PRINT("note",
               ("Packet got longer on zstd compression; Not compressed"));
    return NULL;
  }

  *complen = *len;
  *len = zstd_res;
  return (uchar *)compbuf;
}

// Returns 0 on success
bool zstd_uncompress(NET *net, uchar *packet, size_t len, size_t *complen) {
  DBUG_ASSERT(net != NULL);
  size_t zstd_res;
  void *compbuf;

  if (!net->dctx) {
    if (!(net->dctx = ZSTD_createDCtx())) {
      return true;
    }
  }

  if (!(compbuf =
            my_malloc(key_memory_my_compress_alloc, *complen, MYF(MY_WME)))) {
    return true;
  }

  zstd_res = ZSTD_decompressDCtx(net->dctx, compbuf, *complen,
                                 (const void *)packet, len);

  if (ZSTD_isError(zstd_res) || zstd_res != *complen) {
    DBUG_PRINT("error", ("Can't uncompress zstd packet, error: %zd, %s",
                         zstd_res, ZSTD_getErrorName(zstd_res)));
    my_free(compbuf);
    return true;
  }

  memcpy(packet, compbuf, *complen);
  my_free(compbuf);
  return false;
}

uchar *zstd_stream_compress_alloc(NET *net, const uchar *packet, size_t *len,
                                  size_t *complen, int level) {
  size_t zstd_len = ZSTD_compressBound(*len);
  uchar *compbuf;
  size_t zstd_res;
  if (net->compress_buf_len < zstd_len) {
    my_free(net->compress_buf);
    if (!(compbuf = (uchar *)my_malloc(key_memory_my_compress_alloc, zstd_len,
                                       MYF(MY_WME)))) {
      return NULL;
    }
    net->compress_buf = compbuf;
    net->compress_buf_len = zstd_len;
  } else {
    compbuf = net->compress_buf;
  }

  ZSTD_inBuffer inBuf = {packet, *len, 0};
  ZSTD_outBuffer outBuf = {compbuf, zstd_len, 0};

  if (!net->cctx) {
    if (!(net->cctx = ZSTD_createCStream())) {
      return NULL;
    }
    zstd_res = ZSTD_initCStream(net->cctx, level);
    if (ZSTD_isError(zstd_res)) {
      goto error;
    }
  }

  zstd_res = ZSTD_compressStream(net->cctx, &outBuf, &inBuf);
  if (ZSTD_isError(zstd_res)) {
    DBUG_PRINT("error", ("Can't compress zstd_stream packet, error %zd, %s",
                         zstd_res, ZSTD_getErrorName(zstd_res)));
    goto error;
  }

  if (inBuf.pos != inBuf.size) {
    goto error;
  }

  zstd_res = ZSTD_flushStream(net->cctx, &outBuf);
  if (zstd_res != 0) {
    DBUG_PRINT("error", ("Can't flush zstd_stream packet, error %zd, %s",
                         zstd_res, ZSTD_getErrorName(zstd_res)));
    goto error;
  }

  if (outBuf.pos > *len) {
    DBUG_PRINT("note", ("Packet got longer on zstd_stream compression; Not "
                        "compressed, %zu -> %zu",
                        *len, outBuf.pos));
    goto nocompress;
  }

  *complen = *len;
  *len = outBuf.pos;

  return compbuf;

nocompress:
  *complen = 0;
error:
  ZSTD_CCtx_reset(net->cctx, ZSTD_reset_session_only);
  compress_ctx_reset++;
  return NULL;
}

bool zstd_stream_uncompress(NET *net, uchar *packet, size_t len,
                            size_t *complen) {
  unsigned long long decom_size = *complen;
  size_t zstd_res;
  void *decom_buf;
  if (!net->dctx) {
    if (!(net->dctx = ZSTD_createDStream())) {
      return true;
    }
    zstd_res = ZSTD_initDStream(net->dctx);
    if (ZSTD_isError(zstd_res)) {
      return true;
    }
  }

  DBUG_PRINT("note", ("zstd_stream uncompress %zu -> %zu", len, *complen));

  if (!(decom_buf =
            my_malloc(key_memory_my_compress_alloc, decom_size, MYF(MY_WME)))) {
    return true;
  }

  ZSTD_inBuffer inBuf = {packet, len, 0};
  ZSTD_outBuffer outBuf = {decom_buf, decom_size, 0};

  zstd_res = ZSTD_decompressStream(net->dctx, &outBuf, &inBuf);
  if (ZSTD_isError(zstd_res)) {
    DBUG_PRINT("error", ("Can't uncompress zstd_stream packet, error: %zd, %s",
                         zstd_res, ZSTD_getErrorName(zstd_res)));
    my_free(decom_buf);
    return true;
  }
  DBUG_ASSERT(outBuf.pos == outBuf.size);

  if (outBuf.pos != outBuf.size) {
    my_free(decom_buf);
    return true;
  }

  memcpy(packet, decom_buf, outBuf.pos);
  my_free(decom_buf);
  *complen = outBuf.pos;

  return false;
}

uchar *lz4f_stream_compress_alloc(NET *net, const uchar *packet, size_t *len,
                                  size_t *complen, int level) {
  size_t lz4f_len = LZ4F_compressBound(*len, NULL) + LZ4F_HEADER_SIZE_MAX;
  uchar *compbuf;
  size_t lz4f_res;
  size_t pos = 0;

  if (net->compress_buf_len < lz4f_len) {
    my_free(net->compress_buf);
    if (!(compbuf = (uchar *)my_malloc(key_memory_my_compress_alloc, lz4f_len,
                                       MYF(MY_WME)))) {
      return NULL;
    }
    net->compress_buf = compbuf;
    net->compress_buf_len = lz4f_len;
  } else {
    compbuf = net->compress_buf;
  }

  if (!net->lz4f_cctx) {
    lz4f_res = LZ4F_createCompressionContext(
        (LZ4F_compressionContext_t *)&net->lz4f_cctx, LZ4F_VERSION);
    if (LZ4F_isError(lz4f_res)) {
      DBUG_PRINT("error", ("Can't create lz4f_stream context, error %zd, %s",
                           lz4f_res, LZ4F_getErrorName(lz4f_res)));
      goto error;
    }
    if (!net->lz4f_cctx) {
      goto error;
    }
    net->reset_cctx = true;
  }

  if (net->reset_cctx) {
    DBUG_PRINT("note", ("lz4f_stream cctx reset"));
    LZ4F_preferences_t prefs;
    memset(&prefs, 0, sizeof(prefs));
    prefs.compressionLevel = level;

    lz4f_res = LZ4F_compressBegin((LZ4F_compressionContext_t)net->lz4f_cctx,
                                  compbuf, lz4f_len, &prefs);
    if (LZ4F_isError(lz4f_res)) {
      DBUG_PRINT("error", ("Can't compress lz4f_stream packet, error %zd, %s",
                           lz4f_res, LZ4F_getErrorName(lz4f_res)));
      goto error;
    }

    net->reset_cctx = false;
    pos += lz4f_res;
  }

  lz4f_res =
      LZ4F_compressUpdate((LZ4F_compressionContext_t)net->lz4f_cctx,
                          compbuf + pos, lz4f_len - pos, packet, *len, NULL);
  if (LZ4F_isError(lz4f_res)) {
    DBUG_PRINT("error", ("Can't compress lz4f_stream packet, error %zd, %s",
                         lz4f_res, LZ4F_getErrorName(lz4f_res)));
    goto error;
  }
  pos += lz4f_res;

  lz4f_res = LZ4F_flush((LZ4F_compressionContext_t)net->lz4f_cctx,
                        compbuf + pos, lz4f_len - pos, NULL);
  if (LZ4F_isError(lz4f_res)) {
    DBUG_PRINT("error", ("Can't flush lz4f_stream packet, error %zd, %s",
                         lz4f_res, LZ4F_getErrorName(lz4f_res)));
    goto error;
  }
  pos += lz4f_res;

  if (pos > *len) {
    DBUG_PRINT("note", ("Packet got longer on lz4f_stream compression; Not "
                        "compressed, %zu -> %zu",
                        *len, pos));
    goto nocompress;
  }

  *complen = *len;
  *len = pos;

  DBUG_PRINT("note", ("lz4f_stream compress %zu -> %zu", *complen, pos));
  return compbuf;

nocompress:
  *complen = 0;
error:
  compress_ctx_reset++;
  net->reset_cctx = true;
  return NULL;
}

bool lz4f_stream_uncompress(NET *net, uchar *packet, size_t len,
                            size_t *complen) {
  size_t decom_size = *complen;
  size_t lz4f_res;
  uchar *decom_buf;

  DBUG_PRINT("note", ("lz4f_stream uncompress %zu -> %zu", len, *complen));
  if (!net->lz4f_dctx) {
    lz4f_res = LZ4F_createDecompressionContext(
        (LZ4F_decompressionContext_t *)&net->lz4f_dctx, LZ4F_VERSION);
    if (LZ4F_isError(lz4f_res)) {
      DBUG_PRINT("error", ("Can't create lz4f_stream context, error %zd, %s",
                           lz4f_res, LZ4F_getErrorName(lz4f_res)));
      return true;
    }
    if (!net->lz4f_dctx) {
      return true;
    }
  }

  if (!(decom_buf = (uchar *)my_malloc(key_memory_my_compress_alloc, decom_size,
                                       MYF(MY_WME)))) {
    return true;
  }

  uchar *src_pos = packet;
  size_t src_len = len;
  uchar *dst_pos = decom_buf;
  size_t dst_len = decom_size;

  lz4f_res = LZ4F_decompress((LZ4F_decompressionContext_t)net->lz4f_dctx,
                             dst_pos, &dst_len, src_pos, &src_len, NULL);
  if (LZ4F_isError(lz4f_res)) {
    DBUG_PRINT("error", ("Can't decompress lz4f_stream packet, error %zd, %s",
                         lz4f_res, LZ4F_getErrorName(lz4f_res)));
    my_free(decom_buf);
    return true;
  }

  dst_pos += dst_len;
  src_pos += src_len;

  // Assert that src and dst are consumed.
  DBUG_ASSERT(dst_pos == decom_buf + decom_size);
  DBUG_ASSERT(src_pos == packet + len);

  if (dst_pos != decom_buf + decom_size || src_pos != packet + len) {
    my_free(decom_buf);
    return true;
  }

  memcpy(packet, decom_buf, decom_size);
  my_free(decom_buf);
  *complen = decom_size;

  return false;
}

uchar *my_compress_alloc(NET *net, const uchar *packet, size_t *len,
                         size_t *complen, int level) {
  enum mysql_compression_lib comp_lib =
      net ? net->comp_lib : MYSQL_COMPRESSION_ZLIB;
  if (comp_lib == MYSQL_COMPRESSION_ZSTD) {
    return zstd_compress_alloc(net, packet, len, complen, level);
  } else if (comp_lib == MYSQL_COMPRESSION_ZSTD_STREAM) {
    return zstd_stream_compress_alloc(net, packet, len, complen, level);
  } else if (comp_lib == MYSQL_COMPRESSION_LZ4F_STREAM) {
    return lz4f_stream_compress_alloc(net, packet, len, complen, level);
  }

  uchar *compbuf;
  uLongf tmp_complen;
  int res;
  *complen = *len * 120 / 100 + 12;

  if (!(compbuf = (uchar *)my_malloc(key_memory_my_compress_alloc, *complen,
                                     MYF(MY_WME))))
    return 0; /* Not enough memory */

  tmp_complen = (uint)*complen;
  res =
      compress2(compbuf, &tmp_complen, packet, static_cast<uLong>(*len), level);
  *complen = tmp_complen;

  if (res != Z_OK) {
    my_free(compbuf);
    return 0;
  }

  if (*complen >= *len) {
    *complen = 0;
    my_free(compbuf);
    DBUG_PRINT("note", ("Packet got longer on compression; Not compressed"));
    return 0;
  }
  /* Store length of compressed packet in *len */
  std::swap(*len, *complen);
  return compbuf;
}

/*
  Uncompress packet

   SYNOPSIS
     my_uncompress()
     packet	Compressed data. This is is replaced with the orignal data.
     len	Length of compressed data
     complen	Length of the packet buffer (must be enough for the original
                data)

   RETURN
     1   error
     0   ok.  In this case 'complen' contains the updated size of the
              real data.
*/

bool my_uncompress(NET *net, uchar *packet, size_t len, size_t *complen) {
  uLongf tmp_complen;
  DBUG_ENTER("my_uncompress");

  if (*complen) /* If compressed */
  {
    enum mysql_compression_lib comp_lib =
        net ? net->comp_lib : MYSQL_COMPRESSION_ZLIB;
    if (comp_lib == MYSQL_COMPRESSION_ZSTD) {
      DBUG_RETURN(zstd_uncompress(net, packet, len, complen));
    } else if (comp_lib == MYSQL_COMPRESSION_ZSTD_STREAM) {
      DBUG_RETURN(zstd_stream_uncompress(net, packet, len, complen));
    } else if (comp_lib == MYSQL_COMPRESSION_LZ4F_STREAM) {
      DBUG_RETURN(lz4f_stream_uncompress(net, packet, len, complen));
    }

    uchar *compbuf =
        (uchar *)my_malloc(key_memory_my_compress_alloc, *complen, MYF(MY_WME));
    int error;
    if (!compbuf) DBUG_RETURN(1); /* Not enough memory */

    tmp_complen = (uint)*complen;
    error = uncompress(compbuf, &tmp_complen, packet, (uLong)len);
    *complen = tmp_complen;
    if (error != Z_OK) { /* Probably wrong packet */
      DBUG_PRINT("error", ("Can't uncompress packet, error: %d", error));
      my_free(compbuf);
      DBUG_RETURN(1);
    }
    memcpy(packet, compbuf, *complen);
    my_free(compbuf);
  } else {
    *complen = len;
    enum mysql_compression_lib comp_lib =
        net ? net->comp_lib : MYSQL_COMPRESSION_ZLIB;
    // On the compression side, the compression context is reset when an
    // uncompressed packet is sent. The same should be done on the
    // decompression side so that both contexts stay in sync.
    if (comp_lib == MYSQL_COMPRESSION_ZSTD_STREAM) {
      if (net->dctx) {
        ZSTD_DCtx_reset(net->dctx, ZSTD_reset_session_only);
      }
    } else if (comp_lib == MYSQL_COMPRESSION_LZ4F_STREAM) {
      if (net->lz4f_dctx) {
        DBUG_PRINT("note", ("lz4f_stream dctx reset"));
        LZ4F_resetDecompressionContext(
            (LZ4F_decompressionContext_t)net->lz4f_dctx);
      }
    }
  }
  DBUG_RETURN(0);
}
