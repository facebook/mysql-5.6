======================
 The xbstream Binary
======================

To support simultaneous compression and streaming, a new custom streaming format called xbstream was introduced to XtraBackup in addition to the TAR format. That was required to overcome some limitations of traditional archive formats such as tar, cpio and others which did not allow streaming dynamically generated files, for example dynamically compressed files. Other advantages of xbstream over traditional streaming/archive format include ability to stream multiple files concurrently (so it is possible to use streaming in the xbstream format together with the --parallel option) and more compact data storage. 

This utility has a tar-like interface:

 - with the '-x' option it extracts files from the stream read from its standard input to the current directory unless specified otherwise with the '-C' option.

 - with the '-c' option it streams files specified on the command line to its standard output.

The utility also tries to minimize its impact on the OS page cache by using the appropriate posix_fadvise() calls when available.

When compression is enabled with |xtrabackup| all data is being compressed, including the transaction log file and meta data files, using the specified compression algorithm. The only currently supported algorithm is 'quicklz'. The resulting files have the qpress archive format, i.e. every \*.qp file produced by xtrabackup is essentially a one-file qpress archive and can be extracted and uncompressed by the `qpress file archiver <http://www.quicklz.com/>`_. This means that there is no need to uncompress entire backup to restore a single table as with tar.gz. 

Files can be decompressed using the **qpress** tool that can be downloaded from `here <http://www.quicklz.com/>`_. Qpress supports multi-threaded decompression.
