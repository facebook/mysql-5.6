#
# Include this script to wait until the connection to the
# server has been restored or timeout occurs
--disable_result_log
--disable_query_log
let $counter= 3000;
if ($wait_timeout)
{
  let $counter= $wait_timeout;
}
# Reset $wait_timeout
let $wait_timeout= 0;

let $mysql_errno= 9999;
while ($mysql_errno)
{
  # Strangely enough, the server might return "Too many connections"
  # while being shutdown, thus 1040 is an "allowed" error
  # See BUG#36228
  --error 0,1040,1053,2002,2003,2006,2013,2064
  show status;

  dec $counter;
  if (!$counter)
  {
    --die Server failed to restart
  }
  --sleep 0.1
}
if ($rpl_enable_raft)
{
  if (!$rpl_skip_election_on_startup) {
    source include/rpl_raft_make_master_leader.inc;
  }
}
--enable_query_log
--enable_result_log
