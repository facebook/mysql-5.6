--perl

my $err_file = "$ENV{'MYSQLTEST_VARDIR'}/log/mysqld.1.err";
open LOG_FILE, $err_file or die "Failed to open $err_file for read.";

my @lines = <LOG_FILE>;
my $total_lines = @lines;

# Scan the log from end
for (my $i = $total_lines - 1; $i >= 0; $i--) {

  # Exit the loop once the start of log of the current test case is seen
  if ($lines[$i] =~ m/CURRENT_TEST: main.query_attrs/) {
    print "Found the start of the test case in error log.\n";
    last;
  }

  # Looking for the debug log of the temp table names
  if ($lines[$i] =~ m/\[query-attrs\]\[.*\](.*)/) {
    print $lines[$i]
  }
}
close LOG_FILE;
EOF
