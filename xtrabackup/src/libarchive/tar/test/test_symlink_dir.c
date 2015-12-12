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
#include "test.h"
__FBSDID("$FreeBSD: src/usr.bin/tar/test/test_symlink_dir.c,v 1.1 2008/09/14 02:16:04 kientzle Exp $");

/*
 * tar -x -P should follow existing symlinks for dirs, but not other
 * content.  Plain tar -x should remove symlinks when they're in the
 * way of a dir extraction.
 */

static int
mkfile(const char *name, int mode, const char *contents, size_t size)
{
	FILE *f = fopen(name, "wb");
	size_t written;

	(void)mode; /* UNUSED */
	if (f == NULL)
		return (-1);
	written = fwrite(contents, 1, size, f);
	fclose(f);
	if (size != written)
		return (-1);
	return (0);
}

DEFINE_TEST(test_symlink_dir)
{
	assertUmask(0);

	assertMakeDir("source", 0755);
	assertEqualInt(0, mkfile("source/file", 0755, "a", 1));
	assertEqualInt(0, mkfile("source/file2", 0755, "ab", 2));
	assertMakeDir("source/dir", 0755);
	assertMakeDir("source/dir/d", 0755);
	assertEqualInt(0, mkfile("source/dir/f", 0755, "abc", 3));
	assertMakeDir("source/dir2", 0755);
	assertMakeDir("source/dir2/d2", 0755);
	assertEqualInt(0, mkfile("source/dir2/f2", 0755, "abcd", 4));
	assertMakeDir("source/dir3", 0755);
	assertMakeDir("source/dir3/d3", 0755);
	assertEqualInt(0, mkfile("source/dir3/f3", 0755, "abcde", 5));

	assertEqualInt(0,
	    systemf("%s -cf test.tar -C source dir dir2 dir3 file file2",
		testprog));

	/*
	 * Extract with -x and without -P.
	 */
	assertMakeDir("dest1", 0755);
	/* "dir" is a symlink to an existing "dest1/real_dir" */
	assertMakeDir("dest1/real_dir", 0755);
	if (canSymlink()) {
		assertMakeSymlink("dest1/dir", "real_dir");
		/* "dir2" is a symlink to a non-existing "real_dir2" */
		assertMakeSymlink("dest1/dir2", "real_dir2");
	} else {
		skipping("some symlink checks");
	}
	/* "dir3" is a symlink to an existing "non_dir3" */
	assertEqualInt(0, mkfile("dest1/non_dir3", 0755, "abcdef", 6));
	if (canSymlink())
		assertMakeSymlink("dest1/dir3", "non_dir3");
	/* "file" is a symlink to existing "real_file" */
	assertEqualInt(0, mkfile("dest1/real_file", 0755, "abcdefg", 7));
	if (canSymlink()) {
		assertMakeSymlink("dest1/file", "real_file");
		/* "file2" is a symlink to non-existing "real_file2" */
		assertMakeSymlink("dest1/file2", "real_file2");
	}
	assertEqualInt(0, systemf("%s -xf test.tar -C dest1", testprog));

	/* dest1/dir symlink should be replaced */
	failure("symlink to dir was followed when it shouldn't be");
	assertIsDir("dest1/dir", -1);
	/* dest1/dir2 symlink should be replaced */
	failure("Broken symlink wasn't replaced with dir");
	assertIsDir("dest1/dir2", -1);
	/* dest1/dir3 symlink should be replaced */
	failure("Symlink to non-dir wasn't replaced with dir");
	assertIsDir("dest1/dir3", -1);
	/* dest1/file symlink should be replaced */
	failure("Symlink to existing file should be replaced");
	assertIsReg("dest1/file", -1);
	/* dest1/file2 symlink should be replaced */
	failure("Symlink to non-existing file should be replaced");
	assertIsReg("dest1/file2", -1);

	/*
	 * Extract with both -x and -P
	 */
	assertMakeDir("dest2", 0755);
	/* "dir" is a symlink to existing "real_dir" */
	assertMakeDir("dest2/real_dir", 0755);
	if (canSymlink())
		assertMakeSymlink("dest2/dir", "real_dir");
	/* "dir2" is a symlink to a non-existing "real_dir2" */
	if (canSymlink())
		assertMakeSymlink("dest2/dir2", "real_dir2");
	/* "dir3" is a symlink to an existing "non_dir3" */
	assertEqualInt(0, mkfile("dest2/non_dir3", 0755, "abcdefgh", 8));
	if (canSymlink())
		assertMakeSymlink("dest2/dir3", "non_dir3");
	/* "file" is a symlink to existing "real_file" */
	assertEqualInt(0, mkfile("dest2/real_file", 0755, "abcdefghi", 9));
	if (canSymlink())
		assertMakeSymlink("dest2/file", "real_file");
	/* "file2" is a symlink to non-existing "real_file2" */
	if (canSymlink())
		assertMakeSymlink("dest2/file2", "real_file2");
	assertEqualInt(0, systemf("%s -xPf test.tar -C dest2", testprog));

	/* dest2/dir symlink should be followed */
	if (canSymlink()) {
		assertIsSymlink("dest2/dir", "real_dir");
		assertIsDir("dest2/real_dir", -1);
	}

	/* Contents of 'dir' should be restored */
	assertIsDir("dest2/dir/d", -1);
	assertIsReg("dest2/dir/f", -1);
	assertFileSize("dest2/dir/f", 3);
	/* dest2/dir2 symlink should be removed */
	failure("Broken symlink wasn't replaced with dir");
	assertIsDir("dest2/dir2", -1);
	/* dest2/dir3 symlink should be removed */
	failure("Symlink to non-dir wasn't replaced with dir");
	assertIsDir("dest2/dir3", -1);
	/* dest2/file symlink should be removed;
	 * even -P shouldn't follow symlinks for files */
	failure("Symlink to existing file should be removed");
	assertIsReg("dest2/file", -1);
	/* dest2/file2 symlink should be removed */
	failure("Symlink to non-existing file should be removed");
	assertIsReg("dest2/file2", -1);
}
