# replace $uuid with "uuid"

--let $include_filename= raft_show_binary_or_raft_logs_with_opid.inc
--source include/begin_include_file.inc

replace_column 2 #;
show raft logs with opid;
replace_result $uuid uuid; replace_column 2 #;
show raft logs with gtid opid;
replace_column 2 #;
show binary logs with opid;
replace_result $uuid uuid; replace_column 2 #;
show binary logs with gtid opid;

--source include/end_include_file.inc
