============================
 Making a Compressed Backup 
============================


In order to make a compressed backup you'll need to use :option:`--compress` option :: 

  $ innobackupex --compress /data/backup

If you want to speed up the compression you can use the parallel compression, which can be enabled with :option:`--compress-threads=#` option. Following example will use four threads for compression: :: 

  $ innobackupex --compress --compress-threads=4 /data/backup

Output should look like this :: 

  ...
  [01] Compressing ./imdb/comp_cast_type.ibd to /data/backup/2012-06-01_11-24-04/./imdb/comp_cast_type.ibd.qp
  [01]        ...done
  [01] Compressing ./imdb/aka_name.ibd to /data/backup/2012-06-01_11-24-04/./imdb/aka_name.ibd.qp
  [01]        ...done
  ...
  120601 11:50:24  innobackupex: completed OK

Preparing the backup
--------------------

Before you can prepare the backup you'll need to uncompress all the files with `qpress <http://www.quicklz.com/>`_. You can use following one-liner to uncompress all the files:  :: 

  $ for bf in `find . -iname "*\.qp"`; do qpress -d $bf $(dirname $bf) && rm $bf; done

When the files are uncompressed you can prepare the backup with the :option:`--apply-log` option: :: 

  $ innobackupex --apply-log /data/backup/2012-06-01_11-24-04/

You should check for a confirmation message: ::

  120604 02:51:02  innobackupex: completed OK!

Now the files in :file:`/data/backups/2012-06-01_11-24-04` is ready to be used by the server.

Restoring the backup
--------------------

Once the backup has been prepared you can use the :option:`--copy-back` to restore the backup. :: 

  $ innobackupex --copy-back /data/backups/2012-06-01_11-24-04/

This will copy the prepared data back to its original location as defined by the ``datadir`` in your :term:`my.cnf`.

After the confirmation message::

  120604 02:58:44  innobackupex: completed OK!

you should check the file permissions after copying the data back. You may need to adjust them with something like::

  $ chown -R mysql:mysql /var/lib/mysql

Now the :term:`datadir` contains the restored data. You are ready to start the server.
