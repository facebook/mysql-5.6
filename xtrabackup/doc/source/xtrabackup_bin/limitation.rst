Limitations of xtrabackup
=========================

The ``xtrabackup`` binary has some limitations you should be aware of to ensure that your backups go smoothly and are recoverable.

* If the ``xtrabackup_logfile`` is larger than 4GB, the ``--prepare`` step will fail on 32-bit versions of ``xtrabackup``.

* ``xtrabackup`` does not currently create new InnoDB log files (``ib_logfile0``, etc) during the initial ``--prepare`` step. You must prepare the backup a second time to do this, if you wish.

* ``xtrabackup`` copies only the InnoDB data and logs. It does not copy table definition files (``.frm`` files), MyISAM data, users, privileges, or any other portions of the overall database that lie outside of the InnoDB data. To back up this data, you need a wrapper script such as :doc:`innobackupex <../innobackupex/innobackupex_script>`.

* ``xtrabackup`` doesn't understand the very old ``--set-variable`` ``my.cnf`` syntax that MySQL uses. See :doc:`configuring`.

