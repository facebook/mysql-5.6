# Init
--source include/rpl_connection_master.inc
--source include/sync_slave_sql_with_master.inc
--source include/rpl_connection_master.inc
flush logs;
--source include/sync_slave_sql_with_master.inc
--source include/rpl_connection_master.inc
let $master_last_log = query_get_value("SHOW MASTER STATUS", "File", 1);
eval purge binary logs to '$master_last_log';
--source include/rpl_connection_slave.inc
flush logs;
let $slave_last_log = query_get_value("SHOW MASTER STATUS", "File", 1);
eval purge binary logs to '$slave_last_log';

# Generate load
--source include/rpl_connection_master.inc
disable_result_log;
disable_query_log;
create table t1(a int auto_increment primary key) engine=innodb;
# the following loop will generate 300 meta data events, 1 for each statement
# the total number of meta data events will be 301, +1 for create stmt above
let $iter= 10;
while ($iter > 0)
{
  begin;
  eval /*insert { hello:  { world } }*/insert into t1 values();
  eval /*update { { hello : world } }*/update t1 set a= a + 1;
  /*delete { }*/delete from t1;
  commit;
  dec $iter;
}
enable_result_log;
enable_query_log;
--source include/sync_slave_sql_with_master.inc

# Check binlogs for meta data
--source include/rpl_connection_master.inc
disable_query_log;
let $MYSQLD_DATADIR = `select @@datadir`;
let $MYSQLD_SECURE_FILE_DIR = `select @@secure_file_priv`;
exec $MYSQL_BINLOG -v -v $MYSQLD_DATADIR/master-bin.0* | grep "::TRX_META_DATA::" | sed 's/\"[0-9]*\"/ts/g' > $MYSQLD_SECURE_FILE_DIR/meta_data.dat;
set sql_log_bin=0;
create table test.meta_data(meta text);
eval load data infile '$MYSQLD_SECURE_FILE_DIR/meta_data.dat' into table test.meta_data;
select meta, count(*) from test.meta_data group by meta;
drop table test.meta_data;
set sql_log_bin=1;
remove_file $MYSQLD_SECURE_FILE_DIR/meta_data.dat;

--source include/rpl_connection_slave.inc
let $MYSQLD_DATADIR = `select @@datadir`;
let $MYSQLD_SECURE_FILE_DIR = `select @@secure_file_priv`;
exec $MYSQL_BINLOG -v -v $MYSQLD_DATADIR/slave-bin.0* | grep "::TRX_META_DATA::" | sed 's/\"[0-9]*\"/ts/g' > $MYSQLD_SECURE_FILE_DIR/meta_data.dat;
set sql_log_bin=0;
create table test.meta_data(meta text);
eval load data infile '$MYSQLD_SECURE_FILE_DIR/meta_data.dat' into table test.meta_data;
select meta, count(*) from test.meta_data group by meta;
drop table test.meta_data;
set sql_log_bin=1;
remove_file $MYSQLD_SECURE_FILE_DIR/meta_data.dat;
enable_query_log;

# Cleanup
--source include/rpl_connection_master.inc
drop table t1;
