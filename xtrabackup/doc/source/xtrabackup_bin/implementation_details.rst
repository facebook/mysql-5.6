========================
 Implementation Details
========================

This page contains notes on various internal aspects of the |xtrabackup| tool's operation.

File Permissions
================

|xtrabackup| opens the source data files in read-write mode, although it does not modify the files. This means that you must run |xtrabackup| as a user who has permission to write the data files. The reason for opening the files in read-write mode is that |xtrabackup| uses the embedded |InnoDB| libraries to open and read the files, and |InnoDB| opens them in read-write mode because it normally assumes it is going to write to them.

Tuning the OS Buffers
=====================

Because |xtrabackup| reads large amounts of data from the filesystem, it uses ``posix_fadvise()`` where possible, to instruct the operating system not to try to cache the blocks it reads from disk. Without this hint, the operating system would prefer to cache the blocks, assuming that ``xtrabackup`` is likely to need them again, which is not the case. Caching such large files can place pressure on the operating system's virtual memory and cause other processes, such as the database server, to be swapped out. The ``xtrabackup`` tool avoids this with the following hint on both the source and destination files: ::

  posix_fadvise(file, 0, 0, POSIX_FADV_DONTNEED)

In addition, xtrabackup asks the operating system to perform more aggressive read-ahead optimizations on the source files: ::

  posix_fadvise(file, 0, 0, POSIX_FADV_SEQUENTIAL)

Copying Data Files
==================

When copying the data files to the target directory, |xtrabackup| reads and writes 1MB of data at a time. This is not configurable. When copying the log file, |xtrabackup| reads and writes 512 bytes at a time. This is also not possible to configure, and matches |InnoDB| 's behavior.

After reading from the files, ``xtrabackup`` iterates over the 1MB buffer a page at a time, and checks for page corruption on each page with InnoDB's ``buf_page_is_corrupted()`` function. If the page is corrupt, it re-reads and retries up to 10 times for each page. It skips this check on the doublewrite buffer.
