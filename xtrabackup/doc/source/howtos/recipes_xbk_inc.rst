==============================
 Making an Incremental Backup
==============================

Backup all the InnoDB data and log files - located in ``/var/lib/mysql/`` - **once**, then make two daily incremental backups in ``/data/backups/mysql/`` (destination). Finally, prepare the backup files to be ready to restore or use.

Create one full backup
======================

Making an incremental backup requires a full backup as a base::

  xtrabackup --backup --target-dir=/data/backups/mysql/

It is important that you **do not run** the :option:`--prepare` command yet.

Create two incremental backups
==============================

Suppose the full backup is on Monday, and you will create an incremental one on Tuesday::

  xtrabackup --backup --target-dir=/data/backups/inc/tue/ \
        --incremental-basedir=/data/backups/mysql/

and the same policy is applied on Wednesday::

 xtrabackup --backup --target-dir=/data/backups/inc/wed/ \
        --incremental-basedir=/data/backups/inc/tue/

Prepare the base backup
=======================

Prepare the base backup (Monday's backup)::

  xtrabackup --prepare --apply-log-only --target-dir=/data/backups/mysql/

Roll forward the base data to the first increment
=================================================

Roll Monday's data forward to the state on Tuesday: ::

  xtrabackup --prepare --apply-log-only --target-dir=/data/backups/mysql/ \
     --incremental-dir=/data/backups/inc/tue/

Roll forward again to the second increment
==========================================

Roll forward again to the state on Wednesday: ::

  xtrabackup --prepare --apply-log-only --target-dir=/data/backups/mysql/ \
     --incremental-dir=/data/backups/inc/wed/

Prepare the whole backup to be ready to use
===========================================

Create the new logs by preparing it::

  xtrabackup --prepare --target-dir=/data/backups/mysql/

Notes
=====

*  You might want to set the :option:`--use-memory` to speed up the process if you are on a dedicated server that has enough free memory. More details :doc:`here <../xtrabackup_bin/xbk_option_reference>`.

* A more detailed explanation is :doc:`here <../xtrabackup_bin/incremental_backups>`.
