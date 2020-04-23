# ==== Purpose ====
#
# Auxiliary file used in include/show_binlog_events.inc and
# include/show_relaylog_events.inc.
#
# ==== Usage ====
#
# See include/show_binlog_events.inc

--let $include_filename= show_events.inc
--source include/begin_include_file.inc

if (!$binlog_start)
{
  # If $binlog_start is not set, we will set it as the second event's
  # position.  The first event(Description Event) is always ignored. For
  # description event's length might be changed because of adding new events,
  # 'SHOW BINLOG EVENTS LIMIT 1' is used to get the right value.
  --let $binlog_start= query_get_value(SHOW BINLOG EVENTS LIMIT 1, End_log_pos, 1)
}

--let $_se_old_statement= $statement

--let $statement=show BINLOG events
if ($is_relay_log)
{
  --let $statement=show relaylog events
}

if ($binlog_file)
{
  --let $_binlog_file= $binlog_file
  if ($binlog_file == 'LAST')
  {
    if ($is_relay_log)
    {
      --let $_binlog_file= query_get_value(SHOW SLAVE STATUS, Relay_Log_File, 1)
    }
    if (!$is_relay_log)
    {
      --let $_binlog_file= query_get_value(SHOW MASTER STATUS, File, 1)
    }
  }
  --let $statement= $statement in '$_binlog_file'
}
if (!$binlog_file)
{
  # In raft mode, if the binlog file is not provided, we skip the first file coz
  # it will be empty and will have been rotated away
  if ($rpl_enable_raft)
  {
    if (!$dont_decrement_filename)
    {
      --let $_binlog_file= query_get_value(SHOW BINARY LOGS, Log_name, 2)
      --let $statement= $statement in '$_binlog_file'
    }
  }
}

--let $statement= $statement from $binlog_start

if ($binlog_limit != '')
{
  --let $statement= $statement limit $binlog_limit
}

# Execute the statement and write to $output_file
--let $output_file= GENERATE
--source include/write_result_to_file.inc

# Filter the file through the following script.
--delimiter ||
let $script=
  # todo: use select_columns instead (requires updating all result files)
  s{([^\t]*\t)[^\t]*(\t[^\t]*\t)[^\t]*\t[^\t]*(\t[^\t]*)}{DOLLAR1#DOLLAR2#\t#DOLLAR3};
  s{/\* xid=.* \*/}{/\* XID \*/};
  s{table_id: [0-9]+}{table_id: #};
  s{file_id=[0-9]+}{file_id=#};
  s{block_len=[0-9]+}{block_len=#};
  s{Server ver:.*DOLLAR}{SERVER_VERSION, BINLOG_VERSION};
  s{SQL_LOAD-[a-z,0-9,-]*.[a-z]*}{SQL_LOAD-<SERVER UUID>-<MASTER server-id>-<file-id>.<extension>};
  s{rand_seed1=[0-9]*,rand_seed2=[0-9]*}{rand_seed1=<seed 1>,rand_seed2=<seed 2>};
  s{((?:master|slave|slave-relay)-bin\.[0-9]{6};pos=)[0-9]+DOLLAR}{DOLLAR1POS};
  s{SONAME ".*"}{SONAME "LIB"};
  s{DOLLARmysqltest_vardir}{MYSQLTEST_VARDIR}g;
||
--let $pre_script= my DOLLARmysqltest_vardir = DOLLARENV{'MYSQLTEST_VARDIR'};
if (!$keep_gtid_events)
{
  --let $script= $script DOLLAR_ = '' if (m{\t(?:Gtid|Previous_gtids)\t});
}
if ($keep_gtid_events)
{
  let $pre_script= $pre_script
    my DOLLARuuid_regex = '[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}';
    my DOLLARuuid_sidno_regex = DOLLARuuid_regex.'(?::[0-9]+(?:-[0-9]+)?)+';
  ||
  --let $script= $script s{DOLLARuuid_sidno_regex(?:,DOLLARuuid_sidno_regex)*}{Gtid_set};
}
--delimiter ;

#--let $select_columns= 1 3 6
--let $input_file= $output_file
--source include/filter_file.inc

if (!$show_metadata_event)
{
  exec eval sed '/Metadata/d' -i $output_file;
}

# If Raft is enabled, it will have caused one rotation at the beginning making
# all binlog file names to be offset by 1. So we hack in to the output to
# subtract 1 from the file number to make it appear like there was no rotation
# in the beginning. This will help avoid having to re-record all tests which
# rely on binlog file name matching in the output
if ($rpl_enable_raft)
{
  if (!$dont_decrement_filename)
  {
    exec eval perl -pe 's/\(\(?:master\|slave\)-bin\.\)\([0-9]{6}\)/\$1.\(\"0\"\ x\(6-length\(\$2-1\)\)\).\(\$2-1\)/e' -i $output_file;
  }
}

# Write to result file
--cat_file $output_file

# Remove the file
--remove_file $output_file

--let $statement= $_se_old_statement

--let $include_filename= show_events.inc
--source include/end_include_file.inc
