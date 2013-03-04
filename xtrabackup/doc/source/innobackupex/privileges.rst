==================================
 Connection and Privileges Needed
==================================

|XtraBackup| needs to be able to connect to the database server and perform operations on the server and the :term:`datadir` when creating a backup, when preparing in some scenarios and when restoring it. In order to do so, there are privileges and permission requirements on its execution that must be fulfilled.

Privileges refers to the operations that a system user is permitted to do in the database server. **They are set at the database server and only apply to users in the database server**.

Permissions are those which permits a user to perform operations on the system, like reading, writing or executing on a certain directory or start/stop a system service. **They are set at a system level and only apply to system users**.

Whether |xtrabackup| or |innobackupex| is used, there are two actors involved: the user invoking the program - *a system user* - and the user performing action in the database server - *a database user*. Note that these are different users on different places, despite they may have the same username.

All the invocations of |innobackupex| and |xtrabackup| in this documentation assumes that the system user has the appropriate permissions and you are providing the relevant options for connecting the database server - besides the options for the action to be performed - and the database user has adequate privileges. 

Connecting to the server
========================

The database user used to connect to the server and its password are specified by the :option:`--user` and :option:`--password` option, ::

  $ innobackupex --user=DBUSER --password=SECRET /path/to/backup/dir/
  $ innobackupex --user=LUKE  --password=US3TH3F0RC3 --stream=tar ./ | bzip2 - 
  $ xtrabackup --user=DVADER --password=14MY0URF4TH3R --backup --target-dir=/data/bkps/

If you don't use the :option:`--user` option, |XtraBackup| will assume the database user whose name is the system user executing it.

Other Connection Options
------------------------

According to your system, you may need to specify one or more of the following options to connect to the server:

===============  ===================================================================
Option           Description
===============  ===================================================================
--port           The port to use when connecting to the database server with TCP/IP.
--socket         The socket to use when connecting to the local database.
--host           The host to use when connecting to the database server with TCP/IP.
===============  ===================================================================

These options are passed to the :command:`mysql` child process without alteration, see :option:`mysql --help` for details.

.. note::
 In case of multiple server instances the correct connection parameters (port, socket, host) must be specified in order for |innobackupex| to talk to the correct server. 


Permissions and Privileges Needed
=================================

Once connected to the server, in order to perform a backup you will need ``READ``, ``WRITE`` and ``EXECUTE`` permissions at a filesystem level in the server's :term:`datadir`.

The database user needs the following privileges on the tables / databases to be backed up:

  * ``RELOAD`` and ``LOCK TABLES`` (unless the :option:`--no-lock <innobackupex --no-lock>` option is specified) in order to ``FLUSH TABLES WITH READ LOCK`` prior to start copying the files and 

  * ``REPLICATION CLIENT`` in order to obtain the binary log position,

  * ``CREATE TABLESPACE`` in order to import tables (see :ref:`imp_exp_ibk`) and

  * ``SUPER`` in order to start/stop the slave threads in a replication environment.

The explanation of when these are used can be found in :ref:`how_ibk_works`.

An SQL example of creating a database user with the minimum privileges required to full backups would be:

.. code-block:: sql

  mysql> CREATE USER 'bkpuser'@'localhost' IDENTIFIED BY 's3cret';
  mysql> GRANT RELOAD, LOCK TABLES, REPLICATION CLIENT ON *.* TO 'bkpuser'@'localhost';
  mysql> FLUSH PRIVILEGES;

.. note::

 Connection-related parameters are only recognized in the [client] and [mysql] groups in configuration files. Adding custom groups like [xtrabackup] will work only if XtraBackup binary is used, it will not work with the innobackupex.
