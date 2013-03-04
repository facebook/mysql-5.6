=============================================
 The :program:`innobackupex` Option Reference
=============================================

.. program:: innobackupex

This page documents all of the command-line options for the :program:`innobackupex` Perl script.


Options
=======

.. option:: --apply-log

   Prepare a backup in ``BACKUP-DIR`` by applying the transaction log file named :file:`xtrabackup_logfile` located in the same directory. Also, create new transaction logs. The InnoDB configuration is read from the file :file:`backup-my.cnf` created by |innobackupex| when the backup was made.

.. option:: --compress

   This option instructs xtrabackup to compress backup copies of InnoDB data files. It is passed directly to the xtrabackup child process. See the :program:`xtrabackup` :doc:`documentation <../xtrabackup_bin/xtrabackup_binary>` for details.

.. option::  --compress-threads

   This option specifies the number of worker threads that will be used for parallel compression. It is passed directly to the xtrabackup child process. See the :program:`xtrabackup` :doc:`documentation <../xtrabackup_bin/xtrabackup_binary>` for details.

.. option:: --copy-back

    Copy all the files in a previously made backup from the backup directory to their original locations.

.. option:: --databases=LIST

   This option specifies the list of databases that |innobackupex| should back up. The option accepts a string argument or path to file that contains the list of databases to back up. The list is of the form "databasename1[.table_name1] databasename2[.table_name2] . . .". If this option is not specified, all databases containing |MyISAM| and |InnoDB| tables will be backed up. Please make sure that --databases contains all of the |InnoDB| databases and tables, so that all of the innodb.frm files are also backed up. In case the list is very long, this can be specified in a file, and the full path of the file can be specified instead of the list. (See option --tables-file.)

.. option:: --defaults-file=[MY.CNF]

   This option accepts a string argument that specifies what file to read the default MySQL options from. It is also passed directly to :program:`xtrabackup` 's defaults-file option. See the :program:`xtrabackup` :doc:`documentation <../xtrabackup_bin/xtrabackup_binary>` for details.

.. option:: --defaults-extra-file=[MY.CNF]

   This option specifies what extra file to read the default |MySQL| options from before the standard defaults-file. The option accepts a string argument. It is also passed directly to xtrabackup's --defaults-extra-file option. See the :program:`xtrabackup` :doc:`documentation <../xtrabackup_bin/xtrabackup_binary>` for details.

.. option:: --defaults-group=GROUP-NAME

   This option accepts a string argument that specifies the group which should be read from the configuration file. This is needed if you use mysqld_multi.

.. option:: --export

   This option is passed directly to :option:`xtrabackup --export` option. It enables exporting individual tables for import into another server. See the |xtrabackup| documentation for details.

.. option:: --extra-lsndir=DIRECTORY

   This option accepts a string argument that specifies the directory in which to save an extra copy of the :file:`xtrabackup_checkpoints` file. It is passed directly to |xtrabackup|'s :option:`--extra-lsndir` option. See the :program:`xtrabackup` documentation for details.

.. option:: --galera-info

   This options creates the ``xtrabackup_galera_info`` file which contains the local node state at the time of the backup. Option should be used when performing the backup of Percona-XtraDB-Cluster.

.. option:: --help

   This option displays a help screen and exits.

.. option:: --host=HOST

   This option accepts a string argument that specifies the host to use when connecting to the database server with TCP/IP. It is passed to the mysql child process without alteration. See :command:`mysql --help` for details.

.. option:: --ibbackup=IBBACKUP-BINARY

   This option accepts a string argument that specifies which |xtrabackup| binary should be used. The string should be the command used to run *XtraBackup*. The option can be useful if the :program:`xtrabackup` binary is not in your search path or working directory and the database server is not accessible at the moment. If this option is not specified, :program:`innobackupex` attempts to determine the binary to use automatically. By default, :program:`xtrabackup` is the command used. When option :option:`--apply-log` is specified, the binary is used whose name is in the file :file:`xtrabackup_binary` in the backup directory, if that file exists, or will attempt to autodetect it. However, if :option:`--copy-back` or :option:`--move-back` is used, :program:`xtrabackup` is used unless other is specified.

.. option:: --include=REGEXP

   This option is a regular expression to be matched against table names in ``databasename.tablename`` format. It is passed directly to |xtrabackup|'s :option:`xtrabackup --tables` option. See the :program:`xtrabackup` documentation for details.

.. option:: --incremental

   This option tells |xtrabackup| to create an incremental backup, rather than a full one. It is passed to the |xtrabackup| child process. When this option is specified, either :option:`--incremental-lsn` or :option:`--incremental-basedir` can also be given. If neither option is given, option :option:`--incremental-basedir` is passed to :program:`xtrabackup` by default, set to the first timestamped backup directory in the backup base directory.

.. option:: --incremental-basedir=DIRECTORY

   This option accepts a string argument that specifies the directory containing the full backup that is the base dataset for the incremental backup. It is used with the :option:`--incremental` option.

.. option:: --incremental-dir=DIRECTORY

   This option accepts a string argument that specifies the directory where the incremental backup will be combined with the full backup to make a new full backup. It is used with the :option:`--incremental` option.

.. option:: --incremental-lsn

   This option accepts a string argument that specifies the log sequence number (:term:`LSN`) to use for the incremental backup. It is used with the :option:`--incremental` option. It is used instead of specifying :option:`--incremental-basedir`. For databases created by *MySQL* and *Percona Server* 5.0-series versions, specify the as two 32-bit integers in high:low format. For databases created in 5.1 and later, specify the LSN as a single 64-bit integer.

.. option:: --move-back

    Move all the files in a previously made backup from the backup directory to their original locations. As this option removes backup files, it must be used with caution.

.. option:: --no-lock

   Use this option to disable table lock with ``FLUSH TABLES WITH READ LOCK``. Use this option to disable table lock with ``FLUSH TABLES WITH READ LOCK``. Use it only if ALL your tables are InnoDB and you **DO NOT CARE** about the binary log position of the backup. This option shouldn't be used if there are any ``DDL`` statements being executed or if any updates are happening on non-InnoDB tables (this includes the system MyISAM tables in the *mysql* database), otherwise it could lead to an inconsistent backup. 
   If you are considering to use :option:`--no-lock` because your backups are failing to acquire the lock, this could be because of incoming replication events preventing the lock from succeeding. Please try using :option:`--safe-slave-backup` to momentarily stop the replication slave thread, this may help the backup to succeed and you then don't need to resort to using this option.

.. option:: --no-timestamp

   This option prevents creation of a time-stamped subdirectory of the ``BACKUP-ROOT-DIR`` given on the command line. When it is specified, the backup is done in ``BACKUP-ROOT-DIR`` instead.

.. option:: --parallel=NUMBER-OF-THREADS

   This option accepts an integer argument that specifies the number of threads the :program:`xtrabackup` child process should use to back up files concurrently.  Note that this option works on file level, that is, if you have several .ibd files, they will be copied in parallel. If you have just single big .ibd file, it will have no effect. It is passed directly to xtrabackup's :option:`xtrabackup --parallel` option. See the :program:`xtrabackup` documentation for details

.. option:: --password=PASSWORD

   This option accepts a string argument specifying the password to use when connecting to the database. It is passed to the :command:`mysql` child process without alteration. See :command:`mysql --help` for details.

.. option:: --port=PORT

   This option accepts a string argument that specifies the port to use when connecting to the database server with TCP/IP. It is passed to the :command:`mysql` child process. It is passed to the :command:`mysql` child process without alteration. See :command:`mysql --help` for details.

.. option:: --redo-only

   This option should be used when preparing the base full backup and when merging all incrementals except the last one. It is passed directly to xtrabackup's :option:`xtrabackup --apply-log-only` option. This forces :program:`xtrabackup` to skip the "rollback" phase and do a "redo" only. This is necessary if the backup will have incremental changes applied to it later. See the |xtrabackup| :doc:`documentation <../xtrabackup_bin/incremental_backups>` for details.

.. option:: --remote-host=HOSTNAME

   This option accepts a string argument that specifies the remote host on which the backup files will be created, by using an ssh connection. This option is DEPRECATED and will be removed in Percona XtraBackup 2.1. In Percona XtraBackup 2.0 and later, you should use streaming backups instead.

.. option:: --rsync

   Uses the :program:`rsync` utility to optimize local file transfers. When this option is specified, :program:`innobackupex` uses :program:`rsync` to copy all non-InnoDB files instead of spawning a separate :program:`cp` for each file, which can be much faster for servers with a large number of databases or tables.  This option cannot be used together with :option:`--remote-host` or :option:`--stream`.

.. option:: --safe-slave-backup

   Stop slave SQL thread and wait to start backup until ``Slave_open_temp_tables`` in ``SHOW STATUS`` is zero. If there are no open temporary tables, the backup will take place, otherwise the SQL thread will be started and stopped until there are no open temporary tables. The backup will fail if ``Slave_open_temp_tables`` does not become zero after :option:`--safe-slave-backup-timeout` seconds. The slave SQL thread will be restarted when the backup finishes.

.. option:: --safe-slave-backup-timeout

   How many seconds :option:`--safe-slave-backup`` should wait for ``Slave_open_temp_tables`` to become zero. Defaults to 300 seconds.

.. option:: --scpopt = SCP-OPTIONS

   This option accepts a string argument that specifies the command line options to pass to :command:`scp` when the option :option:`--remost-host` is specified. If the option is not specified, the default options are ``-Cp -c arcfour``.

.. option:: --sshopt = SSH-OPTIONS

   This option accepts a string argument that specifies the command line options to pass to :command:`ssh` when the option :option:`--remost-host` is specified.

.. option:: --slave-info

   This option is useful when backing up a replication slave server. It prints the binary log position and name of the master server. It also writes this information to the :file:`xtrabackup_slave_info` file as a ``CHANGE MASTER`` command. A new slave for this master can be set up by starting a slave server on this backup and issuing a ``CHANGE MASTER`` command with the binary log position saved in the :file:`xtrabackup_slave_info` file.

.. option:: --socket

   This option accepts a string argument that specifies the socket to use when connecting to the local database server with a UNIX domain socket. It is passed to the mysql child process without alteration. See :command:`mysql --help` for details.

.. option:: --stream=STREAMNAME

   This option accepts a string argument that specifies the format in which to do the streamed backup. The backup will be done to ``STDOUT`` in the specified format. Currently, supported formats are `tar` and `xbstream`. Uses :doc:`xbstream <../xbstream/xbstream>`, which is available in *XtraBackup* distributions. If you specify a path after this option, it will be interpreted as the value of :option:`tmpdir`

.. option:: --tables-file=FILE

   This option accepts a string argument that specifies the file in which there are a list of names of the form ``database.table``, one per line. The option is passed directly to :program:`xtrabackup` 's :option:`--tables-file` option.

.. option:: --throttle=IOS

   This option accepts an integer argument that specifies the number of I/O operations (i.e., pairs of read+write) per second. It is passed directly to xtrabackup's :option:`xtrabackup --throttle` option.

.. option:: --tmpdir=DIRECTORY

   This option accepts a string argument that specifies the location where a temporary file will be stored. It should be used when :option:`--remote-host` or :option:`--stream` is specified. For these options, the transaction log will first be stored to a temporary file, before streaming or copying to a remote host. This option specifies the location where that temporary file will be stored. If the option is not specifed, the default is to use the value of ``tmpdir`` read from the server configuration.

.. option:: --use-memory

   This option accepts a string argument that specifies the amount of memory in bytes for :program:`xtrabackup` to use for crash recovery while preparing a backup. Multiples are supported providing the unit (e.g. 1MB, 1GB). It is used only with the option :option:`--apply-log`. It is passed directly to |xtrabackup| 's :option:`xtrabackup --use-memory` option. See the |xtrabackup| documentation for details.

.. option:: --user=USER

   This option accepts a string argument that specifies the user (i.e., the *MySQL* username used when connecting to the server) to login as, if that's not the current user. It is passed to the mysql child process without alteration. See :command:`mysql --help` for details.

.. option:: --version

   This option displays the |innobackupex| version and copyright notice and then exits.
