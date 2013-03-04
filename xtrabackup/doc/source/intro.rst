==========================
 About Percona Xtrabackup
==========================


*Percona XtraBackup* is the world's only open-source, free |MySQL| hot backup software that performs non-blocking backups for |InnoDB| and |XtraDB| databases. With *Percona XtraBackup*, you can achieve the following benefits:

* Backups that complete quickly and reliably

* Uninterrupted transaction processing during backups

* Savings on disk space and network bandwidth

* Automatic backup verification

* Higher uptime due to faster restore time

|XtraBackup| makes |MySQL| hot backups for all versions of |Percona Server|, |MySQL|, |MariaDB|, and |Drizzle|. It performs streaming, compressed, and incremental |MySQL| backups.

|Percona| |XtraBackup| works with |MySQL|, |MariaDB|, |Percona Server|, and |Drizzle| databases (support for |Drizzle| is beta). It supports completely non-blocking backups of |InnoDB|, |XtraDB|, and *HailDB* storage engines. In addition, it can back up the following storage engines by briefly pausing writes at the end of the backup: |MyISAM|, :term:`Merge <.MRG>`, and :term:`Archive <.ARM>`, including partitioned tables, triggers, and database options.

|Percona|'s enterprise-grade commercial `MySQL Support <http://www.percona.com/mysql-support/>`_ contracts include support for XtraBackup. We recommend support for critical production deployments.

MySQL Backup Tool Feature Comparison
====================================

+---------------------------------------+----------------------+-----------------------+
|Features                               |Percona XtraBackup    |MySQL Enterprise Backup|
|                                       |                      |(InnoDB Hot Backup)    |
+=======================================+======================+=======================+
|License                                | GPL                  | Proprietary           |      
+---------------------------------------+----------------------+-----------------------+
|Price                                  | Free                 | $5000 per server      |      
+---------------------------------------+----------------------+-----------------------+
|Open source                            | Yes                  |                       |      
+---------------------------------------+----------------------+-----------------------+
|Non-blocking [#n-1]_                   | Yes                  | Yes                   |      
+---------------------------------------+----------------------+-----------------------+
|InnoDB backups                         | Yes                  | Yes                   |      
+---------------------------------------+----------------------+-----------------------+
|MyISAM backups                         | Yes                  | Yes                   |      
+---------------------------------------+----------------------+-----------------------+
|Compressed backups                     | Yes                  | Yes                   |      
+---------------------------------------+----------------------+-----------------------+
|Partial backups                        | Yes                  | Yes                   |      
+---------------------------------------+----------------------+-----------------------+
|Throttling [#n-2]_                     | Yes                  | Yes                   |      
+---------------------------------------+----------------------+-----------------------+
|Point-in-time recovery support         | Yes                  | Yes                   |      
+---------------------------------------+----------------------+-----------------------+
|Incremental backups                    | Yes                  | Yes                   |      
+---------------------------------------+----------------------+-----------------------+
|Parallel backups                       | Yes                  |                       |      
+---------------------------------------+----------------------+-----------------------+
|Streaming backups                      | Yes                  |                       |      
+---------------------------------------+----------------------+-----------------------+
|Parallel compression                   | Yes                  |                       |      
+---------------------------------------+----------------------+-----------------------+
|LRU backups                            | Yes                  |                       |      
+---------------------------------------+----------------------+-----------------------+
|OS buffer optimizations [#n-3]_        | Yes                  |                       |      
+---------------------------------------+----------------------+-----------------------+
|Export individual tables               | Yes                  |                       |      
+---------------------------------------+----------------------+-----------------------+
|Restore tables to a different server   | Yes                  |                       |      
+---------------------------------------+----------------------+-----------------------+
|Analyze data & index files             | Yes                  |                       |      
+---------------------------------------+----------------------+-----------------------+
|Familiar command-line behavior [#n-4]_ | Yes                  |                       |      
+---------------------------------------+----------------------+-----------------------+


What are the features of Percona XtraBackup?
============================================

Here is a short list of |XtraBackup| features. See the documentation for more.

* Create hot |InnoDB| backups without pausing your database
* Make incremental backups of |MySQL|
* Stream compressed |MySQL| backups to another server
* Move tables between |MySQL| servers on-line
* Create new |MySQL| replication slaves easily
* Backup |MySQL| without adding load to the server



.. rubric:: Footnotes

.. [#n-1] |MyISAM| backups require a table lock.

.. [#n-2] |XtraBackup| performs throttling based on the number of IO operations per second. *MySQL Enterprise Backup* supports a configurable sleep time between operations.

.. [#n-3] |XtraBackup| tunes the operating system buffers to avoid swapping. See the documentation.

.. [#n-4] |XtraBackup| is linked against the |MySQL| client libraries, so it behaves the same as standard |MySQL| command-line programs. *MySQL Enterprise Backup* has its own command-line and configuration-file behaviors.


