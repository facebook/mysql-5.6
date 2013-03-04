/******************************************************
Copyright (c) 2011 Percona Ireland Ltd.

The xbstream format interface.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

*******************************************************/

#ifndef XBSTREAM_H
#define XBSTREAM_H

#include <my_base.h>

/* Magic value in a chunk header */
#define XB_STREAM_CHUNK_MAGIC "XBSTCK01"

/* Chunk flags */
/* Chunk can be ignored if unknown version/format */
#define XB_STREAM_FLAG_IGNORABLE 0x01

typedef struct xb_wstream_struct xb_wstream_t;

typedef struct xb_wstream_file_struct xb_wstream_file_t;

/************************************************************************
Write interface. */

xb_wstream_t *xb_stream_write_new(void);

xb_wstream_file_t *xb_stream_write_open(xb_wstream_t *stream, const char *path,
					MY_STAT *mystat);

int xb_stream_write_data(xb_wstream_file_t *file, const void *buf, size_t len);

int xb_stream_write_close(xb_wstream_file_t *file);

int xb_stream_write_done(xb_wstream_t *stream);

/************************************************************************
Read interface. */

typedef enum {
	XB_STREAM_READ_CHUNK,
	XB_STREAM_READ_EOF,
	XB_STREAM_READ_ERROR
} xb_rstream_result_t;

typedef enum {
	XB_CHUNK_TYPE_UNKNOWN = '\0',
	XB_CHUNK_TYPE_PAYLOAD = 'P',
	XB_CHUNK_TYPE_EOF = 'E'
} xb_chunk_type_t;

typedef struct xb_rstream_struct xb_rstream_t;

typedef struct {
	uchar           flags;
	xb_chunk_type_t type;
	uint		pathlen;
	char		path[FN_REFLEN];
	size_t		length;
	my_off_t	offset;
	void		*data;
	ulong		checksum;
} xb_rstream_chunk_t;

xb_rstream_t *xb_stream_read_new(void);

xb_rstream_result_t xb_stream_read_chunk(xb_rstream_t *stream,
					 xb_rstream_chunk_t *chunk);

int xb_stream_read_done(xb_rstream_t *stream);

#endif
