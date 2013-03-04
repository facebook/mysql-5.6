================
LRU dump backup
================

This feature reduces the warm up time by restoring buffer pool state from :file:`ib_lru_dump` file after restart. |Xtrabackup| discovers :file:`ib_lru_dump` and backs it up automatically.

.. image:: /_static/lru_dump.png

If the buffer restore option is enabled in :file:`my.cnf` buffer pool will be in the warm state after backup is restored. To enable this set the variable `innodb_buffer_pool_restore_at_startup <http://www.percona.com/doc/percona-server/5.5/management/innodb_lru_dump_restore.html?id=percona-server:features:innodb_buffer_pool_restore_at_startup#innodb_buffer_pool_restore_at_startup>`_ =1 in Percona Server 5.5 or `innodb_auto_lru_dump <http://www.percona.com/doc/percona-server/5.1/management/innodb_lru_dump_restore.html#innodb_auto_lru_dump>`_ =1 in Percona Server 5.1.
