Enabling the server to communicate with through TCP/IP
======================================================

Most of the Linux distributions do not enable by default to accept TCP/IP connections from outside in their MySQL or Percona Server packages.

You can check it with ``netstat`` on a shell: ::

  $ netstat -lnp | grep mysql
  tcp         0        0 0.0.0.0:3306 0.0.0.0:* LISTEN 2480/mysqld 
  unix 2 [ ACC ] STREAM LISTENING 8101 2480/mysqld /tmp/mysql.sock

You should check two things:

*  there is a line starting with ``tcp`` (the server is indeed accepting TCP connections) and

*  the first address (``0.0.0.0:3306`` in this example) is different than ``127.0.0.1:3306`` (the bind address is not localhost's).

In the first case, the first place to look is the ``my.cnf`` file. If you find the option ``skip-networking``, comment it out or just delete it. Also check that *if* the variable ``bind_address`` is set, then it shouldn't be set to localhost's but to the host's IP. Then restart the MySQL server and check it again with ``netstat``. If the changes you did had no effect, then you should look at your distribution's startup scripts (like ``rc.mysqld``). You should comment out flags like ``--skip-networking`` and/or change the ``bind-address``.

After you get the server listening to remote TCP connections properly, the last thing to do is checking that the port (3306 by default) is indeed open. Check your firewall configurations (``iptables -L``) and that you are allowing remote hosts on that port (in ``/etc/hosts.allow``).

And we're done! We have a MySQL server running which is able to communicate with the world through TCP/IP.
