=================
 Partial Backups
=================

|XtraBackup| features partial backups, which means that you may backup only some specific tables or databases. The only requirement for this feature is having the :term:`innodb_file_per_table` option enabled in the server. 

There is only one caveat about partial backups: do not copy back the prepared backup. Restoring partial backups should be done by importing the tables, not by using the traditional :option:`--copy-back` option. Although there are some scenarios where restoring can be done by copying back the files, this may be lead to database inconsistencies in many cases and it is not the recommended way to do it.

Creating Partial Backups
========================

There are three ways of specifying which part of the whole data will be backed up: regular expressions (:option:`--include`), enumerating the tables in a file (:option:`--tables-file`) or providing a list of databases (:option:`--databases`). 

Using the :option:`--include` option
------------------------------------

The regular expression provided to this will be matched against the fully qualified tablename, including the database name, in the form ``databasename.tablename``.

For example, ::

  $ innobackupex --include='^mydatabase[.]mytable' /path/to/backup

will create a timestamped directory with the usual files that |innobackupex| creates,  but only the data files related to the tables matched.

Note that this option is passed to :option:`xtrabackup --tables` and is matched against each table of each database, the directories of each database will be created even if they are empty.

Using the :option:`--tables-file` option
----------------------------------------

The text file provided (the path) to this option can contain multiple table names, one per line,  in the ``databasename.tablename`` format.

For example, ::

  $ echo "mydatabase.mytable" > /tmp/tables.txt
  $ innobackupex --tables-file=/tmp/tables.txt /path/to/backup

will create a timestamped directory with the usual files that |innobackupex| creates, but only containing the data-files related to the tables specified in the file.

This option is passed to :option:`xtrabackup --tables-file` and, unlike the :option:`--tables` option, only directories of databases of the selected tables will be created.


Using the :option:`--databases` option
--------------------------------------

This option is specific to |innobackupex| and accepts whether a space-separated list of the databases and tables to backup - in the  ``databasename[.tablename]`` form - or a file containing the list at one element per line.

For example, ::

  $ innobackupex --databases="mydatabase.mytable mysql" /path/to/backup

will create a timestamped directory with the usual files that |innobackupex| creates, but only containing the data-files related to ``mytable`` in the ``mydatabase`` directory and the ``mysql`` directory with the entire ``mysql`` database.

.. note:: 
 
 Currently in XtraBackup the --databases option has no effect for InnoDB files for both local and streaming backups, i.e. all InnoDB files are always backed up. Currently, only .frm and non-InnoDB tables are limited by that option.

Preparing Partial Backups
=========================

For preparing partial backups, the procedure is analogous to :doc:`exporting tables <importing_exporting_tables_ibk>` : apply the logs and use the :option:`--export` option::

  $ innobackupex --apply-log --export /path/to/partial/backup

You may see warnings in the output about tables that don't exists. This is because |InnoDB| -based engines stores its data dictionary inside the tablespace files besides the :term:`.frm` files. |innobackupex| will use |xtrabackup| to remove the missing tables (those who weren't selected in the partial backup) from the data dictionary in order to avoid future warnings or errors::

  111225  0:54:06  InnoDB: Error: table 'mydatabase/mytablenotincludedinpartialb'
  InnoDB: in InnoDB data dictionary has tablespace id 6,
  InnoDB: but tablespace with that id or name does not exist. It will be removed from data dictionary.

You should also see the notification of the creation of a file needed for importing (:term:`.exp` file) for each table included in the partial backup::

  xtrabackup: export option is specified.
  xtrabackup: export metadata of table 'employees/departments' to file `.//departments.exp` (2 indexes)
  xtrabackup:     name=PRIMARY, id.low=80, page=3
  xtrabackup:     name=dept_name, id.low=81, page=4

Note that if you can use the :option:`--export` option with :option:`--apply-log` to an already-prepared backup in order to create the :term:`.exp` files.

Finally, check the for the confirmation message in the output::

  111225 00:54:18  innobackupex: completed OK!


Restoring Partial Backups
=========================

Restoring should be done by :doc:`importing the tables <importing_exporting_tables_ibk>` in the partial backup to the server. 

Although it can be done by copying back the prepared backup to a "clean" :term:`datadir` (in that case, make sure of having included the ``mysql`` database)...



