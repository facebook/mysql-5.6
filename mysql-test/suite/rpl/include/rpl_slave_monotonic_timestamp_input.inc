################################################################################
# The workload generator on the master for rpl_slave_monotonic_timestamp_test
################################################################################

# Create the databases and tables

--disable_query_log
--disable_result_log

let $small_timestamp = 1000000000;
let $cur_timestamp = `select unix_timestamp()`;

let $i = $databases;

while ($i)
{
  eval drop database if exists test$i;
  eval create database test$i;
  eval use test$i;
  eval drop table if exists t$i;
  eval create table t$i (a int) engine=InnoDB;
  dec $i;
}

# Run the queries with different timestamps

while ($iter)
{
  let $i=$databases;

  while ($i)
  {
    if ($i == 4)
    {
      eval set timestamp=$small_timestamp;
      dec $small_timestamp;
    }
    if ($i == 3)
    {
      eval set timestamp=$cur_timestamp;
      inc $cur_timestamp;
    }
    if ($i == 2)
    {
      eval set timestamp=$small_timestamp;
      dec $small_timestamp;
    }
    if ($i == 1)
    {
      eval set timestamp=$cur_timestamp;
      inc $cur_timestamp;
    }

    eval use test$i;
    eval insert into t$i values ($iter);
    dec $i;
  }
  dec $iter;
}

--enable_result_log
--enable_query_log
