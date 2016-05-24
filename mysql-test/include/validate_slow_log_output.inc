# validate_slow_log_output.inc
#
# SUMMARY
#
#    Parses lines in the slow log output file that start with "# Query_time"
#    and pull the values into a temporary table.  Displays the lines and
#    validates that the number of lines is the number expected.
#
# USAGE
#
#    let $slow_log_output_filename = "slow.log";
#    let $slow_log_count = 6;
#    --source validate_slow_log_output.inc
#
# EXAMPLE
#    main.slow_log_extra_big
#

--echo #
--echo # This is a hack to check the log result.
--echo # We strip off time related fields (non-deterministic)
--echo # and verify the rest are correct.
--echo #
let SLO_FILENAME = $slow_log_output_filename;
--perl
# Parse the log file
open FILE, "<$ENV{'SLO_FILENAME'}" or die;
open TMP, ">$ENV{'MYSQLTEST_VARDIR'}/mysqld.1/data/test/indata.tmp" or die;
my @lines = <FILE>;
foreach $line (@lines) {
  if ($line =~ m/^# Query_time/) {
    $line =~ m/
        Rows_sent:\s(\d+).*
        Rows_examined:\s(\d+).*
        Errno:\s(\d+).*
        Killed:\s(\d+).*
        Bytes_received:\s(\d+).*
        Bytes_sent:\s(\d+).*
        Read_first:\s(\d+).*
        Read_last:\s(\d+).*
        Read_key:\s(\d+).*
        Read_next:\s(\d+).*
        Read_prev:\s(\d+).*
        Read_rnd:\s(\d+).*
        Read_rnd_next:\s(\d+).*
        RocksDB_key_skipped:\s(\d+).*
        RocksDB_del_skipped:\s(\d+).*
        Sort_merge_passes:\s(\d+).*
        Sort_range_count:\s(\d+).*
        Sort_rows:\s(\d+).*
        Sort_scan_count:\s(\d+).*
        Created_tmp_disk_tables:\s(\d+).*
        Tmp_table_bytes_written:\s(\d+).*/x;
    # Write the comma-deliminated data to the temporary file
    print TMP "$1,$2,$3,$4,$5,$6,$7,$8,$9,$10,".
              "$11,$12,$13,$14,$15,$16,$17,$18,$19,$20,$21\n";
  }
}
close FILE;
close TMP;
EOF

--disable_query_log
create table log_output(Rows_sent int, Rows_examined int, Errno int,
                        Killed int, Bytes_received int, Bytes_sent int,
                        Read_first int, Read_last int, Read_key int,
                        Read_next int, Read_prev int, Read_rnd int,
                        Read_rnd_next int, RocksDB_key_skipped int,
                        RocksDB_del_skipped int, Sort_merge_passes int,
                        Sort_range_count int, Sort_rows int,
                        Sort_scan_count int, Created_tmp_disk_tables int,
                        Tmp_table_bytes_written int);
load data infile 'indata.tmp' into table log_output
    fields terminated by ',' lines terminated by '\n';
--enable_query_log

select * from log_output;

--disable_query_log
eval select count(*) = $file_lines_count
    "if the following is not 1 you need to adjust the file_lines_count variable"
    from log_output;
drop table log_output;
--enable_query_log

--remove_file $MYSQLTEST_VARDIR/mysqld.1/data/test/indata.tmp
