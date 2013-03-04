================================
 Exporting and Importing Tables
================================

In standard InnoDB, it is not normally possible to copy tables between servers by copying the files, even with :term:`innodb_file_per_table`. However, with the |xtrabackup| binary, you can export individual tables from any |InnoDB| database, and import them into |Percona Server| with |XtraDB|. (The source doesn't have to be |XtraDB|, but the destination does.) This functionality requires :term:`innodb_file_per_table` to be used on both servers, and requires :term:`innodb_expand_import` to be enabled on the destination server. It only works on individual :term:`.ibd` files, and cannot export a table that is not contained in its own :term:`.ibd` file.

Let's see how to export and import the following table: ::

  CREATE TABLE export_test (
    a int(11) DEFAULT NULL
  ) ENGINE=InnoDB DEFAULT CHARSET=latin1;

Exporting the Table
===================

This table should have been created in :term:`innodb_file_per_table` mode, so after taking a backup as usual with :option:`--backup`, the :term:`.ibd` file should exist in the target directory: ::

  $ find /data/backups/mysql/ -name export_test.*
  /data/backups/mysql/test/export_test.ibd

when you prepare the backup, add the extra parameter :option:`--export` to the command. If you do not have :term:`innodb_file_per_table` in your `my.cnf`, you must add that to the command-line options. Here is an example: ::

  $ xtrabackup --prepare --export --innodb-file-per-table --target-dir=/data/backups/mysql/

Now you should see a :term:`.exp` file in the target directory: ::

  $ find /data/backups/mysql/ -name export_test.*
  /data/backups/mysql/test/export_test.exp
  /data/backups/mysql/test/export_test.ibd

These two files are all you need to import the table into a server running |Percona Server| with |XtraDB|.

Importing the Table
===================

On the destination server running |Percona Server| with |XtraDB|, with :term:`innodb_expand_import` enabled, create a table with the same structure, and then perform the following steps:

* Execute ``ALTER TABLE test.export_test DISCARD TABLESPACE;``
  
  * If you see the following message, then you must enable :term:`innodb_file_per_table` and create the table again: ``ERROR 1030 (HY000): Got error -1 from storage engine``

* Copy the exported files to the ``test/`` subdirectory of the destination server's data directory

* Execute ``ALTER TABLE test.export_test IMPORT TABLESPACE;``

The table should now be imported, and you should be able to ``SELECT`` from it and see the imported data.


