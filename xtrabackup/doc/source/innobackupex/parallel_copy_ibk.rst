.. _parallel-ibk:

=====================================================================
 Accelerating with :option:`--parallel` copy and `--compress-threads`
=====================================================================

When performing a local backup, multiple files can be copied concurrently by using the :option:`--parallel` option. This option specifies the number of threads created by |xtrabackup| to copy data files.

To take advantage of this option whether the multiple tablespaces option must be enabled (:term:`innodb_file_per_table`) or the shared tablespace must be stored in multiple :term:`ibdata` files with the :term:`innodb_data_file_path` option.  Having multiple files for the database (or splitting one into many) doesn't have a measurable impact on performance.


As this feature is implemented **at a file level**, concurrent file transfer can sometimes increase I/O throughput when doing a backup on highly fragmented data files, due to the overlap of a greater number of random read requests. You should consider tuning the filesystem also to obtain the maximum performance (e.g. checking fragmentation). 

If the data is stored on a single file, this option will have no effect.

To use this feature, simply add the option to a local backup, for example: ::

  $ innobackupex --parallel=4 /path/to/backup

By using the |xbstream| in streaming backups you can additionally speed up the compression process by using the :option:`--compress-threads` option. This option specifies the number of threads created by |xtrabackup| for  for parallel data compression. The default value for this option is 1.

To use this feature, simply add the option to a local backup, for example ::

 $ innobackupex --stream=xbstream --compress --compress-threads=4 ./ > backup.xbstream 

Before applying logs, compressed files will need to be uncompressed.

