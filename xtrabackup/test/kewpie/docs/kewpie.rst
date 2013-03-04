**********************************
kewpie
**********************************

Synopsis
========
Database Quality Platform.  Versatile, pluggable test runner for MySQL-based databases

**./kewpie** [ *OPTIONS* ] [ TESTCASE ]

Description
===========

:program:`kewpie.py` is intended to provide a standardized
platform to facilitate testing MySQL databases.  Currently a number of MySQL variants are supported and include:
* Drizzle
* MySQL
* Percona Server
* MySQL using Galera replication  

Designed as a response to the sometimes painfully diverse ecosystem of testing tools, the intent is to provide a system that standardizes common testing tasks while providing a variety of ways to analyze the behavior of database systems.

Long experience with MySQL testing has shown that a number of tasks are common to all tests:
* Allocation and management of test servers
* Test collection and management
* Reporting of test results

Kewpie combines this with the flexibity of Python's unittest framework.  By allowing users to write tests as Python modules, we have great flexibility in how we start and set up servers, what we do to them, and how we assess their performance of our test tasks.  Some of the tasks kewpie test cases cover:
* randgen tests
* running sql-bench comprehensive tests
* running sql-bench crashme
* running sysbench
* running drizzle-test-run test cases
* more direct tests using python code and helper libraries

Provided test suites
=======================

* MySQL / Percona Server / Galera tests:

    * randgen_basic - basic randgen tests of the server (optimizer stress, etc)
    * randgen_bugs - holder suite for failing tests
    * crashme - sql-bench's crashme suite (may take some time to run)
    * sqlbench - sql-bench comprehensive suite.  (may take ~45 min. to execute)
    * cluster_basic - small, atomic tests of replication functionality.  Tests are written in such a way as to be portable across server types (!)
    * cluster_bugs - holder suite for failing tests
    * cluster_randgen - tests of replication functionality using the random query generator and relevant test loads
    * xtrabackup_basic - tests of the Percona Xtrabackup tool
    * xtrabackup_bugs - hoder suite for failing tests

* Drizzle tests:

    * randgen_basic - basic tests of the server (optimizer stress, etc)
    * The following all use the same transaction tests, but validate different functionality:

        * randgen_trxLog - tests the file-based replication log
        * randgen_innoTrxLog - tests the innodb-table-based replication log
        * randgen_slavePlugin  - tests the functionality of the replication / slave plugin

    * crashme - sql-bench's crashme suite (may take some time to run)
    * sqlbench - sql-bench comprehensive suite.  (may take ~45 min. to execute)


Running tests
=========================

There are several different ways to run tests using :program:`kewpie.py`.

It should be noted that unless :option:`--force` is used, the program will
stop execution upon encountering the first failing test. 
:option:`--force` is recommended if you are running several tests - it will
allow you to view all successes and failures in one run.

Running individual tests
------------------------
If one only wants to run a few, specific tests, they may do so this way::

    ./kewpie.py [OPTIONS] test1 [test2 ... testN]

Running all tests within a suite
--------------------------------
Many of the tests supplied with Drizzle are organized into suites.  

The tests within drizzle/tests/t are considered the 'main' suite.  
Other suites are located in either drizzle/tests/suite or within the various
directories in drizzle/plugin.  Tests for a specific plugin should live in 
the plugin's directory - drizzle/plugin/example_plugin/tests

To run the tests in a specific suite::

    ./kewpie.py [OPTIONS] --suite=SUITENAME

Running specific tests within a suite
--------------------------------------
To run a specific set of tests within a suite::

    ./kewpie.py [OPTIONS] --suite=SUITENAME TEST1 [TEST2..TESTN]

Calling tests using <suitename>.<testname> currently does not work.
One must specify the test suite via the :option:`--suite` option.


Running all available tests
---------------------------
Currently, the quickest way to execute all tests in all suites is
to use 'make test-kewpie' from the drizzle root.

Otherwise, one should simply name all suites::

    ./kewpie.py [OPTIONS] --suite=SUITE1, SUITE2, ...SUITEN

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
  * counts and percentages of total exectuted for all test statuses
  * a listing of failing, skipped, or disabled tests
  * total time spent executing the tests

Example output::

    <snip>
    30 Jan 2011 16:26:31 : main.small_tmp_table                                    [ pass ]           38
    30 Jan 2011 16:26:31 : main.snowman                                            [ pass ]           42
    30 Jan 2011 16:26:31 : main.statement_boundaries                               [ pass ]           47
    30 Jan 2011 16:26:31 : main.status                                             [ pass ]           51
    30 Jan 2011 16:26:31 : main.strict                                             [ pass ]          138
    30 Jan 2011 16:26:43 : main.subselect                                          [ fail ]        12361
    30 Jan 2011 16:26:43 : --- drizzle/tests/r/subselect.result	2011-01-30 16:23:54.975776148 -0500
    30 Jan 2011 16:26:43 : +++ drizzle/tests/r/subselect.reject	2011-01-30 16:26:43.835519303 -0500
    30 Jan 2011 16:26:43 : @@ -5,7 +5,7 @@
    30 Jan 2011 16:26:43 : 2
    30 Jan 2011 16:26:43 : explain extended select (select 2);
    30 Jan 2011 16:26:43 : id	select_type	table	type	possible_keys	key	key_len	ref	rows	filtered	Extra
    30 Jan 2011 16:26:43 : -9	PRIMARY	NULL	NULL	NULL	NULL	NULL	NULL	NULL	NULL	No tables used
    30 Jan 2011 16:26:43 : +1	PRIMARY	NULL	NULL	NULL	NULL	NULL	NULL	NULL	NULL	No tables used
    <snip>
    30 Jan 2011 16:30:20 : ================================================================================
    30 Jan 2011 16:30:20 INFO: Test execution complete in 314 seconds
    30 Jan 2011 16:30:20 INFO: Summary report:
    30 Jan 2011 16:30:20 INFO: Executed 552/552 test cases, 100.00 percent
    30 Jan 2011 16:30:20 INFO: STATUS: FAIL, 1/552 test cases, 0.18 percent executed
    30 Jan 2011 16:30:20 INFO: STATUS: PASS, 551/552 test cases, 99.82 percent executed
    30 Jan 2011 16:30:20 INFO: FAIL tests: main.subselect
    30 Jan 2011 16:30:20 INFO: Spent 308 / 314 seconds on: TEST(s)
    30 Jan 2011 16:30:20 INFO: Test execution complete
    30 Jan 2011 16:30:20 INFO: Stopping all running servers...

    
Additional uses
===============
Starting a server for manual testing
------------------------------------

:program:`kewpie.py` allows a user to get a Drizzle server up and running
quickly.  This can be useful for fast ad-hoc testing.

To do so call::

    ./kewpie.py --start-and-exit [*OPTIONS*]

This will start a Drizzle server that you can connect to and query

Starting a server against a pre-populated DATADIR
--------------------------------------------------

Using :option:`--start-dirty` prevents :program:`kewpie.py` from attempting
to initialize (clean) the datadir.  This can be useful if you want to use
an already-populated datadir for testing.

NOTE: This feature is still being tested, use caution with your data!!!

Cleanup mode
-------------
A cleanup mode is provided for user convenience.  This simply shuts down
any servers whose pid files are detected in the kewpie workdir.  It is mainly
intended as a quick cleanup for post-testing with :option:`--start-and-exit`::

	./kewpie.py --mode=cleanup

    Setting --start-dirty=True for cleanup mode...
    23 Feb 2011 11:35:59 INFO: Using Drizzle source tree:
    23 Feb 2011 11:35:59 INFO: basedir: drizzle
    23 Feb 2011 11:35:59 INFO: clientbindir: drizzle/client
    23 Feb 2011 11:35:59 INFO: testdir: drizzle/tests
    23 Feb 2011 11:35:59 INFO: server_version: 2011.02.2188
    23 Feb 2011 11:35:59 INFO: server_compile_os: unknown-linux-gnu
    23 Feb 2011 11:35:59 INFO: server_platform: x86_64
    23 Feb 2011 11:35:59 INFO: server_comment: (Source distribution (kewpie_randgen))
    23 Feb 2011 11:35:59 INFO: Using --start-dirty, not attempting to touch directories
    23 Feb 2011 11:35:59 INFO: Using default-storage-engine: innodb
    23 Feb 2011 11:35:59 INFO: Using testing mode: cleanup
    23 Feb 2011 11:35:59 INFO: Killing pid 10484 from drizzle/tests/workdir/testbot0/server0/var/run/server0.pid
    23 Feb 2011 11:35:59 INFO: Stopping all running servers...

Program architecture
====================

:program:`kewpie.py`'s 'dtr' mode uses a simple diff-based mechanism for testing.
This is the default mode and where the majority of Drizzle testing occurs.  
It will execute the statements contained in a test and compare the results 
to pre-recorded expected results.  In the event of a test failure, you
will be presented with a diff::

    main.exp1                                                    [ fail ]
    --- drizzle/tests/r/exp1.result	2010-11-02 02:10:25.107013998 +0300
    +++ drizzle/tests/r/exp1.reject	2010-11-02 02:10:32.017013999 +0300
    @@ -5,4 +5,5 @@
    a
    1
    2
    +3
    DROP TABLE t1;

A test case consists of a .test and a .result file.  The .test file includes
the various statements to be executed for a test.  The .result file lists
the expected results for a given test file.  These files live in tests/t 
and tests/r, respectively.  This structure is the same for all test suites.

kewpie.py options
===================

The :program:`kewpie.py` tool has several available options:

./kewpie.py [ OPTIONS ] [ TESTCASE ]


Options
-------

.. program:: kewpie.py

.. option:: -h, --help
 
   show this help message and exit

Configuration controls - kewpie can read config files with certain options pre-set:
---------------------------------------------------------------------------------------------------

.. option:: --sys_config_file=SYSCONFIGFILEPATH
    
   The file that specifies system configuration specs for
   kewpie to execute tests (not yet implemented)

Options for the test-runner itself
----------------------------------

.. program:: kewpie.py

.. option:: --force

    Set this to continue test execution beyond the first failed test

.. option:: --start-and-exit

   Spin up the server(s) for the first specified test then exit 
   (will leave servers running)

.. option:: --verbose

   Produces extensive output about test-runner state.  
   Distinct from --debug

.. option:: --debug

   Provide internal-level debugging output.  
   Distinct from --verbose

.. option:: --mode=MODE

   Testing mode.  
   Currently supporting dtr, sysbench, and native (unittest) modes.  The goal is to remove this and have all tests operate via unittest"
   [native]

.. option:: --record

   Record a testcase result 
   (if the testing mode supports it - MTR / DTR specific) 
   [False]

.. option:: --fast

   Don't try to cleanup from earlier runs 
   (currently just a placeholder) [False]


Options for controlling which tests are executed
------------------------------------------------

.. program:: kewpie.py

.. option:: --suite=SUITELIST

   The name of the suite containing tests we want. 
   Can accept comma-separated list (with no spaces). 
   Additional --suite args are appended to existing list 
   [autosearch]

.. option:: --suitepath=SUITEPATHS 

   The path containing the suite(s) you wish to execute. 
   Use on --suitepath for each suite you want to use.

.. option:: --do-test=DOTEST

   input can either be a prefix or a regex. 
   Will only execute tests that match the provided pattern

.. option:: --skip-test=SKIPTEST

   input can either be a prefix or a regex.  
   Will exclude tests that match the provided pattern

.. option:: --reorder

   sort the testcases so that they are executed optimally
   for the given mode [False]

.. option:: --repeat=REPEAT     

    Run each test case the specified number of times.  For
    a given sequence, the first test will be run n times,
    then the second, etc [1]

Options for defining the code that will be under test
-----------------------------------------------------

.. program:: kewpie.py

.. option:: --basedir=BASEDIR   

   Pass this argument to signal to the test-runner 
   that this is an in-tree test (not required).  
   We automatically set a number of variables 
   relative to the argument (client-bindir, 
   serverdir, testdir) [../]

.. option:: --serverdir=SERVERPATH

   Path to the server executable.  [auto-search]

.. option:: --client-bindir=CLIENTBINDIR

   Path to the directory containing client program
   binaries for use in testing [auto-search]

.. option:: --default-storage-engine=DEFAULTENGINE
                        
   Start drizzled using the specified engine [innodb]

Options for defining the testing environment
--------------------------------------------

.. program:: kewpie.py

.. option:: --testdir=TESTDIR   

    Path to the test dir, containing additional files for
    test execution. [pwd]

.. option:: --workdir=WORKDIR   

   Path to the directory test-run will use to store
   generated files and directories.
   [basedir/tests/kewpie_work]

.. option:: --top-srcdir=TOPSRCDIR

   build option [basedir_default]

.. option:: --top-builddir=TOPBUILDDIR

   build option [basedir_default]

.. option:: --no-shm            

   By default, we symlink workdir to a location in shm.
   Use this flag to not symlink [False]

.. option:: --libeatmydata      

    We use libeatmydata (if available) to disable fsyncs
    and speed up test execution.  Implies --no-shm
    
.. option:: --libeatmydata-path=LIBEATMYDATAPATH
            
   Path to the libeatmydata install you want to use
   [/usr/local/lib/libeatmydata.so]

.. option:: --start-dirty       

   Don't try to clean up working directories before test
   execution [False]

.. option:: --no-secure-file-priv
                        
   Turn off the use of --secure-file-priv=vardir for
   started servers

.. option:: --randgen-path=RANDGENPATH
           
   The path to a randgen installation that can be used to
   execute randgen-based tests [kewpie/randgen]

.. option:: --innobackupex-path=INNOBACKUPEXPATH
           
   The path to the innobackupex script that facilitates
   the use of Xtrabackup

.. option:: --xtrabackup-path=XTRABACKUPPATH
            
   The path the xtrabackup binary to be tested

.. option:: --tar4ibd-path=TAR4IBDPATH

   The path to the tar4ibd binary that will be used for any applicable tests

.. option:: --wsrep-provider-path=WSREPPROVIDER
           
   The path to a wsrep provider library for use with
   mysql
   
.. option:: --subunit-outfile=SUBUNITOUTFILE

   File path where subunit output will be logged 
   [/kewpie/workdir/test_results.subunit]

Options to pass options on to the server
-----------------------------------------

.. program:: kewpie.py

.. option:: --drizzled=DRIZZLEDOPTIONS
           
    Pass additional options to the server.  Will be passed
    to all servers for all tests (mostly for --start-and-
    exit)


Options for defining the tools we use for code analysis (valgrind, gprof, gcov, etc)
------------------------------------------------------------------------------------

.. program:: kewpie.py

.. option:: --valgrind          

   Run drizzletest and drizzled executables using
   valgrind with default options [False]

.. option:: --valgrind-option=VALGRINDARGLIST
                       
   Pass an option to valgrind (overrides/removes default
   valgrind options)

.. option:: --valgrind-suppressions=VALGRINDSUPPRESSIONS
            
   Point at a valgrind suppression file
   [kewpie/valgrind.supp]

.. option:: --helgrind

   Use the helgrind tool for valgrind.  Implies / will
   auto-use --valgrind

Options for controlling the use of debuggers with test execution
----------------------------------------------------------------

.. program:: kewpie.py

.. option:: --gdb

    Start the drizzled server(s) in gdb

.. option:: --manual-gdb

    Allows you to start the drizzled server(s) in gdb
    manually (in another window, etc

Options to call additional utilities such as datagen
------------------------------------------------------

.. program:: kewpie.py

.. option:: --gendata=GENDATAFILE
            
    Call the randgen's gendata utility to use the
    specified configuration file.  This will populate the
    server prior to any test execution

