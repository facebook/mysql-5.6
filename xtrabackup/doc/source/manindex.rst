.. Percona Xtrabackup documentation master file, created by
   sphinx-quickstart on Fri May  6 01:04:39 2011.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

====================================
 Percona Xtrabackup - Documentation
====================================

*Percona* |XtraBackup| is an open-source hot backup utility for |MySQL| - based servers that doesn't lock your database during the backup. 

It can back up data from |InnoDB|, |XtraDB|, and |MyISAM| tables on unmodified |MySQL| 5.0, 5.1 and 5.5 servers, as well as |Percona Server| with |XtraDB|. For a high-level overview of many of its advanced features, including a feature comparison, please see :doc:`intro`.

Whether it is a 24x7 highly loaded server or a low-transaction-volume environment, *Percona* |XtraBackup| is designed to make backups a seamless procedure without disrupting the performance of the server in a production environment. `Commercial support contracts are available <http://www.percona.com/mysql-support/>`_.

*Percona* |XtraBackup| is a combination of the |xtrabackup| *C* program, and the |innobackupex| *Perl* script. The |xtrabackup| program copies and manipulates |InnoDB| and |XtraDB| data files, and the *Perl* script enables enhanced functionality, such as interacting with a running |MySQL| server and backing up |MyISAM| tables. |XtraBackup| works with unmodified |MySQL| servers, as well as |Percona Server| with |XtraDB|. It runs on *Linux* and *FreeBSD*. The *Windows* version is currently at alpha stage and it is available for previewing and testing purposes. 

Introduction
============

.. toctree::
   :maxdepth: 1
   :glob:

   intro

User's Manual
=============

.. toctree::
   :maxdepth: 2
   :glob:

   manual

Tutorials, Recipes, How-tos
===========================

.. toctree::
   :maxdepth: 2
   :hidden:

   how-tos

* :ref:`recipes-xbk`

* :ref:`recipes-ibk`

* :ref:`howtos`

* :ref:`aux-guides`

Miscellaneous
=============

.. toctree::
   :maxdepth: 1
   :glob:

   glossary
   trademark-policy

Indices and tables
==================

* :ref:`genindex`

* :ref:`search`

