# ==== Purpose ====
#
# Initialize two servers for the purpose of taking backup
# from one to the other

--let $rpl_server_count= 2
--let $rpl_topology= none
--source include/rpl_init.inc
--source include/rpl_default_connections.inc
--let $page_size = query_get_value(select @@innodb_page_size, @@innodb_page_size, 1)
--exec suite/xtrabackup/include/xb_set_innodb_page_size.sh $page_size 2>&1
