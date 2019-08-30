# create schema and insert rows
connection master;
eval create table t1
(a int primary key, b int, c int, d int, key sec_key(b)) engine = $engine;

insert into t1 values(1, 1, 1, 1);
insert into t1 values(2, 2, 2, 2);
insert into t1 values(3, 3, 3, 3);
insert into t1 values(4, 4, 4, 4);

source include/sync_slave_sql_with_master.inc;

echo "case 1: add column in the end on slave";
connection slave;
set @@sql_log_bin = 0;
alter table t1 add column e int;
set @@sql_log_bin = 1;

connection master;
insert into t1 values(5, 5, 5, 5);
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t1;
select * from t1 force index(sec_key);
connection master;
update t1 set d = 50 where a = 5;
update t1 set c = 50 where a = 5;
update t1 set b = 50 where a = 5;
update t1 set a = 50 where a = 5;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t1;
select * from t1 force index(sec_key);
connection master;
delete from t1 where a = 50;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t1;
select * from t1 force index(sec_key);

# drop the extra col on slave
connection slave;
set @@sql_log_bin = 0;
alter table t1 drop column e;
set @@sql_log_bin = 1;

echo "case 2: drop last column on slave";
connection slave;
set @@sql_log_bin = 0;
alter table t1 drop column d;
set @@sql_log_bin = 1;

connection master;
insert into t1 values(5, 5, 5, 5);
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t1;
select * from t1 force index(sec_key);
connection master;
update t1 set d = 50 where a = 5;
update t1 set c = 50 where a = 5;
update t1 set b = 50 where a = 5;
update t1 set a = 50 where a = 5;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t1;
select * from t1 force index(sec_key);
connection master;
delete from t1 where a = 50;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t1;
select * from t1 force index(sec_key);

# add the column back on the slave
connection slave;
set @@sql_log_bin = 0;
alter table t1 add column d int;
update t1 set d = c;
set @@sql_log_bin = 1;

echo "case 3: rename column on slave";
connection slave;
set @@sql_log_bin = 0;
alter table t1 change b bb int;
alter table t1 change c cc int;
set @@sql_log_bin = 1;

connection master;
insert into t1 values(5, 5, 5, 5);
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t1;
select * from t1 force index(sec_key);
connection master;
update t1 set d = 50 where a = 5;
update t1 set c = 50 where a = 5;
update t1 set b = 50 where a = 5;
update t1 set a = 50 where a = 5;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t1;
select * from t1 force index(sec_key);
connection master;
delete from t1 where a = 50;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t1;
select * from t1 force index(sec_key);

# rename back to original
connection slave;
set @@sql_log_bin = 0;
alter table t1 change bb b int;
alter table t1 change cc c int;
set @@sql_log_bin = 1;

echo "case 4: resize column on slave";
connection slave;
set @@sql_log_bin = 0;
alter table t1 modify b bigint;
alter table t1 modify c bigint;
set @@sql_log_bin = 1;

connection master;
insert into t1 values(5, 5, 5, 5);
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t1;
select * from t1 force index(sec_key);
connection master;
update t1 set d = 50 where a = 5;
update t1 set c = 50 where a = 5;
update t1 set b = 50 where a = 5;
update t1 set a = 50 where a = 5;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t1;
select * from t1 force index(sec_key);
connection master;
delete from t1 where a = 50;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t1;
select * from t1 force index(sec_key);

# resize back to original
connection slave;
set @@sql_log_bin = 0;
alter table t1 modify b int;
alter table t1 modify c int;
set @@sql_log_bin = 1;

echo "case 5: different col orders";
connection master;
set @@sql_log_bin = 0;
eval create table t2
(a int primary key, b int, c int, key sec_key(c)) engine = $engine;
set @@sql_log_bin = 1;
connection slave;
set @@sql_log_bin = 0;
eval create table t2
(c int, b int, a int primary key, key sec_key(c)) engine = $engine;
set @@sql_log_bin = 1;

connection master;
insert into t2 values(1, 2, 3);
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t2;
select * from t2 force index(sec_key);
connection master;
update t2 set c = 30 where a = 1;
update t2 set b = 20 where a = 1;
update t2 set a = 10 where a = 1;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t2;
select * from t2 force index(sec_key);
connection master;
delete from t2 where a = 10;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t2;
select * from t2 force index(sec_key);

# drop the table
connection master;
drop table t2;
source include/sync_slave_sql_with_master.inc;

echo "case 6: different col orders + different PK";
connection master;
set @@sql_log_bin = 0;
eval create table t2 (a int primary key, b int, c int) engine = $engine;
set @@sql_log_bin = 1;
connection slave;
set @@sql_log_bin = 0;
eval create table t2 (c int primary key, b int, a int) engine = $engine;
set @@sql_log_bin = 1;

connection master;
insert into t2 values(1, 2, 3);
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t2;
connection master;
update t2 set c = 30 where a = 1;
update t2 set b = 20 where a = 1;
update t2 set a = 10 where a = 1;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t2;
connection master;
delete from t2 where a = 10;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t2;

# drop the table
connection master;
drop table t2;
source include/sync_slave_sql_with_master.inc;

echo "case 7: add col in middle on slave";
connection master;
set @@sql_log_bin = 0;
eval create table t2
(a int primary key, b int, d int, key sec_key(b)) engine = $engine;
set @@sql_log_bin = 1;
connection slave;
set @@sql_log_bin = 0;
eval create table t2
(a int primary key, b int, c int, d int, key sec_key(b)) engine = $engine;
set @@sql_log_bin = 1;

connection master;
insert into t2 values(1, 2, 4);
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t2;
select * from t2 force index(sec_key);
connection master;
update t2 set d = 40 where a = 1;
update t2 set b = 20 where a = 1;
update t2 set a = 10 where a = 1;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t2;
select * from t2 force index(sec_key);
connection master;
delete from t2 where a = 10;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t2;
select * from t2 force index(sec_key);

# drop the table
connection master;
drop table t2;
source include/sync_slave_sql_with_master.inc;

echo "case 8: remove col in middle on slave";
connection master;
set @@sql_log_bin = 0;
eval create table t2
(a int primary key, b int, c int, d int, key sec_key(b)) engine = $engine;
set @@sql_log_bin = 1;
connection slave;
set @@sql_log_bin = 0;
eval create table t2
(a int primary key, b int, d int, key sec_key(b)) engine = $engine;
set @@sql_log_bin = 1;

connection master;
insert into t2 values(1, 2, 3, 4);
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t2;
select * from t2 force index(sec_key);
connection master;
update t2 set d = 40 where a = 1;
update t2 set c = 30 where a = 1;
update t2 set b = 20 where a = 1;
update t2 set a = 10 where a = 1;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t2;
select * from t2 force index(sec_key);
connection master;
delete from t2 where a = 10;
source include/sync_slave_sql_with_master.inc;
connection slave;
select * from t2;
select * from t2 force index(sec_key);

# drop the table
connection master;
drop table t2;
source include/sync_slave_sql_with_master.inc;

# cleanup
connection master;
drop table t1;
source include/sync_slave_sql_with_master.inc;
