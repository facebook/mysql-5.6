**********************************
randgen (random query generator)
**********************************



Description
===========

The randgen aka the random query generator is a database
testing tool.  It uses a grammar-based stochastic model to represent
some desired set of queries (to exercise the optimizer, for example) 
and generates random queries as allowed by the grammar

The primary documentation is here: http://forge.mysql.com/wiki/RandomQueryGenerator

This document is intended to help the user set up their environment so that the tool
may be used in conjunction with the kewpie.py test-runner.  The forge documentation
contains more information on the particulars of the tool itself.

Requirements
============

DBD::drizzle
-------------
The DBD::drizzle module is required it can be found here http://launchpad.net/dbd-drizzle/

Additional information for installing the module::

    Prerequisites
    ----------------
    * Perl
    * Drizzle (bzr branch lp:drizzle)
    * libdrizzle (bzr branch lp:libdrizzle)
    * C compiler

    Installation
    -------------
    You should only have to run the following:

    perl Makefile.PL --cflags=-I/usr/local/drizzle/include/ --libs=-"L/usr/local/drizzle/lib -ldrizzle"


    Depending on where libdrizzle is installed. Also, you'll want to make 
    sure that ldconfig has configured libdrizzle to be in your library path 

Additional information may be found here: http://forge.mysql.com/wiki/RandomQueryGeneratorQuickStart

Installing the randgen
=======================

Kewpie includes a branch of the randgen so it is not necessary to install it directly, but thecode may be branched from:

    launchpad: bzr branch lp:randgen

it also may be downloaded from: 

    http://launchpad.net/randgen/+download

That is all there is : )

Randgen / kewpie testcases
============================

These tests use kewpie's native mode and are essentially unittest modules.  Currently, the suites are designed so that each individual modules executes a single randgen command line and validates results accordingly.  An example::

    class basicTest(mysqlBaseTestCase):

        def test_OptimizerSubquery1(self):
            self.servers = servers
            test_cmd = ("./gentest.pl "
                        "--gendata=conf/percona/outer_join_percona.zz "
                        "--grammar=conf/percona/outer_join_percona.yy "
                        "--queries=500 "
                        "--threads=5"
                       )
      retcode, output = self.execute_randgen(test_cmd, test_executor, servers)
      self.assertEqual(retcode, 0, output) ``

Running tests
=========================

There are several different ways to run the provided randgen tests.

It should be noted that unless :option:`--force` is used, the program will
stop execution upon encountering the first failing test. 
:option:`--force` is recommended if you are running several tests - it will
allow you to view all successes and failures in one run.

Running individual tests
------------------------
If one only wants to run a few, specific tests, they may do so this way::

    ./kewpie --suite=randgen_basic [OPTIONS] test1 [test2 ... testN]

Running all tests within a suite
--------------------------------
Many of the tests supplied with Drizzle are organized into suites.  

The tests within drizzle/tests/randgen_tests/main are considered the 'main' suite.  
Other suites are also subdirectories of drizzle/tests/randgen_tests.

To run the tests in a specific suite::

    ./kewpie [OPTIONS] --suite=SUITENAME

Running specific tests within a suite
--------------------------------------
To run a specific set of tests within a suite::

    ./kewpie --suite=SUITENAME TEST1 [TEST2..TESTN]

Calling tests using <suitename>.<testname> currently does not work.
One must specify the test suite via the :option:`--suite` option.


Running all available tests
---------------------------
One would currently have to name all suites, but the majority of the working tests live in the main suite
Other suites utilize more exotic server combinations and we are currently tweaking them to better integrate with the 
kewpie system.  The slave-plugin suite does currently have a good config file for setting up simple replication setups for testing.
To execute several suites' worth of tests::

    ./kewpie --mode=randgen --randgen-path=/path/to/randgen [OPTIONS] --suite=SUITE1, SUITE2, ...SUITEN

Interpreting test results
=========================
The output of the test runner is quite simple.  Every test should pass.
In the event of a test failure, please take the time to file a bug here:
*https://bugs.launchpad.net/drizzle*

During a run, the program will provide the user with:
  * test name (suite + name)
  * test status (pass/fail/skipped)
  * time spent executing each test

At the end of a run, the program will provide the user with a listing of:
  * how many tests were run
  * how many tests failed
  * percentage of passing tests
  * a listing of failing tests
  * total time spent executing the tests

Example output::

    24 Feb 2011 17:27:36 : main.outer_join_portable                                [ pass ]         7019
    24 Feb 2011 17:27:39 : main.repeatable_read                                    [ pass ]         2764
    24 Feb 2011 17:28:57 : main.select_stability_validator                         [ pass ]        77946
    24 Feb 2011 17:29:01 : main.subquery                                           [ pass ]         4474
    24 Feb 2011 17:30:52 : main.subquery_semijoin                                  [ pass ]       110355
    24 Feb 2011 17:31:00 : main.subquery_semijoin_nested                           [ pass ]         8750
    24 Feb 2011 17:31:03 : main.varchar                                            [ pass ]         3048
    24 Feb 2011 17:31:03 : ================================================================================
    24 Feb 2011 17:31:03 INFO: Test execution complete in 288 seconds
    24 Feb 2011 17:31:03 INFO: Summary report:
    24 Feb 2011 17:31:03 INFO: Executed 18/18 test cases, 100.00 percent
    24 Feb 2011 17:31:03 INFO: STATUS: PASS, 18/18 test cases, 100.00 percent executed
    24 Feb 2011 17:31:03 INFO: Spent 287 / 288 seconds on: TEST(s)
    24 Feb 2011 17:31:03 INFO: Test execution complete
    24 Feb 2011 17:31:03 INFO: Stopping all running servers...

    
Additional uses
===============
Starting a server for manual testing and (optionally) populating it
--------------------------------------------------------------------

:doc:`kewpie` 's randgen mode allows a user to get a Drizzle server up and running quickly.  This can be useful for fast ad-hoc testing.

To do so call::

    ./kewpie --suite=randgen_basic --start-and-exit [*OPTIONS*]

This will start a Drizzle server that you can connect to and query

With the addition of the --gendata option, a user may utilize the randgen's gendata (table creation and population) tool
to populate a test server.  In the following example, the test server is now populated by the 8 tables listed below::

    ./kewpie --start-and-exit --gendata=conf/drizzle/drizzle.zz
    <snip>
    24 Feb 2011 17:48:48 INFO: NAME: server0
    24 Feb 2011 17:48:48 INFO: MASTER_PORT: 9306
    24 Feb 2011 17:48:48 INFO: DRIZZLE_TCP_PORT: 9307
    24 Feb 2011 17:48:48 INFO: MC_PORT: 9308
    24 Feb 2011 17:48:48 INFO: PBMS_PORT: 9309
    24 Feb 2011 17:48:48 INFO: RABBITMQ_NODE_PORT: 9310
    24 Feb 2011 17:48:48 INFO: VARDIR: /kewpie/tests/workdir/testbot0/server0/var
    24 Feb 2011 17:48:48 INFO: STATUS: 1
    # 2011-02-24T17:48:48 Default schema: test
    # 2011-02-24T17:48:48 Executor initialized, id GenTest::Executor::Drizzle 2011.02.2198 ()
    # 2011-02-24T17:48:48 # Creating Drizzle table: test.A; engine: ; rows: 0 .
    # 2011-02-24T17:48:48 # Creating Drizzle table: test.B; engine: ; rows: 0 .
    # 2011-02-24T17:48:48 # Creating Drizzle table: test.C; engine: ; rows: 1 .
    # 2011-02-24T17:48:48 # Creating Drizzle table: test.D; engine: ; rows: 1 .
    # 2011-02-24T17:48:48 # Creating Drizzle table: test.AA; engine: ; rows: 10 .
    # 2011-02-24T17:48:48 # Creating Drizzle table: test.BB; engine: ; rows: 10 .
    # 2011-02-24T17:48:48 # Creating Drizzle table: test.CC; engine: ; rows: 100 .
    # 2011-02-24T17:48:49 # Creating Drizzle table: test.DD; engine: ; rows: 100 .
    24 Feb 2011 17:48:49 INFO: User specified --start-and-exit.  kewpie.py exiting and leaving servers running...



