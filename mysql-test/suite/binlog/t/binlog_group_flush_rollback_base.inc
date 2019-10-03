# clean slate
reset master;

# setup
set global binlog_error_action = rollback_trx;
create table t1(a int primary key, b char(8)) engine=innodb;
connect(con1,localhost,root,,);
connect(con2,localhost,root,,);

# conn1 is the session that becomes leader of the group. conn2 becomes the
# follower. Both connections simulate flush error by setting
# fail_binlog_flush_raft
# trxs in both sessions will be rolled back implicitly. gtid-executed
# will be left as-is
--connection con1
set session debug="+d,fail_binlog_flush_raft";
set session debug="+d,become_group_leader";
--send insert into t1 values (1, 'a');

--connection con2
set session debug="+d,fail_binlog_flush_raft";
set session debug="+d,become_group_follower";
--send insert into t1 values (2, 'b');

--connection con1
--error ER_ERROR_DURING_COMMIT
--reap
--connection con2
--error ER_ERROR_DURING_COMMIT
--reap

# Cleanup
--disconnect con1
--disconnect con2

--connection default
# Verify that the trx was rolled back
select * from t1;
--source include/show_binlog_events.inc

set global binlog_error_action = default;
drop table t1;
