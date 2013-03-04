==========
 Glossary
==========

.. glossary::

   LSN
     Each InnoDB page (usually 16kb in size) contains a log sequence number, or LSN. The LSN is the system version number for the entire database. Each page's LSN shows how recently it was changed.

   innodb_file_per_table
     By default, all InnoDB tables and indexes are stored in the system tablespace on one file. This option causes the server to create one tablespace file per table. To enable it, set it on your configuration file, 

      .. code-block:: guess

         [mysqld]
         innodb_file_per_table

      or start the server with ``--innodb_file_per_table``.

   innodb_expand_import
     This feature of |Percona Server| implements the ability to import arbitrary :term:`.ibd` files exported using the |XtraBackup| :option:`--export` option.
     
     See the `the full documentation <http://www.percona.com/doc/percona-server/5.5/management/innodb_expand_import.html>`_ for more information.

   innodb_data_home_dir
     The directory (relative to :term:` datadir`) where the database server stores the files in a shared tablespace setup. This option does not affect the location of :term:`innodb_file_per_table`. For example, 

      .. code-block:: guess

         [mysqld]
         innodb_data_home_dir = ./

   innodb_data_file_path
     Specifies the names, sizes and location of shared tablespace files:

      .. code-block:: guess

         [mysqld]
         innodb_data_file_path=ibdata1:50M;ibdata2:50M:autoextend
  
   innodb_log_group_home_dir
     Specifies the location of the |InnoDB| log files:

      .. code-block:: guess

         [mysqld]
         innodb_log_group_home=/var/lib/mysql

   innodb_buffer_pool_size
     The size in bytes of the memory buffer to cache data and indexes of |InnoDB|'s tables. This aims to reduce disk access to provide better performance. By default:

      .. code-block:: guess

         [mysqld]
         innodb_buffer_pool_size=8MB

   InnoDB
      Storage engine which provides ACID-compliant transactions and foreign key support, among others improvements over :term:`MyISAM`. It is the default engine for |MySQL| as of the 5.5 series.

   MyISAM
     Previous default storage engine for |MySQL| for versions prior to 5.5. It doesn't fully support transactions but in some scenarios may be faster than :term:`InnoDB`. Each table is stored on disk in 3 files: :term:`.frm`, :term:`.MYD`, :term:`.MYI`

   XtraDB
     *Percona XtraDB* is an enhanced version of the InnoDB storage engine, designed to better scale on modern hardware, and including a variety of other features useful in high performance environments. It is fully backwards compatible, and so can be used as a drop-in replacement for standard InnoDB. More information `here <http://www.percona.com/docs/wiki/Percona-XtraDB:start>`_ .

   my.cnf
     This file refers to the database server's main configuration file. Most linux distributions place it as :file:`/etc/mysql/my.cnf`, but the location and name depends on the particular installation. Note that this is not the only way of configuring the server, some systems does not have one even and rely on the command options to start the server and its defaults values.

   datadir
    The directory in which the database server stores its databases. Most Linux distribution use :file:`/var/lib/mysql` by default.

   xbstream
     To support simultaneous compression and streaming, a new custom streaming format called xbstream was introduced to XtraBackup in addition to the TAR format. 

   ibdata
     Default prefix for tablespace files, e.g. :file:`ibdata1` is a 10MB  autoextensible file that |MySQL| creates for the shared tablespace by default. 

   .frm
     For each table, the server will create a file with the ``.frm`` extension containing the table definition (for all storage engines).

   .ibd
     On a multiple tablespace setup (:term:`innodb_file_per_table` enabled), |MySQL| will store each newly created table on a file with a ``.ibd`` extension.

   .MYD
     Each |MyISAM| table has ``.MYD`` (MYData) file which contains the data on it.

   .MYI
     Each |MyISAM| table has ``.MYI`` (MYIndex) file which contains the table's indexes.

   .exp
     When :doc:`exporting a table <xtrabackup_bin/exporting_importing_tables>` with |XtraBackup|, it creates a file with ``.exp`` extension per exported table containing the information for importing it.

   .MRG
     Each table using the :program:`MERGE` storage engine, besides of a :term:`.frm` file, will have :term:`.MRG` file containing the names of the |MyISAM| tables associated with it.

   .TRG
     File containing the Triggers associated to a table, e.g. `:file:`mytable.TRG`. With the :term:`.TRN` file, they represent all the Trigger definitions.

   .TRN
     File containing the Triggers' Names associated to a table, e.g. `:file:`mytable.TRN`. With the :term:`.TRG` file, they represent all the Trigger definitions.

   .ARM
     Each table with the :program:`Archive Storage Engine` has ``.ARM`` file which contains the metadata of it.

   .ARZ
     Each table with the :program:`Archive Storage Engine` has ``.ARZ`` file which contains the data of it.

   .CSM
     Each table with the :program:`CSV Storage Engine` has ``.CSM`` file which contains the metadata of it.

   .CSV
     Each table with the :program:`CSV Storage` engine has ``.CSV`` file which contains the data of it (which is a standard Comma Separated Value file).

   .opt
     |MySQL| stores options of a database (like charset) in a file with a :option:`.opt` extension in the database directory.

