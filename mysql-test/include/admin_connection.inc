# This test is to check various cases of connections to the admin tcpip port

# Wait for all the connections from previous tests except
# the default one to be completedly disconnected
let $count_sessions= 1;
--source include/wait_until_count_sessions.inc

flush privileges;
--echo ## Create non-super user nosuper
create user nosuper@localhost;
--echo ## Create super user 'super1'
grant all privileges on *.* to 'super1'@'localhost' identified by password '' with grant option;
--echo ## Create super user 'super2'
grant all privileges on *.* to 'super2'@'localhost' identified by password '' with grant option;
flush user_resources;

select user from mysql.user order by 1 asc;

--echo ## Start a connection fail_admin_con1 at 14000 with new user nosuper
--echo ## This will fail since this user doesn't have SUPER ACL
--replace_result $MASTER_MYSOCK MASTER_SOCKET
--error ER_SPECIFIC_ACCESS_DENIED_ERROR
connect (fail_admin_con1,$IP,nosuper,,mysql,14000);

--echo ## Start connections admin_con_root1,2,3 at 14000
--echo ## This will succeed since root has SUPER ACL
let $i=1;
while($i<=3) {
  connect (admin_con_root$i,$IP,root,,mysql,14000);
  inc $i;
}

let $i=1;
while($i<=3) {
  --echo ## Switch to admin_con_root$i
  connection admin_con_root$i;
  select user from mysql.user order by 1 asc;
  inc $i;
}

--echo ## Disconnect admin_con_root1,2,3
let $i=1;
while($i<=3) {
  disconnect admin_con_root$i;
  inc $i;
}

--echo ## Switch to default connection
connection default;
--echo ## Wait for all the connections except the default one to be completedly disconnected
let $count_sessions= 1;
--source include/wait_until_count_sessions.inc

--echo ## Switch to default connection
connection default;
--echo ## Show processlist: only the default connection will be listed
--replace_column 1 - 3 localhost 5 - 6 - 7 - 8 - 9 - 10 -
--sorted_result
show processlist;

--echo ## Start connections non_admin_con_super1_1,2,3,4
let $i=1;
while ($i<=4) {
  connect (non_admin_con_super1_$i,$IP,super1,,mysql,);
  inc $i;
}

--echo ## Connections non_admin_con_super1_5 will fail due to too many connections for a user
--replace_result $MASTER_MYSOCK MASTER_SOCKET $MASTER_MYPORT MASTER_PORT
--error ER_TOO_MANY_USER_CONNECTIONS
connect (fail_non_admin_con_super1,$IP,super1,,mysql,);

--echo ## Start connections non_admin_con_super2_1,2,3,4
let $i=1;
while ($i<=4) {
  connect (non_admin_con_super2_$i,$IP,super2,,mysql,);
  inc $i;
}

--echo ## Switch to default connection
connection default;
--echo ## Show processlist: totally 9 connections will be listed: the 8 non-admin connections and the default one
--replace_column 1 - 3 localhost 5 - 6 - 7 - 8 - 9 - 10 -
--sorted_result
show processlist;

--echo ## Start a connection fail_non_admin_con9 at master port
--echo ## This will fail due to the limit of the max number of connections
--replace_result $MASTER_MYSOCK MASTER_SOCKET $MASTER_MYPORT MASTER_PORT
--error ER_CON_COUNT_ERROR
connect (fail_non_admin_con_root,$IP,root,,mysql,);

--echo ## Start connections admin_con_root4,5,6,7,8 at 14000
--echo ## This will succeed since admin port is handled separately
--echo ## and it is not limited by either @@max-connections or @@max-user-connections
let $i=4;
while ($i<=8) {
  connect (admin_con_root$i,$IP,root,,mysql,14000);
  inc $i;
}

--echo ## Switch to default connection
connection default;
--echo ## Show processlist: totally 14 connections will be listed: the 5 admin connections, the 8 non-admin connections, and the default one
--replace_column 1 - 3 localhost 5 - 6 - 7 - 8 - 9 - 10 -
--sorted_result
show processlist;

--echo ## Switch to non_admin_con_super1_1
connection non_admin_con_super1_1;
select user from mysql.user order by 1 asc;

let $i=4;
while ($i<=8) {
  --echo ## Switch to admin_con_root$i
  connection admin_con_root$i;
  select user from mysql.user order by 1 asc;
  inc $i;
}

--echo ## Switch to admin_con_root4
connection admin_con_root4;

--echo ## Drop user nosuper, super1, and super2
drop user nosuper@localhost;
drop user super1@localhost;
drop user super2@localhost;
select user from mysql.user order by 1 asc;

--echo ## Disconnect admin_con_root4,5,6,7,8
let $i=4;
while ($i<=8) {
  disconnect admin_con_root$i;
  inc $i;
}

--echo ## Disconnect non_admin_con_super1_1,2,3,4
let $i=1;
while ($i<=4) {
  disconnect non_admin_con_super1_$i;
  inc $i;
}

--echo ## Disconnect non_admin_con_super2_1,2,3,4
let $i=1;
while ($i<=4) {
  disconnect non_admin_con_super2_$i;
  inc $i;
}

--echo ## Switch to default connection
connection default;
