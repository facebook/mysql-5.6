truncate t1;
truncate t2;

begin;
insert into t1 values (0), (1), (2);
insert into t2 values (0), (1), (2);
update t1 set a = a + 1;
# multiple table updates
update t1, t2 set t1.a=t1.a+1, t2.a=t2.a*2;
select 't1', a from t1;
select 't2', a from t2;
commit;

alter table t1 add key (a);
describe t1;
drop index a on t1;

alter table t2 add key (a);
describe t2;
drop index a on t2;

truncate t1;
truncate t2;

--echo # populate some data
begin;
insert into t1 values (1), (2), (3);
insert into t2 values (1), (2), (3);
select 't1', a from t1;
select 't2', a from t2;
commit;
