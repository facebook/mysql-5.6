--echo # Error recovery testing ADD PARTITION
SET SESSION debug="+d,fail_add_partition_1";
--source suite/parts/inc/partition_fail.inc
SET SESSION debug="-d,fail_add_partition_1";
SET SESSION debug="+d,fail_add_partition_2";
--source suite/parts/inc/partition_fail.inc
SET SESSION debug="-d,fail_add_partition_2";
SET SESSION debug="+d,fail_add_partition_3";
--source suite/parts/inc/partition_fail.inc
SET SESSION debug="-d,fail_add_partition_3";
