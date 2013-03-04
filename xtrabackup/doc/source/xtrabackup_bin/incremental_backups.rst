=====================
 Incremental Backups
=====================

Both |xtrabackup| and |innobackupex| tools supports incremental backups, which means that it can copy only the data that has changed since the last full backup. You can perform many incremental backups between each full backup, so you can set up a backup process such as a full backup once a week and an incremental backup every day, or full backups every day and incremental backups every hour.

Incremental backups work because each InnoDB page (usually 16kb in size) contains a log sequence number, or :term:`LSN`. The :term:`LSN` is the system version number for the entire database. Each page's :term:`LSN` shows how recently it was changed. An incremental backup copies each page whose :term:`LSN` is newer than the previous incremental or full backup's :term:`LSN`.

Incremental backups do not actually compare the data files to the previous backup's data files. In fact, you can use :option:`--incremental-lsn` to perform an incremental backup without even having the previous backup, if you know its :term:`LSN`. Incremental backups simply read the pages and compare their :term:`LSN` to the last backup's :term:`LSN`. You still need a full backup to recover the incremental changes, however; without a full backup to act as a base, the incremental backups are useless.

Creating an Incremental Backup
==============================

To make an incremental backup, begin with a full backup as usual. The |xtrabackup| binary writes a file called :file:`xtrabackup_checkpoints` into the backup's target directory. This file contains a line showing the ``to_lsn``, which is the database's :term:`LSN` at the end of the backup. :doc:`Create the full backup <creating_a_backup>` with a command such as the following: ::

  xtrabackup --backup --target-dir=/data/backups/base --datadir=/var/lib/mysql/

If you want a usable full backup, use :doc:`innobackupex <../innobackupex/innobackupex_script>` since `xtrabackup` itself won't copy table definitions, triggers, or anything else that's not .ibd.

If you look at the :file:`xtrabackup_checkpoints` file, you should see some contents similar to the following: ::

  backup_type = full-backuped
  from_lsn = 0
  to_lsn = 1291135

Now that you have a full backup, you can make an incremental backup based on it. Use a command such as the following: ::

  xtrabackup --backup --target-dir=/data/backups/inc1 \
  --incremental-basedir=/data/backups/base --datadir=/var/lib/mysql/

The :file:`/data/backups/inc1/` directory should now contain delta files, such as :file:`ibdata1.delta` and :file:`test/table1.ibd.delta`. These represent the changes since the ``LSN 1291135``. If you examine the :file:`xtrabackup_checkpoints` file in this directory, you should see something similar to the following:  ::

  backup_type = incremental
  from_lsn = 1291135
  to_lsn = 1291340

The meaning should be self-evident. It's now possible to use this directory as the base for yet another incremental backup: ::

  xtrabackup --backup --target-dir=/data/backups/inc2 \
  --incremental-basedir=/data/backups/inc1 --datadir=/var/lib/mysql/

Preparing the Incremental Backups
=================================

The :option:`--prepare` step for incremental backups is not the same as for normal backups. In normal backups, two types of operations are performed to make the database consistent: committed transactions are replayed from the log file against the data files, and uncommitted transactions are rolled back. For technical reasons, you must skip the rollback of uncommitted transactions when preparing a backup that will be used as the base for an incremental backup. You should use the :option:`--apply-log-only` option to prevent the rollback phase.

**If you do not use the** :option:`--apply-log-only` **option to prevent the rollback phase, then your incremental backups will be useless**. After transactions have been rolled back, further incremental backups cannot be applied.

Beginning with the full backup you created, you can prepare it, and then apply the incremental differences to it. Recall that you have the following backups: ::

  /data/backups/base
  /data/backups/inc1
  /data/backups/inc2

To prepare the base backup, you need to run :option:`--prepare` as usual, but prevent the rollback phase: ::

  xtrabackup --prepare --apply-log-only --target-dir=/data/backups/base

The output should end with some text such as the following: ::

  101107 20:49:43  InnoDB: Shutdown completed; log sequence number 1291135

The log sequence number should match the ``to_lsn`` of the base backup, which you saw previously.

This backup is actually safe to :doc:`restore <restoring_a_backup>` as-is now, even though the rollback phase has been skipped. If you restore it and start |MySQL|, |InnoDB| will detect that the rollback phase was not performed, and it will do that in the background, as it usually does for a crash recovery upon start. It will notify you that the database was not shut down normally.

To apply the first incremental backup to the full backup, you should use the following command: ::

  xtrabackup --prepare --apply-log-only --target-dir=/data/backups/base \
  --incremental-dir=/data/backups/inc1

This applies the delta files to the files in :file:`/data/backups/base`, which rolls them forward in time to the time of the incremental backup. It then applies the redo log as usual to the result. The final data is in :file:`/data/backups/base`, not in the incremental directory. You should see some output such as the following: ::

  incremental backup from 1291135 is enabled.
  xtrabackup: cd to /data/backups/base/
  xtrabackup: This target seems to be already prepared.
  xtrabackup: xtrabackup_logfile detected: size=2097152, start_lsn=(1291340)
  Applying /data/backups/inc1/ibdata1.delta ...
  Applying /data/backups/inc1/test/table1.ibd.delta ...
  .... snip
  101107 20:56:30  InnoDB: Shutdown completed; log sequence number 1291340

Again, the |LSN| should match what you saw from your earlier inspection of the first incremental backup. If you restore the files from :file:`/data/backups/base`, you should see the state of the database as of the first incremental backup.

Preparing the second incremental backup is a similar process: apply the deltas to the (modified) base backup, and you will roll its data forward in time to the point of the second incremental backup: ::

  xtrabackup --prepare --target-dir=/data/backups/base \
  --incremental-dir=/data/backups/inc2

.. note::
 
 :option:`--apply-log-only` should be used when merging all incrementals except the last one. That's why the previous line doesn't contain the :option:`--apply-log-only` option. Even if the :option:`--apply-log-only` was used on the last step, backup would still be consistent but in that case server would perform the rollback phase.

If you wish to avoid the notice that |InnoDB| was not shut down normally, when you have applied the desired deltas to the base backup, you can run :option:`--prepare` again without disabling the rollback phase.
