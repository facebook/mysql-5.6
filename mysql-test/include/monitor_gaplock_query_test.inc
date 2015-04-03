--echo "monitor_gaplock_query_test()"

connect(conn1,127.0.0.1,root);
#
# test case: test that non-gap locks are not logged
#
set transaction isolation level repeatable read;
start transaction;
select "test non gap lock select, insert and update";
select * from gaplocktest;
insert into gaplocktest values(101, '101');
update gaplocktest set value='updated 0' where id=0;
rollback;

#
# test case: make sure select for update gets logged
#
set transaction isolation level repeatable read;
start transaction;
# explicit lock [-inf, 25)
select * from gaplocktest where id < 25 for update; # <== gap lock
# explicit lock [25, 50)
select * from gaplocktest where id < 50 lock in share mode; # <== gap lock
# implicit lock on 100
select * from gaplocktest where id = 100 for update;
# explicit lock (75, 99]
select * from gaplocktest where id = 99 for update; # <== gap lock
# implicit lock on 75
update gaplocktest set value = '7575' where id = 75;

#
# test case: make sure that gap lock happens "only" for the required insert
# stmt
#
connect(conn2,127.0.0.1,root);
set transaction isolation level repeatable read;
start transaction;
set innodb_lock_wait_timeout=1;
# implicit lock on 51
insert into gaplocktest values (51, '51');
# any update/delete on 75 should fail since 75 is implicitly locked
--error ER_LOCK_WAIT_TIMEOUT
update gaplocktest set value = '7575' where id = 75;
--error ER_LOCK_WAIT_TIMEOUT
delete from gaplocktest where id = 75;
rollback;

start transaction;
# a gap lock is inherited during insert
--error ER_LOCK_WAIT_TIMEOUT
insert into gaplocktest values (1, '1'); # <== gap lock
--error ER_LOCK_WAIT_TIMEOUT
update gaplocktest set value = '2525' where id = 25;
--error ER_LOCK_WAIT_TIMEOUT
update gaplocktest set value = '7575' where id = 75;
# If the element is not found then a gap lock is obtained
update gaplocktest set value = '2626' where id = 26; # <== gap lock
--error ER_LOCK_WAIT_TIMEOUT
delete from gaplocktest where id = 25;
rollback;

--error ER_LOCK_WAIT_TIMEOUT
insert into gaplocktest values (1, '1'); # <== gap lock
--error ER_LOCK_WAIT_TIMEOUT
update gaplocktest set value = '2525' where id = 25;
--error ER_LOCK_WAIT_TIMEOUT
delete from gaplocktest where id = 25;

#
# test case: make sure select for updates on intersecting and non intersecting
# gaps are logged
#
start transaction;
# explicit lock [100, inf)
select * from gaplocktest where id > 100 for update; # <== gap lock
--error ER_LOCK_WAIT_TIMEOUT
select * from gaplocktest where id > 40 for update; # <== gap lock
rollback;

disconnect conn2;
--connection conn1
rollback;
disconnect conn1;
