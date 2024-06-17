Environment Setup
=================
Before using the test framework (referred to as MYR), the following items need to be setup.

Environment Variables
---------------------
The following environment variables need to be setup in your test user account.

MYRHOME=[dirPath]
MYR can be installed in a directory of your choice specified by dirPath.  /home/qa/myr will be used throughout this document.
ex: MYRHOME=/home/qa/myr

MYRRELHOME=[dirPath]
MYR gets source code from GitHub, makes the project, and store it in subdirectories under this path.
ex: MYRRELHOME=/home/myrocks/builds

MYRBUILD=[buildName]  *See "Creating Builds" below
MYR supports multiple builds.  Quickly switch between them for comparison can be done by setting this variable.
ex: MYRBUILD=mytest

mysqld ports
------------
Both master and slave ports can be configured by changing the following file:
$MYRHOME/common/env/setup.txt

---
myrMasterPort=3306
myrSlave1Port=3307
---

Please also changed the .cnf files in $MYRHOME/myrocks/cnf directory to reflect the desired port used.

myrclient.sh script
-------------------
The myrclient.sh script sets additional environment variables at runtime.  It should be sourced in any script that executes MySQL statements, after setting the MYRBUILD variable, such as the following:

---
MYRBUILD=nightly
source $MYRHOME/common/env/myrclient.sh
$MYRCLIENT testdb -e "select * from test;"
---

The script setups the following additional variables, which can be used in scripts.

#
export MYRMASTERPORT=`cat $MYRHOME/common/env/setup.txt|grep myrMasterPort|awk -F"=" '{print $2}'`
export MYRSLAVE1PORT=`cat $MYRHOME/common/env/setup.txt|grep myrSlave1Port|awk -F"=" '{print $2}'`
#
export MYRCLIENTDIR=$HOME/builds/$MYRBUILD/mysql-5.6/client
export MYRCLIENT="$MYRCLIENTDIR/mysql -uroot --port=$MYRMASTERPORT --socket=/tmp/mysql.sock"
export MYRCMASTER="$MYRCLIENTDIR/mysql -uroot --port=$MYRMASTERPORT --socket=/tmp/mysql.sock"
export MYRCSLAVE1="$MYRCLIENTDIR/mysql -uroot --port=$MYRSLAVE1PORT --socket=/tmp/repSlave1.sock"


Creating Builds
---------------
To pull source code from GitHub and compile a build in the $HOME/builds directory, please execute:

Single server
	$MYRHOME/myrocks/sh/buildEnvSingle.sh buildName
	
Replication server
	$MYRHOME/myrocks/sh/buildEnvRep.sh buildName

The following build names can be used for compiling builds.

Single server
	innodb
	myrref
	myrtst
	mytest
	nightly
	
Replication server
	reptest

Additional build names can be supported by adding corresponding .cnf files in the $MYRHOME/myrocks/cnf directory.

Test Cases
==========
Test cases can be found in the $MYRHOME/testSuites.txt file.

---
#RunTest	category	testName
#
=Y= rqg	examples
=Y= rqg	runtime
=Y= rqg	transactions

=Y= performance	groupTest
#
=Y=	concurrency		concurDDL
=Y=	concurrency		blockTrans
=Y=	concurrency		concurTrans
=Y=	concurrency		concurWriteI124
#
#=N= stability   dwweek
---

The rqg test are test cases from rqg itself.  I learned what they do by the grammer and data files (you can check the goAll.sh for these file names).  I don't really know how it is done, as I did not spend time to understand the grammer syntax.

The groupTest was designed to measure performance and I use to have tools to interpret results for more meaningful presentation using Excel.  Unfortunately, I no longer have those tools.  If required, it can be recreated, but will take time.  I short circuited it by outputting the number of seconds it took to complete the query executed.  The test will take longer since it has to create and load the tables before executing the queries.

concurDDL - Runs DDL statements in multiple concurrent sessions.  Check for error.
blockTrans - A timed concurrent transaction sequence, with predetermined expected result.
concurTrans - Runs transactions concurrently.  Some of them will time out.  Verify final
              table results against DML statement return status
concurWriteI124 - Test case specified in issue #124
              
Actual test cases reside in $MYRHOME/[category]/[testName] directory.  It may contain the following subdirectories:

data		data used for the test case
ref			predetermined reference results
sql			sql scripts
sh			sh scripts
test		main test driver
testcase	test cases that call the main driver, with different sets of parameters
			The main test case is goAll.sh
			You can add additional test cases to the goAll.sh file,
			or create separate script files.
			

Execute a single test
---------------------
1. Change to your test directory where tests result will be saved
2. Execute $MYRHOME/[category]/[testName]/testcase/testCaseScriptFile

Intermediate test status will be stored in testStatus.txt
Final test status will be stored in status.txt


Execute all test
----------------
1. Execute $MYRHOME/testSuites.sh

The testSuites.sh script will call the goAll.sh for each test.  Test cases not included in the goAll.sh script will NOT be executed.

All test results will be stored in ~/tests directory, under a [category]/[testName] directory structure.  Status for all tests are stored in allStatus.txt

Please make sure to edit the testSuites.txt file to enable only the tests that you want to execute.

Please DO NOT enable the dwweek test case, as it is a special test case on its own.


The dwweek test
---------------
This is the "Life of a Data Warehouse in a week" test.  It actually runs forever until it is manually stopped.  The test does not verify test result correctness.  We just want to make sure the server will run continuously.

This test requires the groupTest testcase in the performance category:
$MYRHOME/performance/groupTest

By default, it executes the following tests:

1. During the day, three queries jobs with different time pauses between iterations.  Each job runs a number of concurrent queries.
2. At night, load data using LID
3. At night, delete rows marked for deletion the previous day
4. At night, mark some rows for deletion for the next day

You may need to change the amount of data to be loaded, both initially and nightly, and the number of rows to be deleted nightly per the performance of the VM or physically server that you are using.  The test was originally configured to run on three powerful physical servers.  As I was migrating the test to run on a VM for MyRocks, deleting rows never finished and stuck in the following day.  It took several adjustments to get this running smoothly.

These is a memUsageUM.log file that contains output from the "top" command during test execution.  It would be helpful for checking memory utilization for the test.

You can customize the test by changing the testcase/myrocks.sh script.  You can also include your own SQL scripts as a group/groups by adding them to the $MYRHOME/performance/groupTest/sql directory and refer to them by group number in the myrocks.sh script.

To stop the test:
echo 0 > continue.txt

The flag prevents the main driver from executing the next test iteration.  The current test iteration may take some time to finish.

























