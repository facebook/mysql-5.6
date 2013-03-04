**********************************
sql-bench
**********************************



Description
===========
Kewpie can take advantage of two testing modes offered by the sql-bench tool.

Kewpie has a copy of this tool set in the tree and the test-runner offers two suites for executing sql-bench tests::

    sql-bench modes
    ---------------
    * sqlbench - runs the entire sql-bench test suite and can take a very long time (~45 minutes)
    * crashme - runs the crash-me tool and reports failure if any tests should not pass


Requirements
============
DBD::drizzle
-------------
The DBD::drizzle module is required it can be found here:

    http://launchpad.net/dbd-drizzle/

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

DBD::MySQL:

This is required for using MySQL servers and can be found here:

    http://search.cpan.org/dist/DBD-mysql/


sql-bench / kewpie tests
==========================

Currently, there are only two sql-bench test cases for kewpie.  As one might expect, main.all_sqlbench_tests executes::

    run-all-tests --server=$SERVER_TYPE --dir=$WORKDIR --log --connect-options=port=$MASTER_MYPORT --create-options=ENGINE=innodb --user=root 

against a test server.  The second test case executes the crashme tool against a running server.

Test cases are defined in python .cnf files and live in tests/sqlbench_tests.

Running tests
=========================

NOTE:  all_sqlbench_tests can take a significant amount of time to execute (45 minutes or so on a decently provisioned laptop)

There are several different ways to run tests using :doc:`kewpie` 's sql-bench mode.

It should be noted that unless :option:`--force` is used, the program will
stop execution upon encountering the first failing test. 
:option:`--force` is recommended if you are running several tests - it will
allow you to view all successes and failures in one run.

At present, sql-bench output in a work in progress.  It does report a simple pass/fail, but we are working on alternate ways of viewing / storing the results (and for other testing modes as well)


Running all tests within a suite
--------------------------------
At present, there is only one test case per suite for sqlbench and crashme modes - that is all that is needed for these tools.
To execute the sql-bench test suite::

    ./kewpie --suite=sqlbench

To execute the crash-me test suite::

    ./kewpie --suite=crashme

Interpreting test results
=========================
The output of the test runner is quite simple.  Every test should pass.
In the event of a test failure, please take the time to file a bug here:
*https://bugs.launchpad.net/drizzle*

During a run, the program will provide the user with:
  * test name (suite + name)
  * test status (pass/fail/skipped)
  * time spent executing each test

Example sqlbench output::

    20110608-135645  ===============================================================
    20110608-135645  TEST NAME                                  [ RESULT ] TIME (ms)
    20110608-135645  ===============================================================
    20110608-135645  main.all_sqlbench_tests                    [ pass ]  2732007
    20110608-135645  Test finished. You can find the result in:
    20110608-135645  drizzle/tests/workdir/RUN-drizzle-Linux_2.6.38_9_generic_x86_64
    20110608-135645  Benchmark DBD suite: 2.15
    20110608-135645  Date of test:        2011-06-08 13:11:10
    20110608-135645  Running tests on:    Linux 2.6.38-9-generic x86_64
    20110608-135645  Arguments:           --connect-options=port=9306 --create-options=ENGINE=innodb
    20110608-135645  Comments:
    20110608-135645  Limits from:
    20110608-135645  Server version:      Drizzle 2011.06.19.2325
    20110608-135645  Optimization:        None
    20110608-135645  Hardware:
    20110608-135645  
    20110608-135645  alter-table: Total time: 42 wallclock secs ( 0.06 usr  0.04 sys +  0.00 cusr  0.00 csys =  0.10 CPU)
    20110608-135645  ATIS: Total time: 22 wallclock secs ( 4.01 usr  0.26 sys +  0.00 cusr  0.00 csys =  4.27 CPU)
    20110608-135645  big-tables: Total time: 24 wallclock secs ( 4.16 usr  0.22 sys +  0.00 cusr  0.00 csys =  4.38 CPU)
    20110608-135645  connect: Total time: 31 wallclock secs ( 6.81 usr  4.50 sys +  0.00 cusr  0.00 csys = 11.31 CPU)
    20110608-135645  create: Total time: 59 wallclock secs ( 2.93 usr  1.65 sys +  0.00 cusr  0.00 csys =  4.58 CPU)
    20110608-135645  insert: Total time: 1962 wallclock secs (270.53 usr 66.35 sys +  0.00 cusr  0.00 csys = 336.88 CPU)
    20110608-135645  select: Total time: 560 wallclock secs (23.12 usr  4.62 sys +  0.00 cusr  0.00 csys = 27.74 CPU)
    20110608-135645  transactions: Total time: 21 wallclock secs ( 2.43 usr  1.98 sys +  0.00 cusr  0.00 csys =  4.41 CPU)
    20110608-135645  wisconsin: Total time: 10 wallclock secs ( 2.11 usr  0.52 sys +  0.00 cusr  0.00 csys =  2.63 CPU)
    20110608-135645  
    20110608-135645  All 9 test executed successfully
    20110608-135645  
    20110608-135645  Totals per operation:
    20110608-135645  Operation             seconds     usr     sys     cpu   tests
    20110608-135645  alter_table_add                       18.00    0.02    0.00    0.02     100
    20110608-135645  alter_table_drop                      17.00    0.02    0.01    0.03      91
    20110608-135645  connect                                2.00    1.02    0.51    1.53    2000
    <snip>
    20110608-135645  update_rollback                        3.00    0.26    0.23    0.49     100
    20110608-135645  update_with_key                       73.00    6.70    5.23   11.93  300000
    20110608-135645  update_with_key_prefix                34.00    4.45    2.30    6.75  100000
    20110608-135645  wisc_benchmark                         2.00    1.49    0.00    1.49     114
    20110608-135645  TOTALS                              2865.00  310.26   79.94  390.20 2974250
    20110608-135645  
    20110608-135645  ===============================================================
    20110608-135645 INFO Test execution complete in 2735 seconds
    20110608-135645 INFO Summary report:
    20110608-135645 INFO Executed 1/1 test cases, 100.00 percent
    20110608-135645 INFO STATUS: PASS, 1/1 test cases, 100.00 percent executed
    20110608-135645 INFO Spent 2732 / 2735 seconds on: TEST(s)
    20110608-135645 INFO Test execution complete
    20110608-135645 INFO Stopping all running servers...

Example crashme output::

    20110608-152759  ===============================================================
    20110608-152759  TEST NAME                                  [ RESULT ] TIME (ms)
    20110608-152759  ===============================================================
    20110608-152759  main.crashme                               [ fail ]   155298
    20110608-152759  func_extra_to_days=error		# Function TO_DAYS
    20110608-152759  ###
    20110608-152759  ###<select to_days('1996-01-01') from crash_me_d
    20110608-152759  ###>2450084
    20110608-152759  ###We expected '729024' but got '2450084'
    20110608-152759  func_odbc_timestampadd=error		# Function TIMESTAMPADD
    20110608-152759  ###
    20110608-152759  ###<select timestampadd(SQL_TSI_SECOND,1,'1997-01-01 00:00:00')
    20110608-152759  ###>1997-01-01 00:00:01.000000
    20110608-152759  ###We expected '1997-01-01 00:00:01' but got '1997-01-01 00:00:01.000000'
    20110608-152759  ###
    20110608-152759  ###<select {fn timestampadd(SQL_TSI_SECOND,1,{ts '1997-01-01 00:00:00'}) }
    20110608-152759  ###>1997-01-01 00:00:01.000000
    20110608-152759  ###We expected '1997-01-01 00:00:01' but got '1997-01-01 00:00:01.000000'
    20110608-152759  
    20110608-152759 ERROR Failed test.  Use --force to execute beyond the first test failure
    20110608-152759  ===============================================================
    20110608-152759 INFO Test execution complete in 158 seconds
    20110608-152759 INFO Summary report:
    20110608-152759 INFO Executed 1/1 test cases, 100.00 percent
    20110608-152759 INFO STATUS: FAIL, 1/1 test cases, 100.00 percent executed
    20110608-152759 INFO FAIL tests: main.crashme
    20110608-152759 INFO Spent 155 / 158 seconds on: TEST(s)
    20110608-152759 INFO Test execution complete

