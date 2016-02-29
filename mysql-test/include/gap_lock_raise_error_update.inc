
## single-table insert,update,delete
set session autocommit=0;
insert into t1 (id1, id2, id3) values (-1,-1,-1);
insert into t1 (id1, id2, id3) values (-1,-1,-1)
  on duplicate key update value=100;
--error ER_UNKNOWN_ERROR
update t1 set value=100 where id1=1;
update t1 set value=100 where id1=1 and id2=1 and id3=1;
--error ER_UNKNOWN_ERROR
delete from t1 where id1=2;
delete from t1 where id1=-1 and id2=-1 and id3=-1;
commit;

# autocommit doesn't prevent single table operations
set session autocommit=1;
insert into t1 (id1, id2, id3) values (-1,-1,-1);
insert into t1 (id1, id2, id3) values (-1,-1,-1)
  on duplicate key update value=100;
update t1 set value=100 where id1=1;
update t1 set value=100 where id1=1 and id2=1 and id3=1;
delete from t1 where id1=2;
delete from t1 where id1=-1 and id2=-1 and id3=-1;
commit;


## multi-table statements (preventing all gap locks with autocommit)
set session autocommit=1;
# insert into select
--error ER_UNKNOWN_ERROR
insert into t2 select * from t1;
--error ER_UNKNOWN_ERROR
insert into t2 select * from t1 where id1=1;
insert into t2 select * from t1 where id1=1 and id2=1 and id3=1;

# create table select
create table t4 select * from t1 where id1=1 and id2=1 and id3=1;
drop table t4;
--error ER_UNKNOWN_ERROR
create table t4 select * from t1;
--error ER_UNKNOWN_ERROR
create table t4 select * from t1 where id1=1;

# update join
update t1 join t2 on t1.id1 and t1.id2=t2.id2 set t1.value=100 where t2.id1=3
  and t2.id2=3 and t2.id3=3;
--error ER_UNKNOWN_ERROR
update t1 join t2 on t1.id1 and t1.id2=t2.id2 set t1.value=100 where t2.id1=3;
--error ER_UNKNOWN_ERROR
update t1 join t2 on t1.id1 and t1.id2=t2.id2 join t3 on t1.id1=t3.id
  set t1.value=100 where t2.id1=3;
--error ER_UNKNOWN_ERROR
update t1 set t1.value= (select count(*) from t2);

# delete join
delete t1 from t1 join t2 on t1.id1 and t1.id2=t2.id2 where t2.id1=3
  and t2.id2=3 and t2.id3=3;
--error ER_UNKNOWN_ERROR
delete t1 from t1 join t2 on t1.id1 and t1.id2=t2.id2 where t2.id1=3;

# select join / self join
--error ER_UNKNOWN_ERROR
select * from t1, t2 limit 1 for update;
--error ER_UNKNOWN_ERROR
select * from t1 a, t1 b limit 1 for update;

