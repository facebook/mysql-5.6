===================================
 Streaming and Compressing Backups
===================================

Streaming mode, supported by |XtraBackup|, sends backup to ``STDOUT`` in special ``tar`` or |xbstream| format instead of copying files to the backup directory.

This allows to pipe the stream to other programs, providing great flexibility to the output of it. For example, compression is achieved by piping the output to a compression utility. One of the benefits of streaming backups and using Unix pipes is that the backups can be automatically encrypted. 

To use the streaming feature, you must use the :option:`--stream`, providing the format of the stream (``tar`` or ``xbstream`` ) and where should the store the temporary files::

 $ innobackupex --stream=tar /tmp

|innobackupex| starts |xtrabackup| in :option:`--log-stream` mode in a child process, and redirects its log to a temporary file. It then uses |xbstream| to stream all of the data files to ``STDOUT``, in a special ``xbstream`` format. See :doc:`../xbstream/xbstream` for details. After it finishes streaming all of the data files to ``STDOUT``, it stops xtrabackup and streams the saved log file too.

When compression is enabled, |xtrabackup| compresses all output data, including the transaction log file and meta data files, using the specified compression algorithm. The only currently supported algorithm is 'quicklz'. The resulting files have the qpress archive format, i.e. every \*.qp file produced by xtrabackup is essentially a one-file qpress archive and can be extracted and uncompressed by the `qpress file archiver <http://www.quicklz.com/>`_. New algorithms (gzip, bzip2, etc.) may be added later with minor efforts.

Using |xbstream| as a stream option, backups can be copied and compressed in parallel which can significantly speed up the backup process.  

Examples using xbstream
=======================

To store the backup in one archive it directly: :: 

 $ innobackupex --stream=xbstream /root/backup/ > /root/backup/backup.xbstream

To stream and compress the backup: ::  

 $ innobackupex --stream=xbstream --compress /root/backup/ > /root/backup/backup.xbstream

To unpack the backup to the /root/backup/ directory: ::  

 $ xbstream -x <  backup.xbstream -C /root/backup/

For sending backup compressed directly to another host and unpacking it: :: 

 $ innobackupex --compress --stream=xbstream /root/backup/ | ssh user@otherhost "xbstream -x -C /root/backup/" 

Examples using tar
==================

To store the backup in one archive it directly :: 

 $ innobackupex --stream=tar /root/backup/ > /root/backup/out.tar

For sending it directly to another host by ::

 $ innobackupex --stream=tar ./ | ssh user@destination \ "cat - > /data/backups/backup.tar"

.. warning::  To extract |XtraBackup|'s archive you **must** use |tar| with ``-i`` option::

  $ tar -xizf backup.tar.gz

Choosing the compression tool that best suits you: :: 

 $ innobackupex --stream=tar ./ | gzip - > backup.tar.gz
 $ innobackupex --stream=tar ./ | bzip2 - > backup.tar.bz2

Note that the streamed backup will need to be prepared before restoration. Streaming mode does not prepare the backup.

