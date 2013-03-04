/******************************************************
Copyright (c) 2011 Percona Ireland Ltd.

Streaming implementation for XtraBackup.

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
#include <archive.h>
#include <archive_entry.h>
#include "common.h"
#include "datasink.h"
#include "xbstream.h"

typedef struct {
	struct archive	*archive;
	xb_wstream_t	*xbstream;
} ds_stream_ctxt_t;

typedef struct {
	struct archive_entry	*entry;
	xb_wstream_file_t	*xbstream_file;
	ds_stream_ctxt_t	*stream_ctxt;
} ds_stream_file_t;

extern xb_stream_fmt_t xtrabackup_stream_fmt;

/***********************************************************************
General streaming interface */

static ds_ctxt_t *stream_init(const char *root);
static ds_file_t *stream_open(ds_ctxt_t *ctxt, const char *path,
			      MY_STAT *mystat);
static int stream_write(ds_file_t *file, const void *buf, size_t len);
static int stream_close(ds_file_t *file);
static void stream_deinit(ds_ctxt_t *ctxt);

datasink_t datasink_stream = {
	&stream_init,
	&stream_open,
	&stream_write,
	&stream_close,
	&stream_deinit
};

static
ds_ctxt_t *
stream_init(const char *root __attribute__((unused)))
{
	ds_ctxt_t		*ctxt;
	ds_stream_ctxt_t	*stream_ctxt;

	ctxt = my_malloc(sizeof(ds_ctxt_t) + sizeof(ds_stream_ctxt_t),
			 MYF(MY_FAE));
	stream_ctxt = (ds_stream_ctxt_t *)(ctxt + 1);

	if (xtrabackup_stream_fmt == XB_STREAM_FMT_XBSTREAM) {
		/* xbstream format */
		xb_wstream_t *xbstream;

		xbstream = xb_stream_write_new();
		if (xbstream == NULL) {
			msg("xb_stream_write_new() failed.\n");
			goto err;
		}
		stream_ctxt->xbstream = xbstream;
	} else {
		/* Tar format */
		struct archive		*a;

		a = archive_write_new();
		if (a == NULL) {
			msg("archive_write_new() failed.\n");
			goto err;
		}

		if (archive_write_set_compression_none(a) != ARCHIVE_OK ||
		    archive_write_set_format_pax_restricted(a) != ARCHIVE_OK) {
			msg("failed to set libarchive stream options: %s\n",
			    archive_error_string(a));
			archive_write_finish(a);
			goto err;
		}

		if (archive_write_open_fd(a, fileno(stdout)) != ARCHIVE_OK) {
			msg("cannot open output stream.\n");
			goto err;
		}
		stream_ctxt->archive = a;
	}


	ctxt->datasink = &datasink_stream;
	ctxt->ptr = stream_ctxt;

	return ctxt;

err:
	MY_FREE(ctxt);
	return NULL;
}

static
ds_file_t *
stream_open(ds_ctxt_t *ctxt, const char *path, MY_STAT *mystat)
{
	ds_file_t		*file;
	ds_stream_file_t	*stream_file;
	ds_stream_ctxt_t	*stream_ctxt;

	stream_ctxt = (ds_stream_ctxt_t *) ctxt->ptr;

	file = (ds_file_t *) my_malloc(sizeof(ds_file_t) +
				       sizeof(ds_stream_file_t),
				       MYF(MY_FAE));
	stream_file = (ds_stream_file_t *) (file + 1);

	if (xtrabackup_stream_fmt == XB_STREAM_FMT_XBSTREAM) {
		/* xbstream format */

		xb_wstream_t		*xbstream;
		xb_wstream_file_t	*xbstream_file;

		xbstream = stream_ctxt->xbstream;

		xbstream_file = xb_stream_write_open(xbstream, path, mystat);
		if (xbstream_file == NULL) {
			msg("xb_stream_write_open() failed.\n");
			goto err;
		}

		stream_file->xbstream_file = xbstream_file;
	} else {
		/* Tar format */

		struct archive		*a;
		struct archive_entry	*entry;

		a = stream_ctxt->archive;

		entry = archive_entry_new();
		if (entry == NULL) {
			msg("archive_entry_new() failed.\n");
			goto err;
		}

		archive_entry_set_size(entry, mystat->st_size);
		archive_entry_set_mode(entry, 0660);
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_pathname(entry, path);
		archive_entry_set_mtime(entry, mystat->st_mtime, 0);

		if (archive_write_header(a, entry) != ARCHIVE_OK) {
			msg("archive_write_header() failed.\n");
			archive_entry_free(entry);
			goto err;
		}
		stream_file->entry = entry;
	}

	stream_file->stream_ctxt = stream_ctxt;
	file->ptr = stream_file;
	file->path = "<STDOUT>";

	return file;

err:
	MY_FREE(file);

	return NULL;
}

static
int
stream_write(ds_file_t *file, const void *buf, size_t len)
{
	ds_stream_file_t	*stream_file;

	stream_file = (ds_stream_file_t *) file->ptr;

	if (xtrabackup_stream_fmt == XB_STREAM_FMT_XBSTREAM) {
		/* xbstream format */

		xb_wstream_file_t	*xbstream_file;

		xbstream_file = stream_file->xbstream_file;

		if (xb_stream_write_data(xbstream_file, buf, len)) {
			msg("xb_stream_write_data() failed.\n");
			return 1;
		}
	} else {
		/* Tar format */

		struct archive		*a;

		a = stream_file->stream_ctxt->archive;

		if (archive_write_data(a, buf, len) < 0) {
			msg("archive_write_data() failed: %s (errno = %d)\n",
			    archive_error_string(a), archive_errno(a));
			return 1;
		}
	}

	return 0;
}

static
int
stream_close(ds_file_t *file)
{
	ds_stream_file_t	*stream_file;
	int			rc = 0;

	stream_file = (ds_stream_file_t *)file->ptr;

	if (xtrabackup_stream_fmt == XB_STREAM_FMT_XBSTREAM) {
		rc = xb_stream_write_close(stream_file->xbstream_file);
	} else {
		archive_entry_free(stream_file->entry);
	}

	MY_FREE(file);

	return rc;
}

static
void
stream_deinit(ds_ctxt_t *ctxt)
{
	ds_stream_ctxt_t	*stream_ctxt;

	stream_ctxt = (ds_stream_ctxt_t *) ctxt->ptr;

	if (xtrabackup_stream_fmt == XB_STREAM_FMT_XBSTREAM) {
		if (xb_stream_write_done(stream_ctxt->xbstream)) {
			msg("xb_stream_done() failed.\n");
		}
	} else {
		struct archive *a;

		a = stream_ctxt->archive;

		if (archive_write_close(a) != ARCHIVE_OK) {
			msg("archive_write_close() failed.\n");
		}
		archive_write_finish(a);
	}

	MY_FREE(ctxt);
}
