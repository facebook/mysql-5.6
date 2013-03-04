=============================================
 Preparing a Full Backup with |innobackupex|
=============================================

After creating a backup, the data is not ready to be restored. There might be uncommitted transactions to be undone or transactions in the logs to be replayed. Doing those pending operations will make the data files consistent and it is the purpose of the **prepare stage**. Once this has been done, the data is ready to be used. 

To prepare a backup with |innobackupex| you have to use the :option:`--apply-log` and the path to the backup directory as an argument::

  $ innobackupex --apply-log /path/to/BACKUP-DIR

and check the last line of the output for a confirmation on the process::

  111225  1:01:57  InnoDB: Shutdown completed; log sequence number 1609228
  111225 01:01:57  innobackupex: completed OK!

If it succeeded, |innobackupex| performed all operations needed, leaving the data ready to use immediately.

Under the hood
==============
reading the configuration from the files in the backup directory,

|innobackupex| replayed the committed transactions in the log files (some transactions could have been done while the backup was being done) and rolled back the uncommitted ones. Once this is done, all the information lay in the tablespace (the InnoDB files), and the log files are re-created.

This implied calling :program:`xtrabackup --prepare` twice with the right binary (determined through the :file:`xtrabackup_binary` or by connecting the server). More details of this process are shown in the :doc:`xtrabackup section <../xtrabackup_bin/preparing_the_backup>`.

Note that this preparation is not suited for incremental backups. If you perform it on the base of an incremental backup, you will not be able to "add" the increments. See :doc:`incremental_backups_innobackupex`.

Other options to consider
=========================

The :option:`--use-memory` option
---------------------------------

The preparing process can be speed up by using more memory in it. It depends on the free or available ``RAM`` on your system, it defaults to ``100MB``. In general, the more memory available to the process, the better. The amount of memory used in the process can be specified by multiples of bytes::

  $ innobackupex --apply-log --use-memory=4G /path/to/BACKUP-DIR
