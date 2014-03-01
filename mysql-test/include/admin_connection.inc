# This test is to check various cases of connections
# to the admin tcpip port

# Wait for all the connections from previous tests except
# the default one to be completedly disconnected
let $count_sessions= 1;
--source include/wait_until_count_sessions.inc

--echo ## Start a connection con_new_user to UNIX socket ##
connect (con_new_user,localhost,root,,mysql);

--echo ## Create user nosuper
create user nosuper@localhost;
select user from mysql.user;

--echo ## Start a connection fail_admin_con at 14000 with new user nosuper ##
--echo ## This will fail since this user doesn't have SUPER ACL ##
--replace_result $MASTER_MYSOCK MASTER_SOCKET
--error ER_SPECIFIC_ACCESS_DENIED_ERROR
connect (fail_admin_con,$IP,nosuper,,mysql,14000);

--echo ## Switch to con_new_user ##
connection con_new_user;

--echo ## Drop user nosuper
drop user nosuper@localhost;
select user from mysql.user;

--echo ## Disconnect con_new_user ##
disconnect con_new_user;

--echo ## Start a connection admin_con at 14000 ##
--echo ## This will succeed since root has SUPER ACL ##
connect (admin_con,$IP,root,,mysql,14000);
select user from mysql.user;

--echo ## Disconnect admin_con ##
disconnect admin_con;

--echo ## Switch to default connection
connection default;
--echo ## Wait for all the connections except the default one to be completedly disconnected
let $count_sessions= 1;
--source include/wait_until_count_sessions.inc

--echo ## Switch to default connection
connection default;
--echo ## Show processlist
--replace_column 1 - 3 localhost 5 - 6 - 7 - 8 - 9 - 10 - 11 -
--replace_result init STATE "cleaning up" STATE
--sorted_result
show processlist;

--echo ## Start connections non_admin_con1, non_admin_con2, ... non_admin_con7 at master port ##
--echo ## All these will succeed ##
connect (non_admin_con1,$IP,root,,mysql,);
connect (non_admin_con2,$IP,root,,mysql,);
connect (non_admin_con3,$IP,root,,mysql,);
connect (non_admin_con4,$IP,root,,mysql,);
connect (non_admin_con5,$IP,root,,mysql,);
connect (non_admin_con6,$IP,root,,mysql,);
connect (non_admin_con7,$IP,root,,mysql,);

--echo ## Switch to default connection
connection default;
--echo ## Show processlist
--replace_column 1 - 3 localhost 5 - 6 - 7 - 8 - 9 - 10 - 11 -
--replace_result init STATE "cleaning up" STATE
--sorted_result
show processlist;

--echo ## Start connections non_admin_con8 at master port ##
--echo ## This will succeed ##
connect (non_admin_con8,$IP,root,,mysql,);

--echo ## Switch to default connection
connection default;
--echo ## Show processlist
--replace_column 1 - 3 localhost 5 - 6 - 7 - 8 - 9 - 10 - 11 -
--replace_result init STATE "cleaning up" STATE
--sorted_result
show processlist;

--echo ## Start a connection fail_non_admin_con9 at master port ##
--echo ## This will fail due to the limit of the max number of connections ##
--replace_result $MASTER_MYSOCK MASTER_SOCKET $MASTER_MYPORT MASTER_PORT
--error ER_CON_COUNT_ERROR
connect (fail_non_admin_con9,$IP,root,,mysql,);

--echo ## Start a connection admin_con2 at 14000 ##
--echo ## This will succeed since admin port is handled separately ##
connect (admin_con2,$IP,root,,mysql,14000);

--echo ## Switch to non_admin_con1 ##
connection non_admin_con1;
select user from mysql.user;

--echo ## Switch to admin_con2 ##
connection admin_con2;
select user from mysql.user;

--echo ## Disconnect admin_con2 ##
disconnect admin_con2;

--echo ## Disconnect non_admin_con1, non_admin_con2, ... non_admin_con8 ##
disconnect non_admin_con1;
disconnect non_admin_con2;
disconnect non_admin_con3;
disconnect non_admin_con4;
disconnect non_admin_con5;
disconnect non_admin_con6;
disconnect non_admin_con7;
disconnect non_admin_con8;

--echo ## Switch to default connection
connection default;
