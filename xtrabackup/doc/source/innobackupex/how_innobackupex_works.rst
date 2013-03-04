.. _how_ibk_works:

==========================
 How |innobackupex| Works
==========================

:program:`innobackupex` is a script written in Perl that wraps the :program:`xtrabackup` and :program:`tar4ibd` binaries and performs the tasks where the performance and efficiency of *C* program isn't needed. In this way, it provides a convinient and integrated approach to backing up in many common scenarios. 

The following describes the rationale behind :program:`innobackupex` actions.

.. _making-backup-ibk:

Making a Backup
===============

If no mode is specified, |innobackupex| will assume the backup mode.

By default, it starts :program:`xtrabackup` with the :option:`--suspend-at-end` option, and lets it copy the InnoDB data files. When :program:`xtrabackup` finishes that, :program:`innobackupex` sees it create the :file:`xtrabackup_suspended` file and executes ``FLUSH TABLES WITH READ LOCK``. Then it begins copying the rest of the files.

If the :option:`--ibbackup` is not supplied, |innobackupex| will try to detect it: if the :file:`xtrabackup_binary` file exists on the backup directory, it reads from it which binary of |xtrabackup| will be used. Otherwise, it will try to connect to the database server in order to determine it. If the connection can't be established, |xtrabackup| will fail and you must specify it (see :ref:`ibk-right-binary`).

When the binary is determined, the connection to the database server is checked. This done by connecting, issuing a query, and closing the connection. If everything goes well, the binary is started as a child process.

If it is not an incremental backup, it connects to the server. It waits for slaves in a replication setup if the option :option:`--safe-slave-backup` is set and will flush all tables with **READ LOCK**, preventing all |MyISAM| tables from writing (unless option :option:`--no-lock` is specified).

Once this is done, the backup of the files will begin. It will backup :term:`.frm`, :term:`.MRG`, :term:`.MYD`, :term:`.MYI`, :term:`.TRG`, :term:`.TRN`, :term:`.ARM`, :term:`.ARZ`, :term:`.CSM`, :term:`.CSV` and :term:`.opt` files.

When all the files are backed up, it resumes :program:`ibbackup` and wait until it finishes copying the transactions done while the backup was done. Then, the tables are unlocked, the slave is started (if the option :option:`--safe-slave-backup` was used) and the connection with the server is closed. Then, it removes the :file:`xtrabackup_suspended` file and permits :program:`xtrabackup` to exit.

It  will also create the following files in the directory of the backup:

:file:`xtrabackup_checkpoints`
   containing the :term:`LSN` and the type of backup;

:file:`xtrabackup_binlog_info` 
   containing the position of the binary log at the moment of backing up;

:file:`xtrabackup_binlog_pos_innodb`
   containing the position of the binary log at the moment of backing up relative to |InnoDB| transactions;

:file:`xtrabackup_slave_info`
   containing the MySQL binlog position of the master server in a replication setup via ``'SHOW SLAVE STATUS\G;'`` if the :option:`--slave-info` option is passed;

:file:`backup-my.cnf`
   containing only the :file:`my.cnf` options required for the backup;

:file:`xtrabackup_binary` 
   containing the binary used for the backup;

:file:`mysql-stderr`
  containing the ``STDERR`` of :program:`mysqld` during the process and

:file:`mysql-stdout`
  containing the ``STDOUT`` of the server.

If the :option:`--remote-host` was set, |innobackupex| will test the connection to the host via :command:`ssh` and create the backup directories. Then the same process will be applied but the log will be written to a temporary file and will be copied via :command:`scp` with the options set by :option:`--scpopt` (``-Cp -c arcfour`` by default).

After each copy the files will be deleted. The same rationale is for the :option:`--stream` mode.

Finally, the binary log position will be printed to ``STDERR`` and |innobackupex| will exit returning 0 if all went OK.

Note that the ``STDERR`` of |innobackupex| is not written in any file. You will have to redirect it to a file, e.g., ``innobackupex OPTIONS 2> backupout.log``.

.. _copy-back-ibk:

Restoring a backup
==================

To restore a backup with |innobackupex| the :option:`--copy-back` option must be used.

|innobackupex| will read the read from the :file:`my.cnf` the variables :term:`datadir`, :term:`innodb_data_home_dir`, :term:`innodb_data_file_path`, :term:`innodb_log_group_home_dir` and check that the directories exist.

It will copy the |MyISAM| tables, indexes, etc. (:term:`.frm`, :term:`.MRG`, :term:`.MYD`, :term:`.MYI`, :term:`.TRG`, :term:`.TRN`, :term:`.ARM`, :term:`.ARZ`, :term:`.CSM`, :term:`.CSV` and :term:`.opt` files) first, |InnoDB| tables and indexes next and the log files at last. It will preserve file's attributes when copying them, you may have to change the files' ownership to ``mysql`` before starting the database server, as they will be owned by the user who created the backup.

Alternatively, the :option:`--move-back` option may be used to restore a
backup. This option is similar to :option:`--copy-back` with the only
difference that instead of copying files it moves them to their target
locations. As this option removes backup files, it must be used with
caution. It is useful in cases when there is not enough free disk space
to hold both data files and their backup copies.
