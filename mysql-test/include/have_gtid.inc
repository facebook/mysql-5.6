# ==== Purpose ====
#
# Ensure that the server is running with GTID support on.
#
# ==== Usage ====
#
# --source include/have_gtid.inc

--source include/have_log_bin.inc

if ( `SELECT @@GLOBAL.GTID_MODE = "OFF"` )
{
  --skip Test requires GTID_MODE=ON.
}
