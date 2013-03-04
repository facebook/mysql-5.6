==============================
 Making an Incremental Backup
==============================

Every incremental backup starts with a full one, which we will call the *base backup*: ::

  innobackupex --user=USER --password=PASSWORD /path/to/backup/dir/

Note that the full backup will be in a timestamped subdirectory of ``/path/to/backup/dir/`` (e.g. ``/path/to/backup/dir/2011-12-24_23-01-00/``).

Assuming that variable ``$FULLBACKUP`` contains :file:`/path/to/backup/dir/2011-5-23_23-01-18`, let's do an incremental backup an hour later: ::

  innobackupex --incremental /path/to/inc/dir \
    --incremental-basedir=$FULLBACKUP --user=USER --password=PASSWORD

Now, the incremental backup should be in :file:`/path/to/inc/dir/2011-12-25_00-01-00/`. Let's call ``$INCREMENTALBACKUP=2011-5-23_23-50-10``.

Preparing incremental backups is a bit different than full ones:

First you have to replay the committed transactions on each backup, ::

  innobackupex --apply-log --redo-only $FULLBACKUP \
   --use-memory=1G --user=USER --password=PASSWORD

The :option:`–use-memory` option is not necessary, it will speed up the process if it is used (provided that the amount of RAM given is available).

If everything went fine, you should see an output similar to: ::

  111225 01:10:12 InnoDB: Shutdown completed; log sequence number 91514213

Now apply the incremental backup to the base backup, by issuing: ::

  innobackupex --apply-log --redo-only $FULLBACKUP
   --incremental-dir=$INCREMENTALBACKUP
   --use-memory=1G --user=DVADER --password=D4RKS1D3

Note the ``$INCREMENTALBACKUP``.

*The final data will be in the base backup directory*, not in the incremental one. In this example, ``/path/to/backup/dir/2011-12-24_23-01-00`` or ``$FULLBACKUP``.

If you want to apply more incremental backups, repeat this step with the next one. It is important that you do this in the chronological order in where the backups were done.

You can check the file xtrabackup_checkpoints at the directory of each one.

They should look like: (in the base backup) ::

  backup_type = full-backuped
  from_lsn = 0
  to_lsn = 1291135

and in the incremental ones: ::

  backup_type = incremental
  from_lsn = 1291135
  to_lsn = 1291340

The ``to_lsn`` number must match the ``from_lsn`` of the next one.

Once you put all the parts together, you can prepare again the full backup (base + incrementals) once again to rollback the pending transactions: ::

  innobackupex-1.5.1 --apply-log $FULLBACKUP --use-memory=1G \
    --user=$USERNAME --password=$PASSWORD

Now your backup is ready to be used immediately after restoring it. This preparation step is optional, as if you restore it without doing it, the database server will assume that a crash occurred and will begin to rollback the uncommitted transaction (causing some downtime which can be avoided).
