# ==== Purpose ====
#
# Ensure that the server is running with binlogging off
#
# ==== Usage ====
#
# source include/have_no_log_bin.inc;

--let $_have_log_bin= query_get_value(SHOW VARIABLES LIKE 'log_bin', Value, 1)
if ($_have_log_bin != OFF)
{
  --skip Test requires --log-bin to be disabled
}
--let $show_rpl_debug_info= 1
