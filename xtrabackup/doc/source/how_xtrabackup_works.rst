========================
 How |XtraBackup| Works
========================

|XtraBackup| is based on :term:`InnoDB`'s crash-recovery functionality. It copies your |InnoDB| data files, which results in data that is internally inconsistent; but then it performs crash recovery on the files to make them a consistent, usable database again.

This works because |InnoDB| maintains a redo log, also called the transaction log. This contains a record of every change to InnoDB's data. When |InnoDB| starts, it inspects the data files and the transaction log, and performs two steps. It applies committed transaction log entries to the data files, and it performs an undo operation on any transactions that modified data but did not commit.

|XtraBackup| works by remembering the log sequence number (:term:`LSN`) when it starts, and then copying away the data files. It takes some time to do this, so if the files are changing, then they reflect the state of the database at different points in time. At the same time, |XtraBackup| runs a background process that watches the transaction log files, and copies changes from it. |XtraBackup| needs to do this continually because the transaction logs are written in a round-robin fashion, and can be reused after a while. |XtraBackup| needs the transaction log records for every change to the data files since it began execution.

The above is the backup process. Next is the prepare process. During this step, |XtraBackup| performs crash recovery against the copied data files, using the copied transaction log file. After this is done, the database is ready to restore and use.

The above process is implemented in the |xtrabackup| compiled binary program. The |innobackupex| program adds more convenience and functionality by also permitting you to back up |MyISAM| tables and :term:`.frm` files. It starts |xtrabackup|, waits until it finishes copying files, and then issues ``FLUSH TABLES WITH READ LOCK`` to prevent further changes to |MySQL|'s data and flush all |MyISAM| tables to disk. It holds this lock, copies the |MyISAM| files, and then releases the lock.

The backed-up |MyISAM| and |InnoDB| tables will eventually be consistent with each other, because after the prepare (recovery) process, |InnoDB|'s data is rolled forward to the point at which the backup completed, not rolled back to the point at which it started. This point in time matches where the ``FLUSH TABLES WITH READ LOCK`` was taken, so the |MyISAM| data and the prepared |InnoDB| data are in sync.

The |xtrabackup| and |innobackupex| tools both offer many features not mentioned in the preceding explanation. Each tool's functionality is explained in more detail on its manual page. In brief, though, the tools permit you to do operations such as streaming and incremental backups with various combinations of copying the data files, copying the log files, and applying the logs to the data.
