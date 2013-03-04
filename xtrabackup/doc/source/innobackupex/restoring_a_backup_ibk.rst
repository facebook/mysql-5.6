=============================================
 Restoring a Full Backup with |innobackupex|
=============================================

For convenience, |innobackupex| has a :option:`--copy-back` option, which performs the restoration of a backup to the server's :term:`datadir` ::

  $ innobackupex --copy-back /path/to/BACKUP-DIR

It will copy all the data-related files back to the server's :term:`datadir`, determined by the server's :file:`my.cnf` configuration file. You should check the last line of the output for a success message::

  innobackupex: Finished copying back files.

  111225 01:08:13  innobackupex: completed OK!


As files’ attributes will be preserved, in most cases you will need to change the files’ ownership to ``mysql`` before starting the database server, as they will be owned by the user who created the backup::

  $ chown -R mysql:mysql /var/lib/mysql

Also note that all of these operations will be done as the user calling |innobackupex|, you will need write permissions on the server's :term:`datadir`.
