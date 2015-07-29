--echo # continue previous transaction
--echo # read_only was turned on in the middle of a transaction
--echo # new update/insert statement will be blocked immediately
update t1 set a = a + 1;
# multiple table updates
update t1, t2 set t1.a=t1.a+1, t2.a=t2.a*2;
select 't1', a from t1;
select 't2', a from t2;

--echo # write transaction was rolled back at the end
--error ER_DB_READ_ONLY
commit;

# insert/update rolled back
select 't1', a from t1;
select 't2', a from t2;

--echo # write transaction with 'begin'
begin;
insert into t1 values (4), (5), (6);
update t1 set a = a + 1;
# creating a table DDL is failed immdiately
--error ER_DB_READ_ONLY
create table t3 (a int) engine=innodb;
select a from t1;
--error ER_DB_READ_ONLY
commit;

--echo # read-only transactions are ok
begin;
select count(*) from t1;
select count(*) from t2;
commit;

--echo # transaction without 'begin'
insert into t1 values (4), (5), (6);
insert into t1 values (7), (8), (9);
select a from t1;
--error ER_DB_READ_ONLY
commit;
select a from t1;

--echo # rolled-back transaction
insert into t1 values (4), (5), (6);
insert into t1 values (7), (8), (9);
select a from t1;
rollback;
select a from t1;

set autocommit = 1;
--echo # multiple table updates (autocommit)
--error ER_DB_READ_ONLY
update t1, t2 set t1.a=t1.a+1, t2.a=t2.a*2;
select 't1', a from t1;
select 't2', a from t2;
set autocommit = 0;

# table update DDL is blocked
--error ER_DB_READ_ONLY
alter table t1 add key (a);

# table update DDL is blocked
--error ER_DB_READ_ONLY
create index a on t1 (a);

# drop table is not allowed
--error ER_DB_READ_ONLY
drop table t1;

# drop database is not allowed
--error ER_DB_READ_ONLY
drop database test;

--echo #
--echo # OK to create temporary table
--echo #
create temporary table temp1 (a int);
insert into temp1 select * from t1;
update temp1 set a = a + 1;
select * from temp1;
drop temporary table temp1;

--echo #
--echo # OK to switch and write another database
--echo # read_only scope is per database
--echo #
create database test2;
use test2;
show create database test2;
create table t1 (a int) engine = innodb;
insert into t1 values (0), (1), (2);
update t1 set a = a + 1;
select a from t1;

--echo #
--echo # cross-db/noncurrent-db transaction
--echo # Transaction writing to test db from session of test2 db
--echo #
begin;
insert into test.t1 values (4), (5), (6);
update test.t1 set a = a + 1;
select a from test.t1;
--error ER_DB_READ_ONLY
commit;
select a from test.t1;
select a from test2.t1;

begin;
insert into test.t1 values (4), (5), (6);
update test.t1 set a = a + 1;
select a from test.t1;
update test2.t1 set a = a + 1;
select a from test2.t1;
--error ER_DB_READ_ONLY
commit;
select a from test.t1;
select a from test2.t1;

use test;
drop database test2;
