--echo set_gaplock_query_monitor(logfilename=$logfilename, set=$set, enable=$enable, verbose=$verbose)

eval set global innodb_monitor_gaplock_query_print_verbose = $verbose;
show variables like '%innodb_monitor_gaplock_query_print_verbose%';

if ($set)
{
  --echo 'setting gap lock query log to $logfilename'
  eval set global innodb_monitor_gaplock_query_filename= $logfilename;
  show variables like 'innodb_monitor_gaplock_query_filename';
}

if ($enable)
{
  --echo 'enabling gap lock query logging'
  set global innodb_monitor_gaplock_query= true;
  show variables like 'innodb_monitor_gaplock_query';
}

if (!$enable)
{
  --echo 'disabling gap lock query logging'
  set global innodb_monitor_gaplock_query= false;
  show variables like 'innodb_monitor_gaplock_query';

}
