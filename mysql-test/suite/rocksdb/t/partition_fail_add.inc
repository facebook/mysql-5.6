--echo # Error recovery testing ADD PARTITION
SET SESSION debug="+d,fail_add_partition_2";
--source partition_fail.inc
SET SESSION debug="-d,fail_add_partition_2";
SET SESSION debug="+d,fail_add_partition_3";
--source partition_fail.inc
SET SESSION debug="-d,fail_add_partition_3";
