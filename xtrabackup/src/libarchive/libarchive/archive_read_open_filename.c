/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "archive_platform.h"
__FBSDID("$FreeBSD: head/lib/libarchive/archive_read_open_filename.c 201093 2009-12-28 02:28:44Z kientzle $");

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_IO_H
#include <io.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "archive.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

struct read_file_data {
	int	 fd;
	size_t	 block_size;
	void	*buffer;
	mode_t	 st_mode;  /* Mode bits for opened file. */
	char	 can_skip; /* This file supports skipping. */
	char	 filename[1]; /* Must be last! */
};

static int	file_close(struct archive *, void *);
static ssize_t	file_read(struct archive *, void *, const void **buff);
#if ARCHIVE_API_VERSION < 2
static ssize_t	file_skip(struct archive *, void *, size_t request);
#else
static off_t	file_skip(struct archive *, void *, off_t request);
#endif

int
archive_read_open_file(struct archive *a, const char *filename,
    size_t block_size)
{
	return (archive_read_open_filename(a, filename, block_size));
}

int
archive_read_open_filename(struct archive *a, const char *filename,
    size_t block_size)
{
	struct stat st;
	struct read_file_data *mine;
	void *b;
	int fd;

	archive_clear_error(a);
	if (filename == NULL || filename[0] == '\0') {
		/* We used to invoke archive_read_open_fd(a,0,block_size)
		 * here, but that doesn't (and shouldn't) handle the
		 * end-of-file flush when reading stdout from a pipe.
		 * Basically, read_open_fd() is intended for folks who
		 * are willing to handle such details themselves.  This
		 * API is intended to be a little smarter for folks who
		 * want easy handling of the common case.
		 */
		filename = ""; /* Normalize NULL to "" */
		fd = 0;
#if defined(__CYGWIN__) || defined(_WIN32)
		setmode(0, O_BINARY);
#endif
	} else {
		fd = open(filename, O_RDONLY | O_BINARY);
		if (fd < 0) {
			archive_set_error(a, errno,
			    "Failed to open '%s'", filename);
			return (ARCHIVE_FATAL);
		}
	}
	if (fstat(fd, &st) != 0) {
		archive_set_error(a, errno, "Can't stat '%s'", filename);
		return (ARCHIVE_FATAL);
	}

	mine = (struct read_file_data *)calloc(1,
	    sizeof(*mine) + strlen(filename));
	b = malloc(block_size);
	if (mine == NULL || b == NULL) {
		archive_set_error(a, ENOMEM, "No memory");
		free(mine);
		free(b);
		return (ARCHIVE_FATAL);
	}
	strcpy(mine->filename, filename);
	mine->block_size = block_size;
	mine->buffer = b;
	mine->fd = fd;
	/* Remember mode so close can decide whether to flush. */
	mine->st_mode = st.st_mode;
	/* If we're reading a file from disk, ensure that we don't
	   overwrite it with an extracted file. */
	if (S_ISREG(st.st_mode)) {
		archive_read_extract_set_skip_file(a, st.st_dev, st.st_ino);
		/*
		 * Enabling skip here is a performance optimization
		 * for anything that supports lseek().  On FreeBSD
		 * (and probably many other systems), only regular
		 * files and raw disk devices support lseek() (on
		 * other input types, lseek() returns success but
		 * doesn't actually change the file pointer, which
		 * just completely screws up the position-tracking
		 * logic).  In addition, I've yet to find a portable
		 * way to determine if a device is a raw disk device.
		 * So I don't see a way to do much better than to only
		 * enable this optimization for regular files.
		 */
		mine->can_skip = 1;
	}
	return (archive_read_open2(a, mine,
		NULL, file_read, file_skip, file_close));
}

static ssize_t
file_read(struct archive *a, void *client_data, const void **buff)
{
	struct read_file_data *mine = (struct read_file_data *)client_data;
	ssize_t bytes_read;

	*buff = mine->buffer;
	for (;;) {
		bytes_read = read(mine->fd, mine->buffer, mine->block_size);
		if (bytes_read < 0) {
			if (errno == EINTR)
				continue;
			else if (mine->filename[0] == '\0')
				archive_set_error(a, errno, "Error reading stdin");
			else
				archive_set_error(a, errno, "Error reading '%s'",
				    mine->filename);
		}
		return (bytes_read);
	}
}

#if ARCHIVE_API_VERSION < 2
static ssize_t
file_skip(struct archive *a, void *client_data, size_t request)
#else
static off_t
file_skip(struct archive *a, void *client_data, off_t request)
#endif
{
	struct read_file_data *mine = (struct read_file_data *)client_data;
	off_t old_offset, new_offset;

	if (!mine->can_skip) /* We can't skip, so ... */
		return (0); /* ... skip zero bytes. */

	/* Reduce request to the next smallest multiple of block_size */
	request = (request / mine->block_size) * mine->block_size;
	if (request == 0)
		return (0);

	/*
	 * Hurray for lazy evaluation: if the first lseek fails, the second
	 * one will not be executed.
	 */
	if (((old_offset = lseek(mine->fd, 0, SEEK_CUR)) < 0) ||
	    ((new_offset = lseek(mine->fd, request, SEEK_CUR)) < 0))
	{
		/* If skip failed once, it will probably fail again. */
		mine->can_skip = 0;

		if (errno == ESPIPE)
		{
			/*
			 * Failure to lseek() can be caused by the file
			 * descriptor pointing to a pipe, socket or FIFO.
			 * Return 0 here, so the compression layer will use
			 * read()s instead to advance the file descriptor.
			 * It's slower of course, but works as well.
			 */
			return (0);
		}
		/*
		 * There's been an error other than ESPIPE. This is most
		 * likely caused by a programmer error (too large request)
		 * or a corrupted archive file.
		 */
		if (mine->filename[0] == '\0')
			/*
			 * Should never get here, since lseek() on stdin ought
			 * to return an ESPIPE error.
			 */
			archive_set_error(a, errno, "Error seeking in stdin");
		else
			archive_set_error(a, errno, "Error seeking in '%s'",
			    mine->filename);
		return (-1);
	}
	return (new_offset - old_offset);
}

static int
file_close(struct archive *a, void *client_data)
{
	struct read_file_data *mine = (struct read_file_data *)client_data;

	(void)a; /* UNUSED */

	/* Only flush and close if open succeeded. */
	if (mine->fd >= 0) {
		/*
		 * Sometimes, we should flush the input before closing.
		 *   Regular files: faster to just close without flush.
		 *   Devices: must not flush (user might need to
		 *      read the "next" item on a non-rewind device).
		 *   Pipes and sockets:  must flush (otherwise, the
		 *      program feeding the pipe or socket may complain).
		 * Here, I flush everything except for regular files and
		 * device nodes.
		 */
		if (!S_ISREG(mine->st_mode)
		    && !S_ISCHR(mine->st_mode)
		    && !S_ISBLK(mine->st_mode)) {
			ssize_t bytesRead;
			do {
				bytesRead = read(mine->fd, mine->buffer,
				    mine->block_size);
			} while (bytesRead > 0);
		}
		/* If a named file was opened, then it needs to be closed. */
		if (mine->filename[0] != '\0')
			close(mine->fd);
	}
	free(mine->buffer);
	free(mine);
	return (ARCHIVE_OK);
}
