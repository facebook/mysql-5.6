--let $_agmo_gtid_mode= `SELECT @@GLOBAL.GTID_MODE`
if ($_agmo_gtid_mode != 'OFF')
{
  --source include/show_rpl_debug_info.inc
  --echo !!!ERROR IN TEST: GTID_MODE was $_agmo_gtid_mode, needs to be OFF
  --die !!!ERROR IN TEST: GTID_MODE was not OFF
}
