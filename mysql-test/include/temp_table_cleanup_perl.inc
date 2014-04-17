--perl

### Find the names of the temp table files in the server error log

# How many logged entries have been seen in the current test run
my $logged_entries = 0;
my $num_file = "$ENV{'MYSQLTEST_VARDIR'}/tmp/temp_table_cleanup_logged_entries.txt";
if (-e $num_file) {
  open NUM_FILE, "$num_file" or die "Failed to open $num_file for read.";
  my @lns = <NUM_FILE>;
  $logged_entries = $lns[0];
  close NUM_FILE;
}
print "Number of logged entries seen last time = $logged_entries\n";

my $err_file = "$ENV{'MYSQLTEST_VARDIR'}/log/mysqld.1.err";
open LOG_FILE, $err_file or die "Failed to open $err_file for read.";

my $myd = "";
my $myi = "";
my $num_of_entries = 0;
my $saw_test_start = 0;

my @lines = <LOG_FILE>;
my $total_lines = @lines;

# Scan the log from end
for (my $i = $total_lines - 1; $i >= 0; $i--) {

  # Exit the loop once the start of log of the current test case is seen
  if ($lines[$i] =~ m/CURRENT_TEST: main.temp_table_cleanup/) {
    $saw_test_start = 1;
    print "Found the start of the test case in error log.\n";
    last;
  }

  # Looking for the debug log of the temp table names
  if ($lines[$i] =~ m/\[temp-table-names\]\[.*\]<MYD>(.*)<MYI>(.*)/) {
    # Only save the last entry
    if ($num_of_entries == 0) {
      $myd = $1;
      $myi = $2;
    }
    ++$num_of_entries;
  }
}
close LOG_FILE;

print "Number of entries seen this time = $num_of_entries.\n";

# Delete the temp file if required. The test case calls this script
# multiple times, this environment variable is set before the last call
# so that this temp file can be deleted.
if ($ENV{'ENV_DEL_TEMP_LINE_NUM_FILE'} eq "YES") {
   print "Deleting temp logged entries file.\n";
   unlink $num_file or die "Failed to delete temp logged entries file $num_file.\n";
}
# Save the number of logged entries have seen
else {
  open NUM_FILE, ">$num_file" or die "Failed to open $num_file for write.";
  print NUM_FILE "$num_of_entries";
  close NUM_FILE;
}

# Verify
if (!saw_test_start) {
  print "Unexpected: didn't see the start of the test case in the server error log.\n";
  return;
}

if ($myd eq "" || $myi eq "") {
  print "Unexpected: didn't find the temp table file names in error log.\n";
  return;
}

if (1 + $logged_entries != $num_of_entries) {
  print "Unexpected: the number of old logged entries doesn't match the total entries seen.\n";
  return;
}

if (-e $myd) {
  print "Unexpected: MYD file exists!\n";
}
else {
  print "Expected: MYD file doesn't exist!\n";
}

if (-e $myi) {
  print "Unexpected: MYI file exists!\n";
}
else {
  print "Expected: MYI doesn't exist!\n";
}
EOF
