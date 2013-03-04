**********************************
sysbench
**********************************



Description
===========
kewpie's sysbench mode allows a user to run a specific iteration of a sysbench test (eg an oltp readonly run at concurrency = 16)


Requirements
============

The SYSBENCH command requires that the Drizzle library header be installed. Simply build Drizzle and do::

    $> sudo make install

This will install the right headers.

The SYSBENCH command also requires installation of the drizzle-sysbench program, which can be installed from source like so::

    $> bzr branch lp:~drizzle-developers/sysbench/trunk  drizzle-sysbench
    $> cd drizzle-sysbench
    $> ./autogen.sh && ./configure && make && sudo make install

Make sure sysbench is then in your path


sysbench / kewpie tests
=====================

A sysbench test defines a run for a particular concurrency.  There are suites for readonly and readwrite.
They are currently broken down this way as an experiment - we are open to other ways of organizing these tests::

    [test_info]
    comment = 16 threads

    [test_command]
    command = sysbench --max-time=240 --max-requests=0 --test=oltp --db-ps-mode=disable --drizzle-table-engine=innodb --oltp-read-only=on --oltp-table-size=1000000 --drizzle-mysql=on --drizzle-user=root --drizzle-db=test --drizzle-port=$MASTER_MYPORT --drizzle-host=localhost --db-driver=drizzle --num-threads=16

    [test_servers]
    servers = [[innodb.buffer-pool-size=256M innodb.log-file-size=64M innodb.log-buffer-size=8M innodb.thread-concurrency=0 innodb.additional-mem-pool-size=16M table-open-cache=4096 table-definition-cache=4096 mysql-protocol.max-connections=2048]]

Running tests
=========================

There are several different ways to run tests using :doc:`kewpie` 's sysbench mode.

It should be noted that unless :option:`--force` is used, the program will
stop execution upon encountering the first failing test. 
:option:`--force` is recommended if you are running several tests - it will
allow you to view all successes and failures in one run.

Running individual tests
------------------------
If one only wants to run a few, specific tests, they may do so this way::

    ./kewpie --mode=sysbench [OPTIONS] test1 [test2 ... testN]

Running all tests within a suite
--------------------------------
Many of the tests supplied with Drizzle are organized into suites.  

The tests within drizzle/tests/randgen_tests/main are considered the 'main' suite.  
Other suites are also subdirectories of drizzle/tests/randgen_tests.

To run the tests in a specific suite::

    ./kewpie --mode=sysbench [OPTIONS] --suite=SUITENAME

Running specific tests within a suite
--------------------------------------
To run a specific set of tests within a suite::

    ./kewpie --mode=sysbench [OPTIONS] --suite=SUITENAME TEST1 [TEST2..TESTN]

Calling tests using <suitename>.<testname> currently does not work.
One must specify the test suite via the :option:`--suite` option.


Running all available tests
---------------------------
One would currently have to name all suites, but the majority of the working tests live in the main suite
Other suites utilize more exotic server combinations and we are currently tweaking them to better integrate with the 
kewpie system.  The slave-plugin suite does currently have a good config file for setting up simple replication setups for testing.
To execute several suites' worth of tests::

    ./kewpie --mode=sysbench [OPTIONS] --suite=SUITE1, SUITE2, ...SUITEN

Interpreting test results
=========================
The output of the test runner is quite simple.  Every test should pass.
In the event of a test failure, please take the time to file a bug here:
*https://bugs.launchpad.net/drizzle*

During a run, the program will provide the user with:
  * test name (suite + name)
  * test status (pass/fail/skipped)
  * time spent executing each test

Example output::

    20110601-191706  ===============================================================
    20110601-191706  TEST NAME                                  [ RESULT ] TIME (ms)
    20110601-191706  ===============================================================
    20110601-191706  readonly.concurrency_16                    [ pass ]   240019
    20110601-191706  max_req_lat_ms: 21.44
    20110601-191706  rwreqps: 4208.2
    20110601-191706  min_req_lat_ms: 6.31
    20110601-191706  deadlocksps: 0.0
    20110601-191706  tps: 150.29
    20110601-191706  avg_req_lat_ms: 6.65
    20110601-191706  95p_req_lat_ms: 7.02
    20110601-191706  ===============================================================
    20110601-191706 INFO Test execution complete in 275 seconds
    20110601-191706 INFO Summary report:
    20110601-191706 INFO Executed 1/1 test cases, 100.00 percent
    20110601-191706 INFO STATUS: PASS, 1/1 test cases, 100.00 percent executed
    20110601-191706 INFO Spent 240 / 275 seconds on: TEST(s)
    20110601-191706 INFO Test execution complete
    20110601-191706 INFO Stopping all running servers...

