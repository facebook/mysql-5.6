.. _user-manual:

==================================
 *Percona XtraBackup* User Manual
==================================

.. toctree::
   :maxdepth: 1
   :hidden:

   innobackupex/innobackupex_script
   xtrabackup_bin/xtrabackup_binary
   xbstream/xbstream
   how_xtrabackup_works

|XtraBackup| is really a set of three tools:

:doc:`innobackupex <innobackupex/innobackupex_script>`
    a wrapper script that provides functionality to backup a whole |MySQL| database instance with :term:`MyISAM`, :term:`InnoDB`, and :term:`XtraDB` tables.

:doc:`xtrabackup <xtrabackup_bin/xtrabackup_binary>`
    a compiled *C* binary, which copies only :term:`InnoDB` and :term:`XtraDB` data
    
:doc:`xbstream <xbstream/xbstream>`
   new utility that allows streaming and extracting files to/from the :term:`xbstream` format.

It is possible to use the |xtrabackup| binary alone, however, the recommend way is using it through the |innobackupex| wrapper script and let it execute |xtrabackup| for you. It might be helpful to first learn :doc:`how to use innobackupex <innobackupex/innobackupex_script>`, and then learn  :doc:`how to use xtrabackup <xtrabackup_bin/xtrabackup_binary>` for having a better low-level understanding or control of the tool if needed.
