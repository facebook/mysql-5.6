/* Copyright (c) 2000, 2017, Oracle and/or its affiliates. All rights reserved.

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

/* Written by Sinisa Milivojevic <sinisa@mysql.com> */

/**
  @file mysys/my_compress.cc
*/

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

#ifdef MYSQL_SERVER
extern uint zstd_net_compression_level;
#else
#define zstd_net_compression_level 3
#endif

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
   needs to be restarted for the change to take effect. The compression level
   for zstd can be modified at runtime using 'zstd_net_compression_level'.
   Changes to compression level will take place immediately. This will not
   affect connections using zlib compression.
*/

bool my_compress(NET *net, uchar *packet, size_t *len, size_t *complen,
                 uint level) {
  DBUG_ENTER("my_compress");
  if (*len < MIN_COMPRESS_LENGTH) {
    *complen = 0;
    DBUG_PRINT("note", ("Packet too short: Not compressed"));
  } else {
    uchar *compbuf = my_compress_alloc(net, packet, len, complen, level);
    if (!compbuf) DBUG_RETURN(*complen ? 0 : 1);
    memcpy(packet, compbuf, *len);
    my_free(compbuf);
  }
  DBUG_RETURN(0);
}

uchar *zstd_compress_alloc(NET *net, const uchar *packet, size_t *len,
                           size_t *complen, uint level MY_ATTRIBUTE((unused))) {
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

  zstd_res =
      ZSTD_compressCCtx(net->cctx, compbuf, zstd_len, (const void *)packet,
                        *len, zstd_net_compression_level);
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

uchar *my_compress_alloc(NET *net, const uchar *packet, size_t *len,
                         size_t *complen, uint level) {
  enum mysql_compression_lib comp_lib =
      net ? net->comp_lib : MYSQL_COMPRESSION_ZLIB;
  if (comp_lib == MYSQL_COMPRESSION_ZSTD) {
    return zstd_compress_alloc(net, packet, len, complen, level);
  }

  uchar *compbuf;
  uLongf tmp_complen;
  int res;
  *complen = *len * 120 / 100 + 12;

  if (!(compbuf = (uchar *)my_malloc(key_memory_my_compress_alloc, *complen,
                                     MYF(MY_WME))))
    return 0; /* Not enough memory */

  tmp_complen = (uint)*complen;
  res = compress2((Bytef *)compbuf, &tmp_complen, (Bytef *)packet, (uLong)*len,
                  level);
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
    }
    uchar *compbuf =
        (uchar *)my_malloc(key_memory_my_compress_alloc, *complen, MYF(MY_WME));
    int error;
    if (!compbuf) DBUG_RETURN(1); /* Not enough memory */

    tmp_complen = (uint)*complen;
    error =
        uncompress((Bytef *)compbuf, &tmp_complen, (Bytef *)packet, (uLong)len);
    *complen = tmp_complen;
    if (error != Z_OK) { /* Probably wrong packet */
      DBUG_PRINT("error", ("Can't uncompress packet, error: %d", error));
      my_free(compbuf);
      DBUG_RETURN(1);
    }
    memcpy(packet, compbuf, *complen);
    my_free(compbuf);
  } else
    *complen = len;
  DBUG_RETURN(0);
}
