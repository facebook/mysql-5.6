Restoring a Backup
==================

The |xtrabackup| binary does not have any functionality for restoring a backup. That is up to the user to do. You might use :program:`rsync` or :program:`cp` to restore the files. You should check that the restored files have the correct ownership and permissions.

Note that |xtrabackup| backs up only the |InnoDB| data. You must separately restore the |MySQL| system database, |MyISAM| data, table definition files (:term:`.frm` files), and everything else necessary to make your database functional -- or |innobackupex| :doc:`can do it for you <../innobackupex/restoring_a_backup_ibk>`.
