connection master;
call mtr.add_suppression('Slave SQL:.* Could not execute Write_rows event '
                         'on table test.test_table2');
call mtr.add_suppression('The slave coordinator and worker threads are stopped,'
                         ' possibly leaving data in inconsistent state');
create table test_table1 (a int primary key);
create table test_table2 (a int primary key);
insert into test_table1 values(1);
insert into test_table2 values(1);

sync_slave_with_master;
set @old_var = @@global.rbr_idempotent_tables;
source include/stop_slave_sql.inc;
set @@global.rbr_idempotent_tables='test_table1';
source include/start_slave_sql.inc;

connection slave;
insert into test_table1 values(2);
insert into test_table2 values(2);

connection master;
# Slave SQL thread doesn't hit duplicate key error.
insert into test_table1 values(2);

sync_slave_with_master;
let $rpl_server_parameters= --rbr_idempotent_tables=test_table1,test_table3;
let $rpl_server_number=2;
source include/rpl_restart_server.inc;

connection slave;
source include/start_slave.inc;
insert into test_table1 values(3);
connection master;
# Slave SQL thread doesn't hit duplicate key error after restart.
insert into test_table1 values(3);

sync_slave_with_master;
source include/stop_slave_sql.inc;
set @@global.rbr_idempotent_tables = @old_var;
source include/start_slave_sql.inc;

connection master;
insert into test_table2 values(2);

# Verify slave hits duplicate key on tables that are not ignored.
let $slave_sql_errno = convert_error(ER_DUP_ENTRY);
source include/wait_for_slave_sql_error_and_skip.inc;

connection master;
drop table test_table1;
drop table test_table2;
