create database linkdb;
use linkdb;

eval create table linktable (
  id1 bigint(20) unsigned NOT NULL DEFAULT '0',
  id2 bigint(20) unsigned NOT NULL DEFAULT '0',
  link_type bigint(20) unsigned NOT NULL DEFAULT '0',
  visibility tinyint(3) NOT NULL DEFAULT '0',
  data varchar(255) NOT NULL DEFAULT '',
  time bigint(20) unsigned NOT NULL DEFAULT '0',
  version int(11) unsigned NOT NULL DEFAULT '0',
  primary key (id1,id2,link_type) COMMENT 'cf_link_pk',
  KEY id1_type (id1,link_type,visibility,time,version,data)
  COMMENT 'rev:cf_link_id1_type') ENGINE=$engine DEFAULT COLLATE=latin1_bin;

eval create table counttable (
  id bigint(20) unsigned NOT NULL DEFAULT '0',
  link_type bigint(20) unsigned NOT NULL DEFAULT '0',
  count int(10) unsigned NOT NULL DEFAULT '0',
  time bigint(20) unsigned NOT NULL DEFAULT '0',
  version bigint(20) unsigned NOT NULL DEFAULT '0',
  primary key (id,link_type) COMMENT 'cf_count_pk')
  ENGINE=$engine DEFAULT COLLATE=latin1_bin;

eval create table nodetable (
  id bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  type int(10) unsigned NOT NULL,
  version bigint(20) unsigned NOT NULL,
  time int(10) unsigned NOT NULL,
  data mediumtext NOT NULL,
  primary key(id) COMMENT 'cf_node_pk')
  ENGINE=$engine DEFAULT COLLATE=latin1_bin;

--echo #
--echo # Test nodeGet function
--echo #
--replace_result $NP_EXAMPLE_LIB NP_EXAMPLE_LIB
eval create native procedure nodeGet soname "$NP_EXAMPLE_LIB";
--disable_query_log
let $i = 100;
while($i)
{
  eval insert into nodetable values ($i, 1, 1, $i * 1000, "data");
  dec $i;
}
--enable_query_log

%nodeGet 1;
%nodeGet 50 anything can go here;
%nodeGet 39;
%nodeGet 98;
%nodeGet 1000;
--error ER_NP_FAILED
%nodeGet -1;

--error ER_NP_FAILED
%nodeGet asdf;

--echo #
--echo # Test linkGetRange/linkGetId2s function
--echo #
--replace_result $NP_EXAMPLE_LIB NP_EXAMPLE_LIB
eval create native procedure linkGetRange soname "$NP_EXAMPLE_LIB";
--replace_result $NP_EXAMPLE_LIB NP_EXAMPLE_LIB
eval create native procedure linkGetId2s soname "$NP_EXAMPLE_LIB";
--disable_query_log
let $i = 5;
while ($i)
{
  eval insert into linktable values (1, $i, 1, 1, "data", 1000 + $i, 1);
  eval insert into linktable values (1, $i, 2, 1, "data", 2000 + $i, 1);
  eval insert into linktable values (1, $i, 3, 1, "data", 3000 + $i, 1);
  eval insert into linktable values (1, $i, 4, 0, "data", 4000 + $i, 1);
  eval insert into linktable values (1, $i, 5, 1, "data", 5000 + $i, 1);
  dec $i;
}
--enable_query_log

%linkGetRange 1 1 1000 2000 0 1000;
%linkGetRange 1 2 1000 2000 0 1000;
%linkGetRange 1 1 5000 2000 0 1000;

%linkGetRange 1 2 1000 6000 0 5;
%linkGetRange 1 2 1000 6000 0 2;
%linkGetRange 1 2 1000 6000 2 2;

%linkGetId2s 1 3 3 1 2 3;
%linkGetId2s 1 3 3 3 2 1;
%linkGetId2s 1 3 3 3 2 10;
%linkGetId2s 1 3 3 3 2 1 asdf;
%linkGetId2s 1 3 0;
--error ER_NP_WRONG_ARGUMENTS
%linkGetId2s 1 3 4 2;

--echo #
--echo # Test rangeQuery function
--echo #
--replace_result $NP_EXAMPLE_LIB NP_EXAMPLE_LIB
eval create native procedure rangeQuery soname "$NP_EXAMPLE_LIB";
%rangeQuery 1 0 0 4 id1 1 link_type 1 visibility 1 time 1001 4 id1 1 link_type 1 visibility 1 time 1005;
%rangeQuery 1 0 1 4 id1 1 link_type 1 visibility 1 time 1001 4 id1 1 link_type 1 visibility 1 time 1005;
%rangeQuery 1 1 0 4 id1 1 link_type 1 visibility 1 time 1001 4 id1 1 link_type 1 visibility 1 time 1005;
%rangeQuery 1 1 1 4 id1 1 link_type 1 visibility 1 time 1001 4 id1 1 link_type 1 visibility 1 time 1005;
%rangeQuery 0 0 0 4 id1 1 link_type 1 visibility 1 time 1001 4 id1 1 link_type 1 visibility 1 time 1005;
%rangeQuery 0 0 1 4 id1 1 link_type 1 visibility 1 time 1001 4 id1 1 link_type 1 visibility 1 time 1005;
%rangeQuery 0 1 0 4 id1 1 link_type 1 visibility 1 time 1001 4 id1 1 link_type 1 visibility 1 time 1005;
%rangeQuery 0 1 1 4 id1 1 link_type 1 visibility 1 time 1001 4 id1 1 link_type 1 visibility 1 time 1005;

%rangeQuery 1 0 0 2 id1 1 link_type 1 2 id1 1 link_type 2;
%rangeQuery 1 0 1 2 id1 1 link_type 1 2 id1 1 link_type 2;
%rangeQuery 1 1 0 2 id1 1 link_type 1 2 id1 1 link_type 2;
%rangeQuery 1 1 1 2 id1 1 link_type 1 2 id1 1 link_type 2;
%rangeQuery 0 0 0 2 id1 1 link_type 1 2 id1 1 link_type 2;
%rangeQuery 0 0 1 2 id1 1 link_type 1 2 id1 1 link_type 2;
%rangeQuery 0 1 0 2 id1 1 link_type 1 2 id1 1 link_type 2;
%rangeQuery 0 1 1 2 id1 1 link_type 1 2 id1 1 link_type 2;

%rangeQuery 0 0 0 2 id1 1 link_type 1 4 id1 1 link_type 2 visibility 1 time 2004;
%rangeQuery 0 0 1 2 id1 1 link_type 1 4 id1 1 link_type 2 visibility 1 time 2004;
%rangeQuery 0 1 0 2 id1 1 link_type 1 4 id1 1 link_type 2 visibility 1 time 2004;
%rangeQuery 0 1 1 2 id1 1 link_type 1 4 id1 1 link_type 2 visibility 1 time 2004;

--echo #
--echo # Test countGet function
--echo #
--replace_result $NP_EXAMPLE_LIB NP_EXAMPLE_LIB
eval create native procedure countGet soname "$NP_EXAMPLE_LIB";
--disable_query_log
let $i = 10;
while ($i)
{
  eval insert into counttable values ($i, 1, $i * 2, $i * 1000, 1);
  dec $i;
}
--enable_query_log

%countGet 1 1;
%countGet 10 1;
%countGet 111 1;
%countGet 1 111;
--error ER_NP_FAILED
%countGet -1 1 1;
--error ER_NP_FAILED
%countGet -1 1 2;
--error ER_NP_WRONG_ARGUMENTS
%countGet;

--echo #
--echo # Check that DDL locks are respected.
--echo #
--replace_result $NP_EXAMPLE_LIB NP_EXAMPLE_LIB
eval create native procedure sleepRange soname "$NP_EXAMPLE_LIB";
connect(con1,localhost,root,,);
connection con1;
send %sleepRange 1;
connection default;
sleep 3;
set @start_lock_wait_timeout = @@session.lock_wait_timeout;
set lock_wait_timeout = 1;
--error ER_LOCK_WAIT_TIMEOUT
drop table counttable;
set lock_wait_timeout = @start_lock_wait_timeout;
connection con1;
reap;
connection default;
disconnect con1;

--echo #
--echo # Check that queries can be killed.
--echo #
connect(con1,localhost,root,,);
connection con1;
let $id= `select connection_id()`;
send %sleepRange 0;
connection default;
let $wait_condition =
  select count(*) = 1 from information_schema.processlist
  where info = "sleepRange 0";
--source include/wait_condition.inc
--replace_result $id ID
eval kill query $id;
connection con1;
--error ER_QUERY_INTERRUPTED
reap;
connection default;
disconnect con1;

--echo #
--echo # Check that native procedures work properly with transactions.
--echo #
connect(con1,localhost,root,,);
connection con1;
use linkdb;
set session transaction isolation level repeatable read;
%countGet 1 1;
begin;
select count from counttable where id = 1 and link_type = 1;
%countGet 1 1;
connection default;
--echo # Before update
%countGet 1 1;
update counttable set count = count + 1 where id = 1 and link_type = 1;
--echo # After update
%countGet 1 1;
connection con1;
--echo # Unchanged due to consistent reads
%countGet 1 1;
connection default;
disconnect con1;

--echo #
--echo # Check index reads on prefixed data.
--echo #
alter table linktable drop index id1_type;
alter table linktable
add index id1_type (id1,link_type,visibility,time,version,data(1))
COMMENT 'rev:cf_link_id1_type';
%linkGetRange 1 1 1000 2000 0 1000;

--echo #
--echo # Check correct error handling for various scenarios.
--echo #
--replace_result $NP_EXAMPLE_LIB NP_EXAMPLE_LIB
eval create native procedure invalidKey1 soname "$NP_EXAMPLE_LIB";
--error ER_NP_FAILED
%invalidKey1;
--replace_result $NP_EXAMPLE_LIB NP_EXAMPLE_LIB
eval create native procedure invalidOpen1 soname "$NP_EXAMPLE_LIB";
--error ER_NP_FAILED
%invalidOpen1;
--replace_result $NP_EXAMPLE_LIB NP_EXAMPLE_LIB
eval create native procedure invalidOpen2 soname "$NP_EXAMPLE_LIB";
--error ER_NP_FAILED
%invalidOpen2;
--replace_result $NP_EXAMPLE_LIB NP_EXAMPLE_LIB
eval create native procedure invalidOpen3 soname "$NP_EXAMPLE_LIB";
--error ER_NP_FAILED
%invalidOpen3;
--replace_result $NP_EXAMPLE_LIB NP_EXAMPLE_LIB
eval create native procedure invalidOpen4 soname "$NP_EXAMPLE_LIB";
--error ER_NP_FAILED
%invalidOpen4;
--error ER_NP_UNKNOWN_PROCEDURE
%invalidProcedure;
--replace_regex /(Can\'t open shared library).*$/\1/
--error ER_CANT_OPEN_LIBRARY
eval create native procedure invalidProcedure soname "invalid.so";
--replace_result $NP_EXAMPLE_LIB NP_EXAMPLE_LIB
--error ER_CANT_FIND_DL_ENTRY
eval create native procedure invalidProcedure soname "$NP_EXAMPLE_LIB";

--echo #
--echo # Check that our functions are reloaded after restart.
--echo #
--source include/restart_mysqld.inc
select * from mysql.native_proc order by name;

drop native procedure nodeGet;
--replace_result $NP_EXAMPLE_LIB NP_EXAMPLE_LIB
--error ER_NP_PROCEDURE_EXISTS
eval create native procedure nodeGet soname "$NP_EXAMPLE_LIB";
drop native procedure linkGetRange;
drop native procedure linkGetId2s;
drop native procedure countGet;
drop native procedure sleepRange;
drop native procedure rangeQuery;
drop native procedure invalidKey1;
drop native procedure invalidOpen1;
drop native procedure invalidOpen2;
drop native procedure invalidOpen3;
drop native procedure invalidOpen4;

--error ER_NP_UNKNOWN_PROCEDURE
%nodeGet 1;

--echo #
--echo # Check that our functions are unloaded after restart.
--echo #
--source include/restart_mysqld.inc
select * from mysql.native_proc order by name;

--error ER_NP_UNKNOWN_PROCEDURE
%nodeGet 1;

drop database linkdb;
