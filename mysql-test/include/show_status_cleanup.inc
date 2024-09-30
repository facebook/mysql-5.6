# Use in conjunction with:
#
#     include/show_status.inc and 
#     include/show_status_init.inc

# This script cleans up the tmp table created in include/show_status_init.inc 

--disable_query_log

eval drop table $status_tab;

--enable_query_log
