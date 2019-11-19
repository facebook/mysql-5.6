--disable_query_log
--disable_result_log
--let $submode= query_get_value(SHOW VARIABLES LIKE "%replica_parallel_type%", Value, 1)
if ($submode != 'DEPENDENCY')
{
  --skip The test needs replica_parallel_type=DEPENDENCY
}
--enable_result_log
--enable_query_log

