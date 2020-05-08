# Setup
connection master;
call mtr.add_suppression("Databases were empty for this trx");
create database test1;
create database test2;

create table test1.t1(a int primary key, b char(8)) engine=InnoDB;
create table test2.t1(a int primary key, b char(8)) engine=InnoDB;
--source include/sync_slave_sql_with_master.inc

# Set minimum_hlc_ns to a high value. Subsequent txn's should see monotonically
# increasing timestamp from this point
connection master;
SET SESSION DEBUG="+d,allow_long_hlc_drift_for_tests";
SET @@global.minimum_hlc_ns = 2538630000000000000; # ~2050 AD

# Enable binlog_hlc and maintain_database_hlc in both master and slave
connection master;
SET @@global.enable_binlog_hlc = true;
SET @@global.maintain_database_hlc = true;
connection slave;
SET @@global.enable_binlog_hlc = true;
SET @@global.maintain_database_hlc = true;

--echo Case 1: Single statement txn
connection master;
use test1;
insert into t1 values(1, 'a');
insert into t1 values(2, 'b');
--source include/show_binlog_events.inc
select * from test1.t1;
select * from information_schema.database_applied_hlc order by 2;

connection slave;
--source include/sync_slave_sql_with_master.inc
--source include/show_binlog_events.inc
select * from test1.t1;
select * from information_schema.database_applied_hlc order by 2;

--echo Case 2: Multi statement txn
connection master;
set autocommit=off;

use test1;
insert into t1 values(3, 'c');
insert into t1 values(4, 'd');
commit;
--source include/show_binlog_events.inc
select * from test1.t1;
select * from information_schema.database_applied_hlc order by 2;

connection slave;
--source include/sync_slave_sql_with_master.inc
--source include/show_binlog_events.inc
select * from test1.t1;
select * from information_schema.database_applied_hlc order by 2;

--echo Case 3: Multi statement txn spanning multiple database
connection master;
set autocommit=off;

insert into test1.t1 values(5, 'e');
insert into test2.t1 values(5, 'e');
commit;
--source include/show_binlog_events.inc
select * from test1.t1;
select * from test2.t1;
select * from information_schema.database_applied_hlc order by 2;

connection slave;
--source include/sync_slave_sql_with_master.inc
--source include/show_binlog_events.inc
select * from test1.t1;
select * from test2.t1;
select * from information_schema.database_applied_hlc order by 2;

--echo Case 4: Multiple single stmt txns txn spanning multiple database
connection master;
set autocommit=off;

insert into test1.t1 values(6, 'f');
commit;

insert into test2.t1 values(8, 'g');
commit;

--source include/show_binlog_events.inc
select * from test1.t1;
select * from test2.t1;
select * from information_schema.database_applied_hlc order by 2;

connection slave;
--source include/sync_slave_sql_with_master.inc
--source include/show_binlog_events.inc
select * from test1.t1;
select * from test2.t1;
select * from information_schema.database_applied_hlc order by 2;


--echo Case 5: Pure DDL should also update and maintain per database HLC
connection master;
set autocommit=off;

create table test1.t2(a int);
create table test2.t2(a int);
create database test3;
select * from information_schema.database_applied_hlc order by 2;

connection slave;
--source include/sync_slave_sql_with_master.inc
select * from information_schema.database_applied_hlc order by 2;

-- echo Case 6: Duplicate enabling of maintain_database_hlc should not clear the map
connection master;
SET @@global.maintain_database_hlc = ON;
SET @@global.maintain_database_hlc = ON;
select * from information_schema.database_applied_hlc order by 2;

connection slave;
SET @@global.maintain_database_hlc = ON;
SET @@global.maintain_database_hlc = ON;
select * from information_schema.database_applied_hlc order by 2;

-- echo Case 7: Duplicate enabling of binlog_hlc should not clear the map
connection master;
SET @@global.enable_binlog_hlc = ON;
SET @@global.enable_binlog_hlc = ON;
select * from information_schema.database_applied_hlc order by 2;

connection slave;
SET @@global.enable_binlog_hlc = ON;
SET @@global.enable_binlog_hlc = ON;
select * from information_schema.database_applied_hlc order by 2;

-- echo Case 8: Disabling maintain_database_hlc should clear the map
connection master;
SET @@global.maintain_database_hlc = OFF;
select * from information_schema.database_applied_hlc order by 2;

connection slave;
SET @@global.maintain_database_hlc = OFF;
select * from information_schema.database_applied_hlc order by 2;

-- echo Case 9: Disabling enable_binlog_hlc should clear the map
connection master;
SET @@global.maintain_database_hlc = ON;
connection slave;
SET @@global.maintain_database_hlc = ON;

connection master;
insert into test1.t2 values(100);
select * from information_schema.database_applied_hlc order by 2;
SET @@global.enable_binlog_hlc = OFF;
select * from information_schema.database_applied_hlc order by 2;

connection slave;
--source include/sync_slave_sql_with_master.inc
select * from information_schema.database_applied_hlc order by 2;
SET @@global.enable_binlog_hlc = OFF;
select * from information_schema.database_applied_hlc order by 2;

# Cleanup
connection master;
SET @@global.minimum_hlc_ns = default;
SET @@global.enable_binlog_hlc = default;
SET @@global.maintain_database_hlc = default;

drop table if exists test1.t1;
drop table if exists test2.t1;
drop table if exists test1.t2;
drop table if exists test2.t2;
drop database test1;
drop database test2;
drop database test3;

connection slave;
--source include/sync_slave_sql_with_master.inc
SET @@global.enable_binlog_hlc = default;
SET @@global.maintain_database_hlc = default;

connection master;
source include/rpl_end.inc;
