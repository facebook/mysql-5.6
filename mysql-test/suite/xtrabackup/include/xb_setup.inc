# ==== Purpose ====
#
# Initialize two servers for the purpose of taking backup
# from one to the other

--let $include_filename= xb_setup.inc
--source include/begin_include_file.inc

--let $rpl_server_count= 2
--let $rpl_topology= none
--source include/rpl_init.inc
--source include/rpl_default_connections.inc

--let $include_filename= xb_setup.inc
--source include/end_include_file.inc
