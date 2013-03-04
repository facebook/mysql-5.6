=========================================
 Incremental Backups with |innobackupex|
=========================================

As not all information changes between each backup, the incremental backup strategy uses this to reduce the storage needs and the duration of making a backup.

This can be done because each |InnoDB| page has a log sequence number, |LSN|, which acts as a version number of the entire database. Every time the database is modified, this number gets incremented.

An incremental backup copies all pages since a specific |LSN|.

Once this pages have been put together in their respective order, applying the logs will recreate the process that affected the database, yielding the data at the moment of the most recently created backup.

Creating an Incremental Backups with |innobackupex|
===================================================

First, a full backup is needed, this will be the BASE for the incremental one: ::

  $ innobackupex /data/backups

This will create a timestamped directory in :file:`/data/backups`. Assuming that the backup is done last day of the year, ``BASEDIR`` would be :file:`/data/backups/2011-12-31_23-01-18`, for example.

.. note:: You can use the :option:`innobackupex --no-timestamp` option to override this behavior and the backup will be created in the given directory.

If you check at the :file:`xtrabackup-checkpoints` file in ``BASE-DIR``, you should see something like::

  backup_type = full-backuped
  from_lsn = 0
  to_lsn = 1291135

To create an incremental backup the next day, use the :option:`--incremental` option and provide the BASEDIR::

  $ innobackupex --incremental /data/backups --incremental-basedir=BASEDIR

and another timestamped directory will be created in :file:`/data/backups`, in this example, :file:`/data/backups/2012-01-01_23-01-18` containing the incremental backup. We will call this ``INCREMENTAL-DIR-1``.

If you check at the :file:`xtrabackup-checkpoints` file in ``INCREMENTAL-DIR-1``, you should see something like::

  backup_type = incremental
  from_lsn = 1291135
  to_lsn = 1352113

Creating another incremental backup the next day will be analogous, but this time the previous incremental one will be base: ::

  $ innobackupex --incremental /data/backups --incremental-basedir=INCREMENTAL-DIR-1

yielding (in this example) :file:`/data/backups/2012-01-02_23-02-08`. We will use ``INCREMENTAL-DIR-2`` instead for simplicity.

At this point, the :file:`xtrabackup-checkpoints` file in ``INCREMENTAL-DIR-2`` should contain something like::

  backup_type = incremental
  from_lsn = 1352113
  to_lsn = 1358967 

As it was said before, an incremental backup only copy pages with a |LSN| greater than a specific value. Providing the |LSN| would have produced directories with the same data inside: ::

  innobackupex --incremental /data/backups --incremental-lsn=1291135
  innobackupex --incremental /data/backups --incremental-lsn=1358967

This is a very useful way of doing an incremental backup, since not always the base or the last incremental will be available in the system.

.. warning:: This procedure only affects |XtraDB| or |InnoDB|-based tables. Other tables with a different storage engine, e.g. |MyISAM|, will be copied entirely each time an incremental backup is performed.

Preparing an Incremental Backup with |innobackupex|
===================================================

Preparing incremental backups is a bit different than full ones. This is, perhaps, the stage where more attention is needed:

 * First, **only the committed transactions must be replayed on each backup**. This will put the base full backup and the incremental ones altogether.

 * Then, the uncommitted transaction must be rolled back in order to have a ready-to-use backup.

If you replay the commited transactions **and** rollback the uncommitted ones on the base backup, you will not be able to add the incremental ones. If you do this on an incremental one, you won't be able to add data from that moment and the remaining increments.

Having this in mind, the procedure is very straight-forward using the :option:`--redo-only` option, starting with the base backup: ::

  innobackupex --apply-log --redo-only BASE-DIR

You should see an output similar to: ::

  120103 22:00:12 InnoDB: Shutdown completed; log sequence number 1291135
  120103 22:00:12 innobackupex: completed OK!

Then, the first incremental backup can be applied to the base backup, by issuing: ::

  innobackupex --apply-log --redo-only BASE-DIR --incremental-dir=INCREMENTAL-DIR-1

You should see an output similar to the previous one but with corresponding |LSN|: ::

  120103 22:08:43 InnoDB: Shutdown completed; log sequence number 1358967
  120103 22:08:43 innobackupex: completed OK!

If no :option:`--incremental-dir` is set, |innobackupex| will use the most recently subdirectory created in the basedir.

At this moment, ``BASE-DIR`` contains the data up to the moment of the first incremental backup. Note that the full data will be always in the directory of the base backup, as we are appending the increments to it.

Repeat the procedure with the second one: ::

  innobackupex --apply-log BASE-DIR --incremental-dir=INCREMENTAL-DIR-2

If the "completed OK!" message was shown, the final data will be in the base backup directory, ``BASE-DIR``.

.. note::
 
 :option:`--redo-only` should be used when merging all incrementals except the last one. That's why the previous line doesn't contain the :option:`--redo-only` option. Even if the :option:`--redo-only` was used on the last step, backup would still be consistent but in that case server would perform the rollback phase.

You can use this procedure to add more increments to the base, as long as you do it in the chronological order that the backups were done. If you omit this order, the backup will be useless. If you have doubts about the order that they must be applied, you can check the file :file:`xtrabackup_checkpoints` at the directory of each one, as shown in the beginning of this section.

Once you put all the parts together, you can prepare again the full backup (base + incrementals) once again to rollback the uncommitted transactions: ::

  innobackupex --apply-log BASE-DIR

Now your backup is ready to be used immediately after restoring it. This preparation step is "optional", as if you restore it without doing it, the database server will assume that a crash occurred and will begin to rollback the uncommitted transaction (causing some downtime which can be avoided).

Note that the :file:`iblog*` files will not be created by |innobackupex|, if you want them to be created, use :command:`xtrabackup --prepare` on the directory. Otherwise, the files will be created by the server once started.

Restoring Incremental Backups with |innobackupex|
=================================================

After preparing the incremental backups, the base directory contains the same as a full one. For restoring it you can use: ::

  innobackupex --copy-back BASE-DIR

and you may have to change the ownership as detailed on :doc:`restoring_a_backup_ibk`.

Incremental Streaming Backups using xbstream and tar
====================================================

Incremental streaming backups can be performed with the |xbstream| streaming option. Currently backups are packed in custom **xbstream** format. With this feature taking a BASE backup is needed as well. 

Taking a base backup: :: 
 
  innobackupex /data/backups

Taking a local backup: ::

  innobackupex --incremental --incremental-lsn=LSN-number --stream=xbstream ./ > incremental.xbstream

Unpacking the backup: ::

  xbstream -x < incremental.xbstream 

Taking a local backup and streaming it to the remote server and unpacking it: :: 

  innobackupex  --incremental --incremental-lsn=LSN-number --stream=xbstream ./ | /
  ssh user@hostname " cat - | xbstream -x -C > /backup-dir/"
 
