======================
 Preparing the backup
======================

After you make a backup with :option:`--backup`, the next step is to prepare it. The data files are not point-in-time consistent until they've been prepared, because they were copied at different times as the program ran, and they might have been changed while this was happening. If you try to start InnoDB with these data files, it will detect corruption and crash itself to prevent you from running on damaged data. The :option:`--prepare` step makes the files perfectly consistent at a single instant in time, so you can run |InnoDB| on them.

You can run the prepare operation on any machine; it does not need to be on the originating server or the server to which you intend to restore. You can copy the backup to a utility server and prepare it there, for example. It is important, however, that you use the same version of the xtrabackup binary that you used to create the backup to do the prepare.

During the prepare operation, |xtrabackup| boots up a kind of modified InnoDB that's embedded inside it (the libraries it was linked against). The modifications are necessary to disable InnoDB's standard safety checks, such as complaining that the log file isn't the right size, which aren't appropriate for working with backups. These modifications are only for the xtrabackup binary; you don't need a modified |InnoDB| to use |xtrabackup| for your backups.

The prepare step uses this "embedded InnoDB" to perform crash recovery on the copied datafiles, using the copied log file. The ``prepare`` step is very simple to use: you simply run |xtrabackup| with the :option:`--prepare` option and tell it which directory to prepare, for example, to prepare the backup previously taken, 

.. code-block:: console

  xtrabackup --prepare --target-dir=/data/backups/mysql/

When this finishes, you should see an "InnoDB shutdown" with a message such as the following, where again the value of :term:`LSN` will depend on your system: ::

  101107 16:40:15  InnoDB: Shutdown completed; log sequence number <LSN>

Your backup is now clean and consistent, and ready to restore. However, you might want to take an extra step to make restores as quick as possible. This is to prepare the backup a second time. The first time makes the data files perfectly self-consistent, but it doesn't create fresh |InnoDB| log files. If you restore the backup at this point and start |MySQL|, it will have to create new log files, which could take a little while, and you might not want to wait for that. If you run :option:`--prepare` a second time, |xtrabackup| will create the log files for you, and output status text such as the following, which is abbreviated for clarity. The value of ``<SIZE>`` will depend on your MySQL configuration.

.. code-block:: console

  $ xtrabackup --prepare --target-dir=/data/backups/mysql/
  xtrabackup: This target seems to be already prepared.
  xtrabackup: notice: xtrabackup_logfile was already used to '--prepare'.
  101107 16:54:10  InnoDB: Log file ./ib_logfile0 did not exist: new to be created
  InnoDB: Setting log file ./ib_logfile0 size to <SIZE> MB
  InnoDB: Database physically writes the file full: wait...
  101107 16:54:10  InnoDB: Log file ./ib_logfile1 did not exist: new to be created
  InnoDB: Setting log file ./ib_logfile1 size to <SIZE> MB
  InnoDB: Database physically writes the file full: wait...
  101107 16:54:15  InnoDB: Shutdown completed; log sequence number 1284108

All following prepares (third and following) will not change the already prepared data files, you can only see that output says

.. code-block:: console

  xtrabackup: This target seems to be already prepared.
  xtrabackup: notice: xtrabackup_logfile was already used to '--prepare'.

It is not recommended to interrupt xtrabackup process while preparing backup - it may cause data files corruption and backup will become not usable. Backup validity is not guaranteed if prepare process was interrupted.

If you intend the backup to be the basis for further incremental backups, you should use the :option:`--apply-log-only` option when preparing the backup, or you will not be able to apply incremental backups to it. See the documentation on preparing :doc:`incremental backups <incremental_backups>` for more details.
