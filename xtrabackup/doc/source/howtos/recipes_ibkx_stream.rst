=========================
 Make a Streaming Backup
=========================

Stream mode sends the backup to ``STDOUT`` in tar format instead of copying it to the directory named by the first argument. You can pipe the output to :command:`gzip`, or across the network to another server.

To extract the resulting tar file, you **must** use the ``-i`` option, such as ``tar -ixvf backup.tar``.

.. warning:: Remember to use the ``-i`` option for extracting a tarred backup. For more information, see :doc:`../innobackupex/streaming_backups_innobackupex`.

Here are some examples using ``tar`` option for streaming:

  * Stream the backup into a tar archived named 'backup.tar' :: 

      innobackupex --stream=tar ./ > backup.tar

  * The same, but compress it ::

      innobackupex --stream=tar ./ | gzip - > backup.tar.gz

  * Encrypt the backup ::

      innobackupex --stream=tar . | gzip - | openssl des3 -salt -k "password" > backup.tar.gz.des3

  * Send it to another server instead of storing it locally ::

      innobackupex --stream=tar ./ | ssh user@desthost "cat - > /data/backups/backup.tar"

  * The same thing with can be done with the ''netcat''.  ::

      ## On the destination host:
      nc -l 9999 | cat - > /data/backups/backup.tar
      ## On the source host:
      innobackupex --stream=tar ./ | nc desthost 9999

  * The same thing, but done as a one-liner: ::

      ssh user@desthost "( nc -l 9999 > /data/backups/backup.tar & )" \
      && innobackupex --stream=tar ./  |  nc desthost 9999

  * Throttling the throughput to 10MB/sec. This requires the 'pv' tools; you can find them at the `official site <http://www.ivarch.com/programs/quickref/pv.shtml>`_ or install it from the distribution package ("apt-get install pv") :: 

      innobackupex --stream=tar ./ | pv -q -L10m \
      | ssh user@desthost "cat - > /data/backups/backup.tar"

Examples using |xbstream| option for streaming:

  * Stream the backup into a tar archived named 'backup.xbstream :: 

      innobackupex --stream=xbstream ./ > backup.xbstream
  
  * The same but with compression :: 
  
      innobackupex --stream=xbstream --compress ./ > backup.xbstream
  
  * To unpack the backup to the current directory: :: 

      xbstream -x <  backup.xbstream 

  * Sending backup compressed directly to another host and unpacking it: ::

      innobackupex --compress --stream=xbstream ./ | ssh user@otherhost "xbstream -x"
  
  * Parallel compression with parallel copying backup :: 
 
      innobackupex --compress --compress-threads=8 --stream=xbstream --parallel=4 ./ > backup.xbstream
