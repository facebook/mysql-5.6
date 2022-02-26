connection slave;
source include/stop_slave.inc;
change replication source to source_auto_position=1;
source include/start_slave.inc;

connection master;
echo "Create schema and pre-insert some rows";
drop table if exists t1;
drop table if exists t2;
eval create table t1(a int primary key, b int unique, c int, d int, e mediumtext NOT NULL, key(c)) engine = $engine;
eval create table t2(a int primary key, b int, c int, d mediumtext) engine = $engine;
disable_query_log;
let $j= 1000;
while ($j <= 1020)
{
  eval insert into t1 values($j, $j, $j, $j, $j);
  inc $j;
}
enable_query_log;
source include/sync_slave_sql_with_master.inc;

connection slave;
flush logs;
let $slave_binlog_file = query_get_value("SHOW MASTER STATUS", "File", 1);
let $slave_binlog_size = query_get_value("SHOW MASTER STATUS", "Position", 1);

connection master;
echo "Execute transctions which will be recovered:";
let $i= 1;      # cursor for new rows to be inserted
let $j= 1000;   # cursor for already exiting rows

let $col= a;
echo "PrimaryKey: insert then update";
eval insert into t1 values($i, $i, $i, $i, $i);
eval update t1 set $col = $col + 100 where $col = $i;
inc $i;
echo "PrimaryKey: insert then delete";
eval insert into t1 values($i, $i, $i, $i, $i);
eval delete from t1 where $col = $i;
inc $i;
echo "PrimaryKey: update then delete";
eval update t1 set $col = $col + 100 where $col = $j;
eval delete from t1 where $col = $j + 100;
inc $j;
echo "PrimaryKey: delete then insert";
eval delete from t1 where $col = $j;
eval insert into t1 values($j, $j, $j, $j, $j);
inc $j;

let $col= b;
echo "UniqueKey: insert then update";
eval insert into t1 values($i, $i, $i, $i, $i);
eval update t1 set $col = $col + 100 where $col = $i;
inc $i;
echo "UniqueKey: insert then delete";
eval insert into t1 values($i, $i, $i, $i, $i);
eval delete from t1 where $col = $i;
inc $i;
echo "UniqueKey: update then delete";
eval update t1 set $col = $col + 100 where $col = $j;
eval delete from t1 where $col = $j + 100;
inc $j;
echo "UniqueKey: delete then insert";
eval delete from t1 where $col = $j;
eval insert into t1 values($j, $j, $j, $j, $j);
inc $j;
echo "UniqueKey: update blob";
eval insert into t1 values($i, $i, $i, $i, $i);
eval update t1 set b = b + 100, e = CONCAT('aaa', $i) where a = $i;
eval update t1 set b = b + 100 where a = $i;
inc $i;

let $col= c;
echo "NonUniqueKey: insert then update";
eval insert into t1 values($i, $i, $i, $i, $i);
eval update t1 set $col = $col + 100 where $col = $i;
inc $i;
echo "NonUniqueKey: insert then delete";
eval insert into t1 values($i, $i, $i, $i, $i);
eval delete from t1 where $col = $i;
inc $i;
echo "NonUniqueKey: update then delete";
eval update t1 set $col = $col + 100 where $col = $j;
eval delete from t1 where $col = $j + 100;
inc $j;
echo "NonUniqueKey: delete then insert";
eval delete from t1 where $col = $j;
eval insert into t1 values($j, $j, $j, $j, $j);
inc $j;

let $col= d;
echo "NonKey: insert then update";
eval insert into t1 values($i, $i, $i, $i, $j);
eval update t1 set $col = $col + 100 where $col = $i;
inc $i;
echo "NonKey: insert then delete";
eval insert into t1 values($i, $i, $i, $i, $i);
eval delete from t1 where $col = $i;
inc $i;
echo "NonKey: update then delete";
eval update t1 set $col = $col + 100 where $col = $j;
eval delete from t1 where $col = $j + 100;
inc $j;
echo "NonKey: delete then insert";
eval delete from t1 where $col = $j;
eval insert into t1 values($j, $j, $j, $j, $j);
inc $j;

echo "AllUniqueKeys: insert then update";
eval insert into t1 values($i, $i, $i, $i, $i);
eval update t1 set a = a + 100, b = b + 100 where a = $i;
inc $i;
echo "AllUniqueKeys: update then delete";
eval update t1 set a = a + 100, b = b + 100 where a = $j;
eval delete from t1 where a = $j + 100;
inc $j;

echo "AllKeys: insert then update";
eval insert into t1 values($i, $i, $i, $i, $i);
eval update t1 set a = a + 100, b = b + 100, c = c + 100 where a = $i;
inc $i;
echo "AllKeys: update then delete";
eval update t1 set a = a + 100, b = b + 100, c = c + 100 where a = $j;
eval delete from t1 where a = $j + 100;
inc $j;

echo "AllCols: insert then update";
eval insert into t1 values($i, $i, $i, $i, $i);
eval update t1 set a = a + 100, b = b + 100, c = c + 100, d = d + 100 where a = $i;
inc $i;
echo "AllCols: update then delete";
eval update t1 set a = a + 100, b = b + 100, c = c + 100, d = d + 100 where a = $j;
eval delete from t1 where a = $j + 100;
inc $j;

echo "PrimaryKey(t2): insert then update"
eval insert into t2 values(1, 1, 1);
eval update t2 set a = a + 100 where a = 1;

source include/sync_slave_sql_with_master.inc;

connection slave;
# Truncate the binlog
let $MYSQLD_DATADIR = `select @@datadir`;
exec truncate -s $slave_binlog_size $MYSQLD_DATADIR/$slave_binlog_file;
flush engine logs;

let $rpl_server_number = 2;
let $rpl_force_stop = 1;
source include/rpl_restart_server.inc;

connection slave;
source include/start_slave.inc;

connection master;
source include/sync_slave_sql_with_master.inc;

echo "Check if master and slave have the same data";
let $diff_tables= master:t1, slave:t1;
source include/diff_tables.inc;

echo "Check if slave binlogs are generated correctly";
connection slave;
exec $MYSQL_BINLOG -v -v $MYSQLD_DATADIR/slave-bin.0* > $MYSQLTEST_VARDIR/tmp/fulldump.sql;
reset master;
exec $MYSQL --user=root --host=127.0.0.1 --port=$SLAVE_MYPORT < $MYSQLTEST_VARDIR/tmp/fulldump.sql;
remove_file $MYSQLTEST_VARDIR/tmp/fulldump.sql;

let $diff_tables= master:t1, slave:t1;
source include/diff_tables.inc;

connection master;
drop table t1;
drop table t2;

source include/sync_slave_sql_with_master.inc;
