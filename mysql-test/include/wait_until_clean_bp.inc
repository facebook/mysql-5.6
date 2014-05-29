#
# Waits until the InnoDB buffer pool becomes fully flushed
#
let $wait_condition= SELECT VARIABLE_VALUE = "0" FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME="Innodb_buffer_pool_pages_dirty";
--source include/wait_condition.inc
