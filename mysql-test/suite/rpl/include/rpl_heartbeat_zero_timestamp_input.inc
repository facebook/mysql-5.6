################################################################################
# The workload generator on the master for rpl_heartbeat_zero_timestamp
################################################################################

# Create the databases and tables

--disable_query_log
--disable_result_log

let $i = $databases;

while ($i)
{
  eval create database test$i;
  eval use test$i;
  eval create table t$i (a int) engine=InnoDB;
  dec $i;
}

# Run the queries

while ($iter)
{
  let $i=$databases;

  while ($i)
  {
    eval use test$i;
    eval insert into t$i values ($iter);
    dec $i;
  }
  dec $iter;
}

--enable_result_log
--enable_query_log
