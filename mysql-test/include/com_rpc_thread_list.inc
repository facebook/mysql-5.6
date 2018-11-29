#
# Test read_only not being respected on detached sessions
#

create database testdb;
use testdb;
create table x (id int primary key) engine = innodb;

connect (con1,localhost,root,,);
if ($stateful)
{
  # Create a detached session by running set session.
  query_attrs_add rpc_role root;
  query_attrs_add rpc_db testdb;
  set session wait_timeout=5;
  let $rpc_id = get_rpc_id();
  # Check that rpc_id is returned in response.
  if ($rpc_id == "") {
    echo Fail: Did not find rpc_id after BEGIN transaction.;
  }
  query_attrs_delete rpc_role;
  query_attrs_delete rpc_db;
  query_attrs_add rpc_id $rpc_id;
}
if (!$stateful)
{
  query_attrs_add rpc_role root;
  query_attrs_add rpc_db testdb;
}


# This will initialize the local db_read_only_hash on the session THD
INSERT INTO x VALUES(5);

connection default;
alter database testdb super_read_only = true;

connection con1;
# This should fail.
--error ER_DB_READ_ONLY
INSERT INTO x VALUES (20);

connection default;
if ($stateful)
{
  --disable_query_log
  eval KILL $rpc_id;
  --enable_query_log
}
alter database testdb read_only = false;
drop table x;

#
# Test segfault on dangling pointer for read_only
#
create table x (id int primary key) engine = innodb;
create database db_default;
alter database db_default read_only = true;
disconnect con1;

connect (con1,localhost,root,,);
if ($stateful)
{
  # Create a detached session by running set session.
  query_attrs_add rpc_role root;
  query_attrs_add rpc_db testdb;
  set session wait_timeout=5;
  let $rpc_id = get_rpc_id();
  # Check that rpc_id is returned in response.
  if ($rpc_id == "") {
    echo Fail: Did not find rpc_id after BEGIN transaction.;
  }
  query_attrs_delete rpc_role;
  query_attrs_delete rpc_db;
  query_attrs_add rpc_id $rpc_id;
}
if (!$stateful)
{
  query_attrs_add rpc_role root;
  query_attrs_add rpc_db testdb;
}

# This will initialize the local db_read_only_hash on the session THD
INSERT INTO x VALUES(5);

connection default;
# This should remove the db from the db_read_only_hash on all THDs
drop database db_default;

connection con1;
# This would segfault if the session THD contained dangling pointers
INSERT INTO x VALUES (20);

connection default;
disconnect con1;

if ($stateful)
{
  --disable_query_log
  eval KILL $rpc_id;
  --enable_query_log
}
drop table x;
use test;
drop database testdb;
