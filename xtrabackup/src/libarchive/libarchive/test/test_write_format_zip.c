/*-
 * Copyright (c) 2003-2008 Tim Kientzle
 * Copyright (c) 2008 Anselm Strauss
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

/*
 * Development supported by Google Summer of Code 2008.
 */

/* TODO: reader does not yet restore permissions. */

#include "test.h"
__FBSDID("$FreeBSD: head/lib/libarchive/test/test_write_format_zip.c 201247 2009-12-30 05:59:21Z kientzle $");

DEFINE_TEST(test_write_format_zip)
{
	char filedata[64];
	struct archive_entry *ae;
	struct archive *a;
	size_t used;
	size_t buffsize = 1000000;
	char *buff;
	const char *compression_type;

	buff = malloc(buffsize);

	/* Create a new archive in memory. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_format_zip(a));
#ifdef HAVE_ZLIB_H
	compression_type = "zip:compression=deflate";
#else
	compression_type = "zip:compression=store";
#endif
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_format_options(a, compression_type));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_compression_none(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_open_memory(a, buff, buffsize, &used));

	/*
	 * Write a file to it.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_mtime(ae, 1, 10);
	assertEqualInt(1, archive_entry_mtime(ae));
	assertEqualInt(10, archive_entry_mtime_nsec(ae));
	archive_entry_copy_pathname(ae, "file");
	assertEqualString("file", archive_entry_pathname(ae));
	archive_entry_set_mode(ae, S_IFREG | 0755);
	assertEqualInt((S_IFREG | 0755), archive_entry_mode(ae));
	archive_entry_set_size(ae, 8);

	assertEqualInt(0, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualInt(8, archive_write_data(a, "12345678", 9));
	assertEqualInt(0, archive_write_data(a, "1", 1));

	/*
	 * Write another file to it.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_mtime(ae, 1, 10);
	assertEqualInt(1, archive_entry_mtime(ae));
	assertEqualInt(10, archive_entry_mtime_nsec(ae));
	archive_entry_copy_pathname(ae, "file2");
	assertEqualString("file2", archive_entry_pathname(ae));
	archive_entry_set_mode(ae, S_IFREG | 0755);
	assertEqualInt((S_IFREG | 0755), archive_entry_mode(ae));
	archive_entry_set_size(ae, 4);

	assertEqualInt(ARCHIVE_OK, archive_write_header(a, ae));
	archive_entry_free(ae);
	assertEqualInt(4, archive_write_data(a, "1234", 5));

	/*
	 * Write a directory to it.
	 */
	assert((ae = archive_entry_new()) != NULL);
	archive_entry_set_mtime(ae, 11, 110);
	archive_entry_copy_pathname(ae, "dir");
	archive_entry_set_mode(ae, S_IFDIR | 0755);
	archive_entry_set_size(ae, 512);

	assertEqualIntA(a, ARCHIVE_OK, archive_write_header(a, ae));
	failure("size should be zero so that applications know not to write");
	assertEqualInt(0, archive_entry_size(ae));
	archive_entry_free(ae);
	assertEqualIntA(a, 0, archive_write_data(a, "12345678", 9));

	/* Close out the archive. */
	assertEqualInt(ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_finish(a));

	/*
	 * Now, read the data back.
	 */
	ae = NULL;
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_support_compression_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_open_memory(a, buff, used));

	/*
	 * Read and verify first file.
	 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt(1, archive_entry_mtime(ae));
	/* Zip doesn't store high-resolution mtime. */
	assertEqualInt(0, archive_entry_mtime_nsec(ae));
	assertEqualInt(0, archive_entry_atime(ae));
	assertEqualInt(0, archive_entry_ctime(ae));
	assertEqualString("file", archive_entry_pathname(ae));
	//assertEqualInt((S_IFREG | 0755), archive_entry_mode(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assertEqualIntA(a, 8,
	    archive_read_data(a, filedata, sizeof(filedata)));
	assertEqualMem(filedata, "12345678", 8);


	/*
	 * Read the second file back.
	 */
	if (!assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae))){
		free(buff);
		return;
	}
	assertEqualInt(1, archive_entry_mtime(ae));
	assertEqualInt(0, archive_entry_mtime_nsec(ae));
	assertEqualInt(0, archive_entry_atime(ae));
	assertEqualInt(0, archive_entry_ctime(ae));
	assertEqualString("file2", archive_entry_pathname(ae));
	//assert((S_IFREG | 0755) == archive_entry_mode(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assertEqualIntA(a, 4,
	    archive_read_data(a, filedata, sizeof(filedata)));
	assertEqualMem(filedata, "1234", 4);

	/*
	 * Read the dir entry back.
	 */
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt(11, archive_entry_mtime(ae));
	assertEqualInt(0, archive_entry_mtime_nsec(ae));
	assertEqualInt(0, archive_entry_atime(ae));
	assertEqualInt(0, archive_entry_ctime(ae));
	assertEqualString("dir/", archive_entry_pathname(ae));
	//assertEqualInt((S_IFDIR | 0755), archive_entry_mode(ae));
	assertEqualInt(0, archive_entry_size(ae));
	assertEqualIntA(a, 0, archive_read_data(a, filedata, 10));

	/* Verify the end of the archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_finish(a));
	free(buff);
}
