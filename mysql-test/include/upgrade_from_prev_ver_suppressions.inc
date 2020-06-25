--disable_query_log
call mtr.add_suppression("Storage engine '.*' does not support system tables. \\[mysql.*\\]");
call mtr.add_suppression("Table 'mysql.component' doesn't exist");
call mtr.add_suppression("is expected to be transactional");
call mtr.add_suppression("table is missing or has an incorrect definition");
call mtr.add_suppression("ACL DDLs will not work unless mysql_upgrade is executed");
call mtr.add_suppression("Column count of mysql.* is wrong");
call mtr.add_suppression(".*Missing system table mysql.global_grants.*");
call mtr.add_suppression("ACL table mysql.[a-z_]* missing. Some operations may fail.");
call mtr.add_suppression("Error in checking mysql.slave_master_info repository info type of TABLE");
call mtr.add_suppression("Error creating master info: Error checking repositories.");
call mtr.add_suppression("Slave: Failed to initialize the master info structure for channel");
call mtr.add_suppression("Failed to create or recover replication info repositories.");
call mtr.add_suppression(".* table is not ready to be used. Table '.*' cannot be opened.");
--enable_query_log
