# ==== Purpose ====
#
# MySQL now includes gtid sets within the binlog index file. However, some
# tests expect to be able to load the original binlog index file into a table
# directly. This include can read a binlog.index or relay_log.index file that
# might contain prev gtid sets and filter out the gtid sets. It will generate
# a tmp .index file that does not contain the gtid sets for loading into MySQL.
#
# ==== Usage ====
#
# --let $gen_binlog_index_file= FILE_NAME
# --let $gen_binlog_output_file= FILE_NAME
# --source include/rpl_gen_binlog_index_file.inc
#
# Parameters:
#
#   $index_file
#     The index file where the filenames are stored.
#
#   $output_file
#     The output file to store only the filenames.

--let $include_filename= rpl_gen_binlog_index_file.inc
--source include/begin_include_file.inc

--disable_query_log

--let _INPUT_FILE= $gen_binlog_index_file
--let _OUTPUT_FILE= $gen_binlog_output_file
if ($rpl_debug)
{
  --echo _INPUT_FILE='$_INPUT_FILE'
  --echo _OUTPUT_FILE='$_OUTPUT_FILE'
}

perl;
  my $idx_file= $ENV{'_INPUT_FILE'};
  my $out_file= $ENV{'_OUTPUT_FILE'};

  # Read index file and generate a new file that does not contain the gtid set
  open(FILE, "$idx_file") or die "Error $? opening $idx_file: $!";
  open(OFILE, ">$out_file") or die "Error $? opening $out_file: $!";
  my $relay_log_file= '';
  while (<FILE>) {
    @arr= split(/ /, $_);
    $relay_log_file= $arr[0];
    # Index file contains gtid sets which need to be skipped
    if ($#arr > 0) {
      seek(FILE, $arr[1] + 1, 1);
    }
    chomp($relay_log_file);
    print OFILE "$relay_log_file\n";
  }
  close FILE or die "Error $? closing $idx_file $!";
  close OFILE or die "Error $? closing $out_file: $!";
EOF

--let $include_filename= rpl_gen_binlog_index_file.inc
--source include/end_include_file.inc
