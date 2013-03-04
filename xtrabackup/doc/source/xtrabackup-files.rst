.. _xtrabackup_files :

=====================================
Index of files created by XtraBackup
=====================================

* Information related to the backup and the server

    * :file:`backup-my.cnf`
       This file contains information to start the mini instance of InnoDB during the :option:`--apply-log`. This is **NOT** a backup of original :file:`my.cnf`.

    * :file:`xtrabackup_checkpoints`
       The type of the backup (e.g. full or incremental), its state (e.g. prepared) and the |LSN| range contained in it. This information is used for incremental backups.
       Example of the :file:`xtrabackup_checkpoints` after taking a full backup: :: 
        
        backup_type = full-backuped
        from_lsn = 0
        to_lsn = 15188961605
        last_lsn = 15188961605

       Example of the :file:`xtrabackup_checkpoints` after taking an incremental backup: :: 
      
        backup_type = incremental
        from_lsn = 15188961605
        to_lsn = 15189350111
        last_lsn = 15189350111

    * :file:`xtrabackup_binlog_info`
       The binary log file used by the server and its position at the moment of the backup. Result of the :command:`SHOW MASTER STATUS`.

    * :file:`xtrabackup_binlog_pos_innodb`
       The binary log file and its current position for |InnoDB| or |XtraDB| tables.

    * :file:`xtrabackup_binary`
       The |xtrabackup| binary used in the process.
    
    * :file:`xtrabackup_logfile` 
       Contains data needed for running the: :option:`--apply-log`. The bigger this file is the :option:`--apply-log` process will take longer to finish. 

    * :file:`<table_name>.delta.meta`
       This file is going to be created when performing the incremental backup. It contains the per-table delta metadata: page size, size of compressed page (if the value is 0 it means the tablespace isn't compressed) and space id. Example of this file could looks like this: :: 

        page_size = 16384
        zip_size = 0
        space_id = 0

* Information related to the replication environment (if using the :option:`--slave-info` option):

    * :file:`xtrabackup_slave_info`
       The ``CHANGE MASTER`` statement needed for setting up a slave.

