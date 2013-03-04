========================================================
 Make a Local Full Backup (Create, Prepare and Restore)
========================================================

Create the Backup
=================

This is the simplest use case. It copies all your |MySQL| data into the specified directory. Here is how to make a backup of all the databases in the :term:`datadir` specified in your :term:`my.cnf`. It will put the backup in a time stamped subdirectory of :file:`/data/backups/`, in this case, :file:`/data/backups/2010-03-13_02-42-44`, ::

  $ innobackupex /data/backups

There is a lot of output, but you need to make sure you see this at the end of the backup. If you don't see this output, then your backup failed: ::

  100313 02:43:07  innobackupex: completed OK!

Prepare the Backup
==================

To prepare the backup use the :option:`--apply-log` option and specify the timestamped subdirectory of the backup. To speed up the apply-log process, we using the :option:`--use-memory` option is recommended: ::

  $ innobackupex --use-memory=4G --apply-log /data/backups/2010-03-13_02-42-44/

You should check for a confirmation message: ::

  100313 02:51:02  innobackupex: completed OK!

Now the files in :file:`/data/backups/2010-03-13_02-42-44` is ready to be used by the server.

Restore the Backup
==================

To restore the already-prepared backup, first stop the server and then use the :option:`--copy-back` function of |innobackupex|:: 

  innobackupex --copy-back /data/backups/2010-03-13_02-42-44/
  ## Use chmod to correct the permissions, if necessary!

This will copy the prepared data back to its original location as defined by the ``datadir`` in your :term:`my.cnf`.

After the confirmation message::

  100313 02:58:44  innobackupex: completed OK!

you should check the file permissions after copying the data back. You may need to adjust them with something like::

  $ chown -R mysql:mysql /var/lib/mysql

Now the :term:`datadir` contains the restored data. You are ready to start the server.


