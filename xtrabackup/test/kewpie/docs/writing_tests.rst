**********************************
Writing native/unittest test cases
**********************************

Synopsis
========

Here, we discuss various topics related to writing test cases, with a focus on features
that allow for more complex testing scenarios.  Additional documentation for other testing
tools will come later.

Writing native mode tests
==========================
Native mode is a way of saying Python unittest mode.  The goal is that all tests will use this paradigm as it is clean, flexible, and easily expanded.  Some references:

    * http://docs.python.org/library/unittest.html
    * http://docs.python.org/library/unittest.html#assert-methods

Writing native modes tests can be quite simple.  Here is an example::

    from lib.util.mysqlBaseTestCase import mysqlBaseTestCase

    server_requirements = [[]]
    servers = []
    server_manager = None
    test_executor = None

    class basicTest(mysqlBaseTestCase):

        def test_OptimizerSubquery1(self):
            self.servers = servers
            test_cmd = "./gentest.pl --gendata=conf/percona/outer_join_percona.zz --grammar=conf/percona/outer_join_percona.yy --queries=500 --threads=5"
            retcode, output = self.execute_randgen(test_cmd, test_executor, servers)
            self.assertEqual(retcode, 0, output)

Every test case should have a basicTest class.  This is not ideal, but it is the current implementation and testing does not suffer for the lack of creative naming ; ).  The test runner pulls from this class and all test_* methods for the class are executed.

Requesting / specifying server requirements
--------------------------------------------

The 'server_requirements' block in the example above is how a test case requests servers for use in a test case.
The content is a list of Python lists, with each sublist containing a string of server options.
A more complex example::

    server_requirements = [ [ ("--binlog-do-db=test "
                               "--innodb-file-per-table "
                               "--innodb_file_format='Barracuda' "
                               "--sync_binlog=100 "
                               "--innodb_flush_log_at_trx_commit=2 "
                               )]
                           ,[ ("--innodb_file_format='Barracuda' "
                               "--innodb_flush_log_at_trx_commit=2"
                              )]
                          ]

The server_manager is passed these requirements and attempts to start up a server with them.  In the event of a startup failure, informative error information will be provided.

More complex server setup tasks
--------------------------------

Sometimes we need to do things that aren't easily described as just a startup option.  Sometimes, we need to specify that a server should replicate from another, or pre-populate the server prior to startup, and so on.  Here is some information on the tools kewpie provides to help with such tasks

The common thread to all of these tasks is the use of the server_requests variable::

    from lib.util.mysqlBaseTestCase import mysqlBaseTestCase 

    server_requirements = [[],[],[]]
    server_requests = {'join_cluster':[(0,1), (0,2)]}
    servers = []
    server_manager = None
    test_executor = None

Setting a server as a slave to another
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In the example above, we see that we have a list with two items tied to the key 'join_cluster'.
The server_manager has a method called join_cluster with basically says::
    def join_cluster(master_server, slave_server):
        slave_server.set_master(master_server)

The way a server sets another as its master can vary greatly and is described in the relevant server object.  This currently works for standard MySQl and Galera replication

Using a pre-populated datadir
------------------------------
The experimental test runner, kewpie allows for starting a server with a pre-populated datadir
for a test case.  This is accomplished via the use of a .cnf file (versus a master.opt file)
Over time, this will be the direction for all drizzletest cases.

The .cnf file shown below tells the test-runner to use the directory drizzle/tests/std_data/backwards_compat_data
as the datadir for the first server.  If a test uses multiple servers, the .cnf file can have additional sections ([s1]...[sN])::

    [test_servers] 
    servers = [[]]

    [s0]
    load-datadir=backwards_compat_data

All datadirs are expected to be in tests/std_data.  If there is need for the ability to use datadirs outside of this location,
it can be explored.

