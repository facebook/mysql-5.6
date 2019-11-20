/* Copyright (c) 2000, 2010, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/* Written by Sinisa Milivojevic <sinisa@mysql.com> */

#include <my_global.h>
#ifdef HAVE_COMPRESS
#include <my_sys.h>
#ifndef SCO
#include <m_string.h>
#endif
#include <zlib.h>
#include <zstd.h>
#include <lz4frame.h>

#include <mysql_com.h>

/* Number of times compression context was reset for streaming compression. */
ulonglong compress_ctx_reset = 0;
/* Number of in/out bytes for compression. */
ulonglong compress_input_bytes = 0;
ulonglong compress_output_bytes = 0;

/*
   This replaces the packet with a compressed packet

   SYNOPSIS
     my_compress()
     net  Contains options like compression lib and other contexts
     packet	Data to compress. This is is replaced with the compressed data.
     len	Length of data to compress at 'packet'
     complen	out: 0 if packet was not compressed

   RETURN
     1   error. 'len' is not changed'
     0   ok.  In this case 'len' contains the size of the compressed packet

  The compression library can be modified using the compression_lib connection
  attribute. If a client requests a library that is not supported by the server,
  the server will default to zlib. If the initial client packet is not
  compressed then the client will be able to seamlessly fall back to zlib as
  well. Otherwise the client will receive an error.

  The slave thread can change the compression library it uses for replication
  through the 'slave_compression_lib' global variable. The slave IO thread
  needs to be restarted for the change to take effect.
*/

my_bool my_compress(NET *net, uchar *packet,
                    size_t *len, size_t *complen, int level)
{
  DBUG_ENTER("my_compress");
  DBUG_ASSERT(packet != NULL);
  DBUG_ASSERT(len != NULL);
  DBUG_ASSERT(complen != NULL);
  enum mysql_compression_lib comp_lib = net ? net->comp_lib : MYSQL_COMPRESSION_ZLIB;
  /*
    Checking MIN_COMPRESS_LENGTH only makes sense for nonstreaming compression
    because even small packets can compressed efficiently with streaming.
  */
  compress_input_bytes += *len;
  if (*len < MIN_COMPRESS_LENGTH &&
      comp_lib != MYSQL_COMPRESSION_ZSTD_STREAM &&
      comp_lib != MYSQL_COMPRESSION_LZ4F_STREAM)
  {
    *complen=0;
    DBUG_PRINT("note",("Packet too short: Not compressed"));
  }
  else
  {
    uchar *compbuf=my_compress_alloc(net, packet, len, complen, level);
    if (!compbuf)
    {
      if (*complen == 0) {
        compress_output_bytes += *len;
        DBUG_RETURN(0);
      } else {
        DBUG_RETURN(1);
      }
    }
    memcpy(packet,compbuf,*len);
    if (net->compress_buf != compbuf) {
      my_free(compbuf);
    }
  }
  compress_output_bytes += *len;
  DBUG_RETURN(0);
}

uchar *zstd_compress_alloc(NET *net, const uchar *packet, size_t *len,
                         size_t *complen, int level)
{
  DBUG_ASSERT(net != NULL);
  if (!net->cctx) {
    if (!(net->cctx = ZSTD_createCCtx())) {
      return NULL;
    }
  }
  size_t zstd_len = ZSTD_compressBound(*len);
  void *compbuf;
  size_t zstd_res;

  if (!(compbuf = my_malloc(zstd_len, MYF(MY_WME)))) {
    return NULL;
  }

  zstd_res = ZSTD_compressCCtx(net->cctx, compbuf, zstd_len, (const void *)packet, *len, level);
  if (ZSTD_isError(zstd_res)) {
    DBUG_PRINT("error", ("Can't compress zstd packet, error: %zd, %s",
               zstd_res, ZSTD_getErrorName(zstd_res)));
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
  return compbuf;
}

// Returns 0 on success
my_bool zstd_uncompress(NET *net, uchar *packet, size_t len, size_t *complen)
{
  DBUG_ASSERT(net != NULL);
  size_t zstd_res;
  void *compbuf;

  if (!net->dctx) {
    if (!(net->dctx = ZSTD_createDCtx())) {
      return TRUE;
    }
  }

  if (!(compbuf = my_malloc(*complen, MYF(MY_WME)))) {
    return TRUE;
  }

  zstd_res = ZSTD_decompressDCtx(net->dctx, compbuf, *complen,
                                 (const void *)packet, len);

  if (ZSTD_isError(zstd_res) || zstd_res != *complen) {
    DBUG_PRINT("error", ("Can't uncompress zstd packet, error: %zd, %s",
               zstd_res, ZSTD_getErrorName(zstd_res)));
    my_free(compbuf);
    return TRUE;
  }

  memcpy(packet, compbuf, *complen);
  my_free(compbuf);
  return FALSE;
}

uchar *zstd_stream_compress_alloc(NET *net, const uchar *packet, size_t *len,
                                  size_t *complen, int level)
{
  size_t zstd_len = ZSTD_compressBound(*len);
  void *compbuf;
  size_t zstd_res;
  if (net->compress_buf_len < zstd_len) {
    my_free(net->compress_buf);
    if (!(compbuf = my_malloc(zstd_len, MYF(MY_WME)))) {
      return NULL;
    }
    net->compress_buf = compbuf;
    net->compress_buf_len = zstd_len;
  } else {
    compbuf = net->compress_buf;
  }

  ZSTD_inBuffer inBuf = { packet, *len, 0 };
  ZSTD_outBuffer outBuf = { compbuf, zstd_len, 0 };

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
    DBUG_PRINT("note",
               ("Packet got longer on zstd_stream compression; Not compressed, %zu -> %zu", *len, outBuf.pos));
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

my_bool zstd_stream_uncompress(NET *net, uchar *packet, size_t len, size_t *complen) {
  unsigned long long decom_size = *complen;
  size_t zstd_res;
  void *decom_buf;
  if (!net->dctx) {
    if (!(net->dctx = ZSTD_createDStream())) {
      return TRUE;
    }
    zstd_res = ZSTD_initDStream(net->dctx);
    if (ZSTD_isError(zstd_res)) {
      return TRUE;
    }
  }

  DBUG_PRINT("note", ("zstd_stream uncompress %zu -> %zu", len, *complen));

  if (!(decom_buf = my_malloc(decom_size, MYF(MY_WME)))) {
    return TRUE;
  }

  ZSTD_inBuffer inBuf = { packet, len, 0 };
  ZSTD_outBuffer outBuf = { decom_buf, decom_size, 0 };

  zstd_res = ZSTD_decompressStream(net->dctx, &outBuf, &inBuf);
  if (ZSTD_isError(zstd_res)) {
    DBUG_PRINT("error", ("Can't uncompress zstd_stream packet, error: %zd, %s",
                         zstd_res, ZSTD_getErrorName(zstd_res)));
    my_free(decom_buf);
    return TRUE;
  }
  DBUG_ASSERT(outBuf.pos == outBuf.size);

  if (outBuf.pos != outBuf.size) {
    my_free(decom_buf);
    return TRUE;
  }

  memcpy(packet, decom_buf, outBuf.pos);
  my_free(decom_buf);
  *complen = outBuf.pos;

  return FALSE;
}

uchar *lz4f_stream_compress_alloc(NET *net, const uchar *packet, size_t *len,
                                  size_t *complen, int level) {
  size_t lz4f_len = LZ4F_compressBound(*len, NULL) + LZ4F_HEADER_SIZE_MAX;
  void *compbuf;
  size_t lz4f_res;
  size_t pos = 0;

  if (net->compress_buf_len < lz4f_len) {
    my_free(net->compress_buf);
    if (!(compbuf = my_malloc(lz4f_len, MYF(MY_WME)))) {
      return NULL;
    }
    net->compress_buf = compbuf;
    net->compress_buf_len = lz4f_len;
  } else {
    compbuf = net->compress_buf;
  }

  if (!net->lz4f_cctx) {
    lz4f_res = LZ4F_createCompressionContext((LZ4F_compressionContext_t *)&net->lz4f_cctx, LZ4F_VERSION);
    if (LZ4F_isError(lz4f_res)) {
      DBUG_PRINT("error", ("Can't create lz4f_stream context, error %zd, %s",
                           lz4f_res, LZ4F_getErrorName(lz4f_res)));
      goto error;
    }
    if (!net->lz4f_cctx) {
      goto error;
    }
    net->reset_cctx = TRUE;
  }

  if (net->reset_cctx) {
    DBUG_PRINT("note", ("lz4f_stream cctx reset"));
    LZ4F_preferences_t prefs;
    memset(&prefs, 0, sizeof(prefs));
    prefs.compressionLevel = level;

    lz4f_res = LZ4F_compressBegin(net->lz4f_cctx, compbuf, lz4f_len, &prefs);
    if (LZ4F_isError(lz4f_res)) {
      DBUG_PRINT("error", ("Can't compress lz4f_stream packet, error %zd, %s",
                           lz4f_res, LZ4F_getErrorName(lz4f_res)));
      goto error;
    }

    net->reset_cctx = FALSE;
    pos += lz4f_res;
  }

  lz4f_res = LZ4F_compressUpdate(net->lz4f_cctx, compbuf + pos, lz4f_len - pos, packet, *len, NULL);
  if (LZ4F_isError(lz4f_res)) {
    DBUG_PRINT("error", ("Can't compress lz4f_stream packet, error %zd, %s",
                         lz4f_res, LZ4F_getErrorName(lz4f_res)));
    goto error;
  }
  pos += lz4f_res;

  lz4f_res = LZ4F_flush(net->lz4f_cctx, compbuf + pos, lz4f_len - pos, NULL);
  if (LZ4F_isError(lz4f_res)) {
    DBUG_PRINT("error", ("Can't flush lz4f_stream packet, error %zd, %s",
                         lz4f_res, LZ4F_getErrorName(lz4f_res)));
    goto error;
  }
  pos += lz4f_res;

  if (pos > *len) {
    DBUG_PRINT("note",
               ("Packet got longer on lz4f_stream compression; Not compressed, %zu -> %zu", *len, pos));
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
  net->reset_cctx = TRUE;
  return NULL;
}

my_bool lz4f_stream_uncompress(NET *net, uchar *packet, size_t len, size_t *complen) {
  size_t decom_size = *complen;
  size_t lz4f_res;
  uchar *decom_buf;

  DBUG_PRINT("note", ("lz4f_stream uncompress %zu -> %zu", len, *complen));
  if (!net->lz4f_dctx) {
    lz4f_res = LZ4F_createDecompressionContext((LZ4F_decompressionContext_t *)&net->lz4f_dctx, LZ4F_VERSION);
    if (LZ4F_isError(lz4f_res)) {
      DBUG_PRINT("error", ("Can't create lz4f_stream context, error %zd, %s",
                           lz4f_res, LZ4F_getErrorName(lz4f_res)));
      return TRUE;
    }
    if (!net->lz4f_dctx) {
      return TRUE;
    }
  }

  if (!(decom_buf = my_malloc(decom_size, MYF(MY_WME)))) {
    return TRUE;
  }

  uchar *src_pos = packet;
  size_t src_len = len;
  uchar *dst_pos = decom_buf;
  size_t dst_len = decom_size;

  lz4f_res = LZ4F_decompress(net->lz4f_dctx, dst_pos, &dst_len, src_pos, &src_len, NULL);
  if (LZ4F_isError(lz4f_res)) {
    DBUG_PRINT("error", ("Can't decompress lz4f_stream packet, error %zd, %s",
                         lz4f_res, LZ4F_getErrorName(lz4f_res)));
    my_free(decom_buf);
    return TRUE;
  }

  dst_pos += dst_len;
  src_pos += src_len;

  // Assert that src and dst are consumed.
  DBUG_ASSERT(dst_pos == decom_buf + decom_size);
  DBUG_ASSERT(src_pos == packet + len);

  if (dst_pos != decom_buf + decom_size || src_pos != packet + len) {
    my_free(decom_buf);
    return TRUE;
  }

  memcpy(packet, decom_buf, decom_size);
  my_free(decom_buf);
  *complen = decom_size;

  return FALSE;
}

uchar *my_compress_alloc(NET *net,
                         const uchar *packet, size_t *len,
                         size_t *complen, int level)
{
  enum mysql_compression_lib comp_lib = net ? net->comp_lib
                                            : MYSQL_COMPRESSION_ZLIB;

  if (comp_lib == MYSQL_COMPRESSION_ZSTD) {
    return zstd_compress_alloc(net, packet, len, complen, level);
  } else if (comp_lib == MYSQL_COMPRESSION_ZSTD_STREAM) {
    return zstd_stream_compress_alloc(net, packet, len, complen, level);
  } else if (comp_lib == MYSQL_COMPRESSION_LZ4F_STREAM) {
    return lz4f_stream_compress_alloc(net, packet, len, complen, level);
  }

  if (comp_lib == MYSQL_COMPRESSION_NONE) {
    // If compression algorithm is set to none do not compress, even if
    // compress flag was set
    *complen = 0;
    return 0;
  }

  uchar *compbuf;
  uLongf tmp_complen;
  int res;
  *complen=  *len * 120 / 100 + 12;

  if (!(compbuf= (uchar *) my_malloc(*complen, MYF(MY_WME))))
    return 0;					/* Not enough memory */

  tmp_complen= (uint) *complen;
  res= compress2((Bytef*) compbuf, &tmp_complen, (Bytef*) packet, (uLong) *len,
                 level);
  *complen=    tmp_complen;

  if (res != Z_OK)
  {
    my_free(compbuf);
    return 0;
  }

  if (*complen >= *len)
  {
    *complen= 0;
    my_free(compbuf);
    DBUG_PRINT("note",("Packet got longer on compression; Not compressed"));
    return 0;
  }
  /* Store length of compressed packet in *len */
  swap_variables(size_t, *len, *complen);
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

my_bool my_uncompress(NET *net, uchar *packet,
                      size_t len, size_t *complen)
{
  uLongf tmp_complen;
  DBUG_ENTER("my_uncompress");

  if (*complen)					/* If compressed */
  {
    enum mysql_compression_lib comp_lib = net ? net->comp_lib
                                              : MYSQL_COMPRESSION_ZLIB;
    if (comp_lib == MYSQL_COMPRESSION_ZSTD) {
      DBUG_RETURN(zstd_uncompress(net, packet, len, complen));
    } else if (comp_lib == MYSQL_COMPRESSION_ZSTD_STREAM) {
      DBUG_RETURN(zstd_stream_uncompress(net, packet, len, complen));
    } else if (comp_lib == MYSQL_COMPRESSION_LZ4F_STREAM) {
      DBUG_RETURN(lz4f_stream_uncompress(net, packet, len, complen));
    }

    uchar *compbuf= (uchar *) my_malloc(*complen,MYF(MY_WME));
    int error;
    if (!compbuf)
      DBUG_RETURN(1);				/* Not enough memory */

    tmp_complen= (uint) *complen;
    error= uncompress((Bytef*) compbuf, &tmp_complen, (Bytef*) packet,
                      (uLong) len);
    *complen= tmp_complen;
    if (error != Z_OK)
    {						/* Probably wrong packet */
      DBUG_PRINT("error",("Can't uncompress packet, error: %d",error));
      my_free(compbuf);
      DBUG_RETURN(1);
    }
    memcpy(packet, compbuf, *complen);
    my_free(compbuf);
  }
  else
  {
    *complen= len;
    enum mysql_compression_lib comp_lib = net ? net->comp_lib
                                              : MYSQL_COMPRESSION_ZLIB;
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
        LZ4F_resetDecompressionContext(net->lz4f_dctx);
      }
    }
  }
  DBUG_RETURN(0);
}

/*
  Internal representation of the frm blob is:

  ver	  4 bytes
  orglen  4 bytes
  complen 4 bytes
*/

#define BLOB_HEADER 12


/*
  packfrm is a method used to compress the frm file for storage in a
  handler. This method was developed for the NDB handler and has been moved
  here to serve also other uses.

  SYNOPSIS
    packfrm()
    data                    Data reference to frm file data.
    len                     Length of frm file data
    out:pack_data           Reference to the pointer to the packed frm data
    out:pack_len            Length of packed frm file data

  NOTES
    data is replaced with compressed content

  RETURN VALUES
    0                       Success
    >0                      Failure
*/

int packfrm(uchar *data, size_t len,
            uchar **pack_data, size_t *pack_len)
{
  int error;
  size_t org_len, comp_len, blob_len;
  uchar *blob;
  DBUG_ENTER("packfrm");
  DBUG_PRINT("enter", ("data: 0x%lx  len: %lu", (long) data, (ulong) len));

  error= 1;
  org_len= len;
  if (my_compress(NULL, (uchar*)data, &org_len, &comp_len,
                  Z_DEFAULT_COMPRESSION))
    goto err;

  DBUG_PRINT("info", ("org_len: %lu  comp_len: %lu", (ulong) org_len,
                      (ulong) comp_len));
  DBUG_DUMP("compressed", data, org_len);

  error= 2;
  blob_len= BLOB_HEADER + org_len;
  if (!(blob= (uchar*) my_malloc(blob_len,MYF(MY_WME))))
    goto err;

  /* Store compressed blob in machine independent format */
  int4store(blob, 1);
  int4store(blob+4, (uint32) len);
  int4store(blob+8, (uint32) org_len);          /* compressed length */

  /* Copy frm data into blob, already in machine independent format */
  memcpy(blob+BLOB_HEADER, data, org_len);

  *pack_data= blob;
  *pack_len=  blob_len;
  error= 0;

  DBUG_PRINT("exit", ("pack_data: 0x%lx  pack_len: %lu",
                      (long) *pack_data, (ulong) *pack_len));
err:
  DBUG_RETURN(error);

}

/*
  unpackfrm is a method used to decompress the frm file received from a
  handler. This method was developed for the NDB handler and has been moved
  here to serve also other uses for other clustered storage engines.

  SYNOPSIS
    unpackfrm()
    pack_data               Data reference to packed frm file data
    out:unpack_data         Reference to the pointer to the unpacked frm data
    out:unpack_len          Length of unpacked frm file data

  RETURN VALUES¨
    0                       Success
    >0                      Failure
*/

int unpackfrm(uchar **unpack_data, size_t *unpack_len,
              const uchar *pack_data)
{
   uchar *data;
   size_t complen, orglen;
   ulong ver;
   DBUG_ENTER("unpackfrm");
   DBUG_PRINT("enter", ("pack_data: 0x%lx", (long) pack_data));

   ver=         uint4korr(pack_data);
   orglen=      uint4korr(pack_data+4);
   complen=     uint4korr(pack_data+8);

   DBUG_PRINT("blob",("ver: %lu  complen: %lu  orglen: %lu",
                      ver, (ulong) complen, (ulong) orglen));
   DBUG_DUMP("blob->data", pack_data + BLOB_HEADER, complen);

   if (ver != 1)
     DBUG_RETURN(1);
   if (!(data= my_malloc(MY_MAX(orglen, complen), MYF(MY_WME))))
     DBUG_RETURN(2);
   memcpy(data, pack_data + BLOB_HEADER, complen);

   if (my_uncompress(NULL, data, complen, &orglen))
   {
     my_free(data);
     DBUG_RETURN(3);
   }

   *unpack_data= data;
   *unpack_len=  orglen;

   DBUG_PRINT("exit", ("frmdata: 0x%lx  len: %lu", (long) *unpack_data,
                       (ulong) *unpack_len));
   DBUG_RETURN(0);
}
#endif /* HAVE_COMPRESS */
