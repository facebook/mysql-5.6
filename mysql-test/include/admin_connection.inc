# This test is to check various cases of connections to the admin tcpip port

# Wait for all the connections from previous tests except
# the default one to be completedly disconnected
let $count_sessions= 1;
--source include/wait_until_count_sessions.inc

# Restart the server with admin_port being set to one more than the server's
# port
--let $admin_port=$MASTER_MYPORT
--inc $admin_port
--let $_mysqld_option=--admin_port=$admin_port
--source include/restart_mysqld_with_option.inc

flush privileges;
--echo ## Create non-super user nosuper
create user nosuper@localhost;
--echo ## Create super user 'super1'
grant all privileges on *.* to 'super1'@'localhost' identified by password '' with grant option;
--echo ## Create super user 'super2'
grant all privileges on *.* to 'super2'@'localhost' identified by password '' with grant option;
flush user_resources;

select user from mysql.user order by 1 asc;
select ERRORS_TOTAL,CONNECTIONS_CONCURRENT,CONNECTIONS_DENIED_MAX_USER,CONNECTIONS_DENIED_MAX_GLOBAL from information_schema.USER_STATISTICS where USER_NAME like 'nosuper';

--echo ## Start a connection fail_admin_con1 at ADMIN_PORT with new user nosuper
--echo ## This will fail since this user doesn't have SUPER ACL
--replace_result $MASTER_MYSOCK MASTER_SOCKET $ADMIN_PORT ADMIN_PORT
--error ER_SPECIFIC_ACCESS_DENIED_ERROR
connect (fail_admin_con1,$IP,nosuper,,mysql,$admin_port);

select ERRORS_TOTAL,CONNECTIONS_CONCURRENT,CONNECTIONS_DENIED_MAX_USER,CONNECTIONS_DENIED_MAX_GLOBAL from information_schema.USER_STATISTICS where USER_NAME like 'nosuper';

--echo ## Grant the admin port privilege to nosuper and check a second time if
--echo ## the user can now successfully connect to the admin port
grant admin port on *.* to 'nosuper'@'localhost' identified by password '';
flush privileges;
connect (success_admin_con2,$IP,nosuper,,,$admin_port);
disconnect success_admin_con2;

--echo ## Start connections nosuper at regular ports
let $i=1;
while($i<=4) {
  connect (noadmin_con_nosuper$i,$IP,nosuper,,);
  inc $i;
}
connection default;
SET @save_max_nonsuper_connections=@@global.max_nonsuper_connections;
SET GLOBAL max_nonsuper_connections=2;

--echo ## This will fail since connecting to regular port
--replace_result $MASTER_MYSOCK MASTER_SOCKET $MASTER_MYPORT MASTER_PORT
--error ER_CON_COUNT_ERROR
connect (noadmin_con_nosuperX,$IP,nosuper,,);
--echo ## This will succeed since connecting to admin port
connect (admin_con_nosuperX,$IP,nosuper,,,$admin_port);
disconnect admin_con_nosuperX;

--echo ## Disconnect nosuper at regular ports
let $i=1;
while($i<=4) {
  disconnect noadmin_con_nosuper$i;
  inc $i;
}
--echo ## Switch to default connection
connection default;
--echo ## Wait for all the connections except the default one to be completedly disconnected
let $count_sessions= 1;
--source include/wait_until_count_sessions.inc

--echo ## Start connections admin_con_root1,2,3 at ADMIN_PORT
--echo ## This will succeed since root has SUPER ACL
let $i=1;
while($i<=3) {
  connect (admin_con_root$i,$IP,root,,mysql,$admin_port);
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
--replace_column 1 - 3 localhost 5 - 6 - 7 - 8 - 9 - 10 - 11 - 12 -
--replace_result init STATE "cleaning up" STATE
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
--replace_column 1 - 3 localhost 5 - 6 - 7 - 8 - 9 - 10 - 11 - 12 -
--replace_result init STATE "cleaning up" STATE
--sorted_result
show processlist;

--echo ## Start a connection fail_non_admin_con9 at master port
--echo ## This will fail due to the limit of the max number of connections
--replace_result $MASTER_MYSOCK MASTER_SOCKET $MASTER_MYPORT MASTER_PORT
--error ER_CON_COUNT_ERROR
connect (fail_non_admin_con_root,$IP,root,,mysql,);

--echo ## Start connections admin_con_root4,5,6,7,8 at ADMIN_PORT
--echo ## This will succeed since admin port is handled separately
--echo ## and it is not limited by either @@max-connections or @@max-user-connections
let $i=4;
while ($i<=8) {
  connect (admin_con_root$i,$IP,root,,mysql,$admin_port);
  inc $i;
}

--echo ## Switch to default connection
connection default;
--echo ## Show processlist: totally 14 connections will be listed: the 5 admin connections, the 8 non-admin connections, and the default one
--replace_column 1 - 3 localhost 5 - 6 - 7 - 8 - 9 - 10 - 11 - 12 -
--replace_result init STATE "cleaning up" STATE
--sorted_result
show processlist;

--echo ## Switch to non_admin_con_super1_1
connection non_admin_con_super1_1;
select user from mysql.user order by 1 asc;

--echo ## The non-super user should be able to bypass the connection limit as
--echo ## well when using the admin port
connect (admin_nonsuper_conn, $IP,nosuper,,,$admin_port);
disconnect admin_nonsuper_conn;

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
SET GLOBAL max_nonsuper_connections=@save_max_nonsuper_connections;
