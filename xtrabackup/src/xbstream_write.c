/******************************************************
Copyright (c) 2011 Percona Ireland Ltd.

The xbstream format writer implementation.

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

#include <mysql_version.h>
#include <my_base.h>
#include <zlib.h>
#include "common.h"
#include "xbstream.h"

/* Group writes smaller than this into a single chunk */
#define XB_STREAM_MIN_CHUNK_SIZE (64 * 1024)

struct xb_wstream_struct {
	pthread_mutex_t	mutex;
	File		fd;
};

struct xb_wstream_file_struct {
	xb_wstream_t	*stream;
	char		*path;
	ulong		path_len;
	char		chunk[XB_STREAM_MIN_CHUNK_SIZE];
	char		*chunk_ptr;
	size_t		chunk_free;
	my_off_t	offset;
};

static int xb_stream_flush(xb_wstream_file_t *file);
static int xb_stream_write_chunk(xb_wstream_file_t *file,
				 const void *buf, size_t len);
static int xb_stream_write_eof(xb_wstream_file_t *file);

xb_wstream_t *
xb_stream_write_new(void)
{
	xb_wstream_t	*stream;

	stream = (xb_wstream_t *) my_malloc(sizeof(xb_wstream_t), MYF(MY_FAE));
	pthread_mutex_init(&stream->mutex, NULL);
	stream->fd = fileno(stdout);

#ifdef __WIN__
	setmode(stream->fd, _O_BINARY);
#endif

	return stream;;
}

xb_wstream_file_t *
xb_stream_write_open(xb_wstream_t *stream, const char *path,
		     MY_STAT *mystat __attribute__((unused)))
{
	xb_wstream_file_t	*file;
	ulong			path_len;

	path_len = strlen(path);

	if (path_len > FN_REFLEN) {
		msg("xb_stream_write_open(): file path is too long.\n");
		return NULL;
	}

	file = (xb_wstream_file_t *) my_malloc(sizeof(xb_wstream_file_t) +
					       path_len + 1, MYF(MY_FAE));

	file->path = (char *) (file + 1);
	memcpy(file->path, path, path_len + 1);
	file->path_len = path_len;

	file->stream = stream;
	file->offset = 0;
	file->chunk_ptr = file->chunk;
	file->chunk_free = XB_STREAM_MIN_CHUNK_SIZE;

	return file;
}

int
xb_stream_write_data(xb_wstream_file_t *file, const void *buf, size_t len)
{
	if (len < file->chunk_free) {
		memcpy(file->chunk_ptr, buf, len);
		file->chunk_ptr += len;
		file->chunk_free -= len;

		return 0;
	}

	if (xb_stream_flush(file))
		return 1;

	return xb_stream_write_chunk(file, buf, len);
}

int
xb_stream_write_close(xb_wstream_file_t *file)
{
	if (xb_stream_flush(file) ||
	    xb_stream_write_eof(file)) {
		MY_FREE(file);
		return 1;
	}

	MY_FREE(file);

	return 0;
}

int
xb_stream_write_done(xb_wstream_t *stream)
{
	pthread_mutex_destroy(&stream->mutex);

	MY_FREE(stream);

	return 0;
}

static
int
xb_stream_flush(xb_wstream_file_t *file)
{
	if (file->chunk_ptr == file->chunk) {
		return 0;
	}

	if (xb_stream_write_chunk(file, file->chunk,
				  file->chunk_ptr - file->chunk)) {
		return 1;
	}

	file->chunk_ptr = file->chunk;
	file->chunk_free = XB_STREAM_MIN_CHUNK_SIZE;

	return 0;
}

#define F_WRITE(ptr,size)                                       	\
	do {								\
		if (my_write(fd, ptr, size, MYF(MY_WME | MY_NABP)))	\
			goto err;					\
	} while (0)

static
int
xb_stream_write_chunk(xb_wstream_file_t *file, const void *buf, size_t len)
{
	/* Chunk magic + flags + chunk type + path_len + path + len + offset +
	checksum */
	uchar		tmpbuf[sizeof(XB_STREAM_CHUNK_MAGIC) - 1 + 1 + 1 + 4 +
			       FN_REFLEN + 8 + 8 + 4];
	uchar		*ptr;
	xb_wstream_t	*stream = file->stream;
	File		fd = stream->fd;
	ulong		checksum;

	/* Write xbstream header */
	ptr = tmpbuf;

	/* Chunk magic */
	memcpy(ptr, XB_STREAM_CHUNK_MAGIC, sizeof(XB_STREAM_CHUNK_MAGIC) - 1);
	ptr += sizeof(XB_STREAM_CHUNK_MAGIC) - 1;

	*ptr++ = 0;                              /* Chunk flags */

	*ptr++ = (uchar) XB_CHUNK_TYPE_PAYLOAD;  /* Chunk type */

	int4store(ptr, file->path_len);          /* Path length */
	ptr += 4;

	memcpy(ptr, file->path, file->path_len); /* Path */
	ptr += file->path_len;

	int8store(ptr, len);                     /* Payload length */
	ptr += 8;

	pthread_mutex_lock(&stream->mutex);

	int8store(ptr, file->offset);            /* Payload offset */
	ptr += 8;

	checksum = crc32(0, buf, len);           /* checksum */
	int4store(ptr, checksum);
	ptr += 4;

	xb_ad(ptr <= tmpbuf + sizeof(tmpbuf));

	F_WRITE(tmpbuf, ptr - tmpbuf);

	F_WRITE(buf, len);                       /* Payload */

	file->offset+= len;

	pthread_mutex_unlock(&stream->mutex);

	return 0;

err:

	pthread_mutex_unlock(&stream->mutex);

	return 1;
}

static
int
xb_stream_write_eof(xb_wstream_file_t *file)
{
	/* Chunk magic + flags + chunk type + path_len + path */
	uchar		tmpbuf[sizeof(XB_STREAM_CHUNK_MAGIC) - 1 + 1 + 1 + 4 +
			       FN_REFLEN];
	uchar		*ptr;
	xb_wstream_t	*stream = file->stream;
	File		fd = stream->fd;

	pthread_mutex_lock(&stream->mutex);

	/* Write xbstream header */
	ptr = tmpbuf;

	/* Chunk magic */
	memcpy(ptr, XB_STREAM_CHUNK_MAGIC, sizeof(XB_STREAM_CHUNK_MAGIC) - 1);
	ptr += sizeof(XB_STREAM_CHUNK_MAGIC) - 1;

	*ptr++ = 0;                              /* Chunk flags */

	*ptr++ = (uchar) XB_CHUNK_TYPE_EOF;      /* Chunk type */

	int4store(ptr, file->path_len);          /* Path length */
	ptr += 4;

	memcpy(ptr, file->path, file->path_len); /* Path */
	ptr += file->path_len;

	xb_ad(ptr <= tmpbuf + sizeof(tmpbuf));

	F_WRITE(tmpbuf, (ulonglong) (ptr - tmpbuf));;

	pthread_mutex_unlock(&stream->mutex);

	return 0;
err:

	pthread_mutex_unlock(&stream->mutex);

	return 1;
}
