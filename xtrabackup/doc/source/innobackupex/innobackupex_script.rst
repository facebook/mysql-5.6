=========================
 The innobackupex Script
=========================

The |innobackupex| tool is a *Perl* script that acts as a wrapper for the :doc:`xtrabackup <../xtrabackup_bin/xtrabackup_binary>` *C* program. It is a patched version of the ``innobackup`` *Perl* script that *Oracle* distributes with the *InnoDB Hot Backup* tool. It enables more functionality by integrating |xtrabackup| and other functions such as file copying and streaming, and adds some convenience. It lets you perform point-in-time backups of |InnoDB| / |XtraDB| tables together with the schema definitions, |MyISAM| tables, and other portions of the server.

We are currently not satisfied with the architecture, code quality and maintainability, or functionality of |innobackupex|, and we expect to replace it with something else in the future.

This manual section explains how to use |innobackupex| in detail.

Prerequisites
=============

.. toctree::
   :maxdepth: 1

   privileges


The Backup Cycle - Full Backups
===============================

.. toctree::
   :maxdepth: 1

   creating_a_backup_ibk
   preparing_a_backup_ibk
   restoring_a_backup_ibk

Other Types of Backups
======================

.. toctree::
   :maxdepth: 1

   incremental_backups_innobackupex
   partial_backups_innobackupex

Proficiency
===========

.. toctree::
   :maxdepth: 1

   streaming_backups_innobackupex
   replication_ibk
   parallel_copy_ibk
   throttling_ibk
   remote_backups_ibk
   importing_exporting_tables_ibk
   pit_recovery_ibk


..    performance_tunning_innobackupex

Implementation
==============

.. toctree::
   :maxdepth: 1

   how_innobackupex_works


References
==========

.. toctree::
   :maxdepth: 1

   innobackupex_option_reference

