################################################################################
# This is used by rpl_slave_monotonic_timestamp.test to keep polling the second
# behind master on the slave every 0.1 seconds for $sbm_polling_times times
# and assert that second behind master is within the upperbound
################################################################################

while ($sbm_polling_times)
{
  let $sbm= query_get_value("show slave status", Seconds_Behind_Master, 1);
  let $upper_bound = `select 100000000`;
  if ($sbm >= $upper_bound) {
    # Since small timestamp is around 1000000000, current time ls 14xxxxxxxx. So
    # The upper bound should be smaller than 4xxxxxxxx. We choose 100000000
    let $assert_text = Seconds_Behind_Master must be less than 'upper_bound';
    let $assert_cond = $sbm < $upper_bound;
    --source include/assert.inc
  }
  --real_sleep 0.1
  dec $sbm_polling_times;
}
