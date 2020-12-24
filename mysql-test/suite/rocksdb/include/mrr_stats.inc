# Run query and output mrr stats data
# Parameters:
#   mrr_query: mrr query to eval


--echo # show explain
--replace_column 10 # 11 #
--eval explain $mrr_query

--echo # save old counter
DROP TEMPORARY TABLE IF EXISTS mrr_stats_t1, mrr_stats_t2;
CREATE TEMPORARY TABLE mrr_stats_t1 as
SELECT * FROM performance_schema.global_status
WHERE variable_name in (
  'ROCKSDB_ROWS_READ', 'ROCKSDB_NUMBER_DB_NEXT', 'ROCKSDB_NUMBER_DB_NEXT_FOUND',
  'ROCKSDB_NUMBER_DB_SEEK', 'ROCKSDB_NUMBER_DB_SEEK_FOUND',
  'ROCKSDB_NUMBER_MULTIGET_BYTES_READ', 'ROCKSDB_NUMBER_MULTIGET_GET',
  'ROCKSDB_NUMBER_MULTIGET_KEYS_READ'
);
flush status; # for Handler_mrr_init

--eval $mrr_query

CREATE TEMPORARY TABLE mrr_stats_t2 as
SELECT * from performance_schema.global_status
WHERE variable_name in (
  'ROCKSDB_ROWS_READ', 'ROCKSDB_NUMBER_DB_NEXT', 'ROCKSDB_NUMBER_DB_NEXT_FOUND',
  'ROCKSDB_NUMBER_DB_SEEK', 'ROCKSDB_NUMBER_DB_SEEK_FOUND',
  'ROCKSDB_NUMBER_MULTIGET_BYTES_READ', 'ROCKSDB_NUMBER_MULTIGET_GET',
  'ROCKSDB_NUMBER_MULTIGET_KEYS_READ'
);

show status like 'Handler_mrr_init';
SELECT
  variable_name,
  mrr_stats_t2.variable_value - mrr_stats_t1.variable_value as DIFF
FROM
  mrr_stats_t1 JOIN mrr_stats_t2 USING (VARIABLE_NAME)
HAVING
  DIFF>0;
