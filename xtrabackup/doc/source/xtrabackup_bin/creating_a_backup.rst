===================
 Creating a Backup
===================

To create a backup, run :program:`xtrabackup` with the :program:`--backup` option. You also need to specify a :option:`--target_dir` option, which is where the backup will be stored, and a :option:`--datadir` option, which is where the |MySQL| data is stored. If the |InnoDB| data or log files aren't stored in the same directory, you might need to specify the location of those, too. If the target directory does not exist, |xtrabackup| creates it. If the directory does exist and is empty, |xtrabackup| will succeed. |xtrabackup| will not overwrite existing files, it will fail with operating system error 17, ' ``file exists`` '.

The tool changes its working directory to the data directory and performs two primary tasks to complete the backup:

* It starts a log-copying thread in the background. This thread watches the InnoDB log files, and when they change, it copies the changed blocks to a file called :file:`xtrabackup_logfile` in the backup target directory. This is necessary because the backup might take a long time, and the recovery process needs all of the log file entries from the beginning to the end of the backup.

* It copies the |InnoDB| data files to the target directory. This is not a simple file copy; it opens and reads the files similarly to the way |InnoDB| does, by reading the data dictionary and copying them a page at a time.

When the data files are finished copying, |xtrabackup| stops the log-copying thread, and creates a files in the target directory called :file:`xtrabackup_checkpoints`, which contains the type of backup performed, the log sequence number at the beginning, and the log sequence number at the end.

An example command to perform a backup follows:

.. code-block:: guess

  xtrabackup --backup --datadir=/var/lib/mysql/ --target-dir=/data/backups/mysql/

This takes a backup of :file:`/var/lib/mysql` and stores it at :file:`/data/backups/mysql/`. If you specify a relative path, the target directory will be relative to the current directory.

During the backup process, you should see a lot of output showing the data files being copied, as well as the log file thread repeatedly scanning the log files and copying from it. Here is an example that shows the log thread scanning the log in the background, and a file copying thread working on the ``ibdata1`` file: ::

  >> log scanned up to (3646475465483)
  >> log scanned up to (3646475517369)
  >> log scanned up to (3646475581716) 
  >> log scanned up to (3646475636841)
  >> log scanned up to (3646475718082)
  >> log scanned up to (3646475988095)
  >> log scanned up to (3646476048286)
  >> log scanned up to (3646476102877)
  >> log scanned up to (3646476140854)
  [01] Copying /usr/local/mysql/var/ibdata1 
       to /usr/local/mysql/Backups/2011-04-18_21-11-15/ibdata1
  [01]        ...done

The last thing you should see is something like the following, where the value of the ``<LSN>`` will be a number that depends on your system: ::

  xtrabackup: Transaction log of lsn (<SLN>) to (<LSN>) was copied.

After the backup is finished, the target directory will contain files such as the following, assuming you have a single InnoDB table :file:`test.tbl1` and you are using MySQL's :term:`innodb_file_per_table` option: ::

  /data/backups/mysql/ibdata1
  /data/backups/mysql/test
  /data/backups/mysql/test/tbl1.ibd
  /data/backups/mysql/xtrabackup_checkpoints
  /data/backups/mysql/xtrabackup_logfile

The backup can take a long time, depending on how large the database is. It is safe to cancel at any time, because it does not modify the database.

The next step is getting your backup ready to restored: :doc:`preparing_the_backup`.
