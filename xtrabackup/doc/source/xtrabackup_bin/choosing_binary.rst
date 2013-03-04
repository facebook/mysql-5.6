.. _ibk-right-binary:

===========================
 Choosing the Right Binary
===========================

The |xtrabackup| binary actually exists as three separate binaries: ``xtrabackup``, ``xtrabackup_51``, and ``xtrabackup_55``. This is to ensure binary compatibility of |InnoDB| data files across releases. Which binary you should use depends on the version of the server that produced the data files you want to back up. It's important to use the correct binary. Whichever binary is used to :doc:`create a backup <creating_a_backup>` should also be the binary used :doc:`to prepare that backup <preparing_the_backup>`.

Throughout the documentation, whenever the |xtrabackup| binary is mentioned, it's assumed that the name of the correct binary will be substituted. The following table summarizes which binary should be used with different server versions.

==============================  ===================
Server                          |xtrabackup| binary
==============================  ===================
MySQL 5.0.*                     ``xtrabackup_51``
MySQL 5.1.*                     ``xtrabackup_51``
MySQL 5.1.* with InnoDB plugin  ``xtrabackup``
MySQL 5.5.*                     ``xtrabackup_55``
MariaDB 5.1.* 					``xtrabackup``
MariaDB 5.2.* 					``xtrabackup``
MariaDB 5.3.* 					``xtrabackup``
MariaDB 5.5.* 					``xtrabackup_55``
Percona Server 5.0		        ``xtrabackup_51``
Percona Server 5.1		        ``xtrabackup``
Percona Server 5.5  		    ``xtrabackup_55``
==============================  ===================
