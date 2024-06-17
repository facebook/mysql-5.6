use test
show engines;
drop table if exists t1;
create table t1 (c1 int, c2 varchar(50), primary key (c1)) engine=rocksdb;
show create table t1;
desc t1;
insert into t1 values (1, 'Hello');
insert into t1 values (2, 'World');
select * from t1;

