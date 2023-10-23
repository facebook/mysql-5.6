# SUMMARY
#
#    Run one iteration of the stall generator workload with a given $yield_freq
#    and assert that some or no stalls occurred based on $expect_stall.
#
# USAGE
#
#    let $db = test_db;     # DB Name to create/drop each iteration.
#    let $n_tables = 1000;  # Number of tables to create in $db
#    let $user = test_user; # User to connect to $db
#    let $yield_freq = 10;  # Admission stall frequency
#    let $expect_stall = 0; # Whether to expect a stall or not.
#    source suite/thread_pool/include/admission_control_stall_iteration.inc;

let $outfile = $MYSQLTEST_VARDIR/tmp/admission_control_stall_gen.output;
let $exec = $MYSQL_TEST_DIR/suite/thread_pool/t/admission_control_stall_gen.py
    --user=$user --port=$MASTER_MYPORT --database=$db -n $n_tables > $outfile;
let $stall_query = SELECT SUM(stalls) FROM information_schema.tp_schedulers;

--echo TODO: DROP DATABASE is currently known to stall.
eval drop database if exists $db;
eval create database $db;
eval use $db;

--echo Yield every $yield_freq queries in a batch. (0 turns it off.)
eval set global thread_pool_admission_control_admission_yield_freq = $yield_freq;

let $stalls_before = `$stall_query`;

--echo Run main workload.
exec $exec;

let $stalls_after = `$stall_query`;
expr $delta_stalls = $stalls_after - $stalls_before;

if ($expect_stall) {
    assert($delta_stalls > 0);
}
# mysqltest doesn't support `else`.
# https://dev.mysql.com/doc/dev/mysql-server/latest/PAGE_MYSQL_TEST_FLOW_CONTROL.html
if (!$expect_stall) {
    assert($delta_stalls == 0);
}
