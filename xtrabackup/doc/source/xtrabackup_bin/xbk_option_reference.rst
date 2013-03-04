============================================
 The :program:`xtrabackup` Option Reference
============================================

.. program:: xtrabackup

This page documents all of the command-line options for the :program:`xtrabackup` binary.

Options
=======

.. option:: --print-defaults

   Print the program argument list and exit. Must be given as the first option on the command-line.

.. option:: --no-defaults

   Don't read default options from any option file. Must be given as the first option on the command-line.

.. option:: --defaults-file=#

   Only read default options from the given file. Must be given as the first option on the command-line. Must be a real file; it cannot be a symbolic link.

.. option:: --defaults-extra-file=#

   Read this file after the global files are read. Must be given as the first option on the command-line.

.. option:: --apply-log-only

   This option causes only the redo stage to be performed when preparing a backup. It is very important for incremental backups.

.. option:: --backup

   Make a backup and place it in :option:`--target-dir`. See :doc:`Creating a backup <creating_a_backup>`.

.. option:: --compress 

   This option tells |xtrabackup| to compress all output data, including the transaction log file and meta data files, using the specified compression algorithm. The only currently supported algorithm is 'quicklz'. The resulting files have the qpress archive format, i.e. every `*.qp` file produced by xtrabackup is essentially a one-file qpress archive and can be extracted and uncompressed by the `qpress <http://www.quicklz.com/>`_  file archiver.

.. option:: --compress-threads 

   This option specifies the number of worker threads used by |xtrabackup| for parallel data compression. This option defaults to 1. Parallel compression ('--compress-threads') can be used together with parallel file copying ('--parallel'). For example, '--parallel=4 --compress --compress-threads=2' will create 4 IO threads that will read the data and pipe it to 2 compression threads. New algorithms (gzip, bzip2, etc.) may be added later with minor efforts.

.. option:: --create-ib-logfile

   This option is not currently implemented. To create the InnoDB log files, you must prepare the backup twice at present.

.. option:: --datadir

   The source directory for the backup. This should be the same as the datadir for your MySQL server, so it should be read from :file:`my.cnf` if that exists; otherwise you must specify it on the command line.

.. option:: --export

   Create files necessary for exporting tables. See :doc:`Exporting and Importing Tables <exporting_importing_tables>`.

.. option:: --extra-lsndir=name 

   (for --backup): save an extra copy of the xtrabackup_checkpoints file in this directory.

.. option:: --incremental-basedir

   When creating an incremental backup, this is the directory containing the full backup that is the base dataset for the incremental backups.

.. option:: --incremental-dir

   When preparing an incremental backup, this is the directory where the incremental backup is combined with the full backup to make a new full backup.

.. option:: --incremental-lsn=name

   When creating an incremental backup, you can specify the log sequence number (:term:`LSN`) instead of specifying :option:`--incremental-basedir`. For databases created by *MySQL* and *Percona Server* 5.0-series versions, specify the :term:`LSN` as two 32-bit integers in high:low format. For databases created in 5.1 and later, specify the :term:`LSN` as a single 64-bit integer.  ##ATTENTION##: If a wrong LSN value is specified, it is impossible to diagnose this, causing the backup to be unusable. Be careful!

.. option:: --innodb-miscellaneous

   There is a large group of InnoDB options that are normally read from the :term:`my.cnf` configuration file, so that xtrabackup boots up its embedded InnoDB in the same configuration as your current server. You normally do not need to specify these explicitly. These options have the same behavior that they have in InnoDB or XtraDB. They are as follows: ::

    --innodb-adaptive-hash-index
    --innodb-additional-mem-pool-size
    --innodb-autoextend-increment
    --innodb-buffer-pool-size
    --innodb-checksums
    --innodb-data-file-path
    --innodb-data-home-dir
    --innodb-doublewrite-file
    --innodb-doublewrite
    --innodb-extra-undoslots
    --innodb-fast-checksum
    --innodb-file-io-threads
    --innodb-file-per-table
    --innodb-flush-log-at-trx-commit
    --innodb-flush-method
    --innodb-force-recovery
    --innodb-io-capacity
    --innodb-lock-wait-timeout
    --innodb-log-buffer-size
    --innodb-log-files-in-group
    --innodb-log-file-size
    --innodb-log-group-home-dir
    --innodb-max-dirty-pages-pct
    --innodb-open-files
    --innodb-page-size
    --innodb-read-io-threads
    --innodb-write-io-threads

.. option:: --defaults-group

   This option is to set the group which should be read from the configuration file. This is used by innobackupex if you use the `--defaults-group` option. It is needed for mysqld_multi deployments.

.. option:: --log-stream

   Makes xtrabackup not copy data files, and output the contents of the InnoDB log files to STDOUT until the :option:`--suspend-at-end` file is deleted. This option enables :option:`--suspend-at-end` automatically.

.. option:: --stream=name 

   Stream all backup files to the standard output in the specified format. Currently supported formats are 'xbstream' and 'tar'.

.. option:: --prepare

   Makes :program:`xtrabackup` perform recovery on a backup created with :option:`--backup`, so that it is ready to use. See :doc:`preparing a backup <preparing_the_backup>`.

.. option:: --print-param

   Makes :program:`xtrabackup` print out parameters that can be used for copying the data files back to their original locations to restore them. See :ref:`scripting-xtrabackup`.

.. option:: --stats

   Causes :program:`xtrabackup` to scan the specified data files and print out index statistics.

.. option:: --suspend-at-end

   Causes :program:`xtrabackup` to create a file called :file:`xtrabackup_suspended` in the :option:`--target-dir`. Instead of exiting after copying data files, :program:`xtrabackup` continues to copy the log file, and waits until the :file:`xtrabackup_suspended` file is deleted. This enables xtrabackup and other programs to coordinate their work. See :ref:`scripting-xtrabackup`.

.. option:: --tables-file=name

   A file containing one table name per line, in databasename.tablename format. The backup will be limited to the specified tables. See :ref:`scripting-xtrabackup`.

.. option:: --tables=name

   A regular expression against which the full tablename, in ``databasename.tablename`` format, is matched. If the name matches, the table is backed up. See :doc:`partial backups <partial_backups>`.

.. option:: --target-dir=name

   This option specifies the destination directory for the backup. If the directory does not exist, :program:`xtrabackup` creates it. If the directory does exist and is empty, :program:`xtrabackup` will succeed. :program:`xtrabackup` will not overwrite existing files, however; it will fail with operating system error 17, ``file exists``.

   If this option is a relative path, it is interpreted as being relative to the current working directory from which :program:`xtrabackup` is executed.

.. option:: --throttle=#

   This option limits :option:`--backup` to the specified number of read+write pairs of operations per second. See :doc:`throttling a backup <throttling_backups>`.

.. option:: --tmpdir=name

   This option is currently not used for anything except printing out the correct tmpdir parameter when :option:`--print-param` is used.

.. option:: --use-memory=#

   This option affects how much memory is allocated for preparing a backup with :option:`--prepare`, or analyzing statistics with :option:`--stats`. Its purpose is similar to :term:`innodb_buffer_pool_size`. It does not do the same thing as the similarly named option in Oracle's InnoDB Hot Backup tool. The default value is 100MB, and if you have enough available memory, 1GB to 2GB is a good recommended value.

.. option:: --parallel=#

   This option specifies the number of threads to use to copy multiple data files concurrently when creating a backup. The default value is 1 (i.e., no concurrent transfer).

.. option:: --version

   This option prints |xtrabackup| version and exits.
