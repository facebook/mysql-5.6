connection server_1;
let $master_port = `select @@port`;

connection server_2;
let $slave_socket = `select @@socket`;

--exec suite/xtrabackup/include/xb_setup_replication.sh $slave_socket $master_port

connection server_1;

let $slave_sync_timeout = 1800;
source include/wait_for_slave_to_sync_with_master.inc;
