--echo # Get the full path name of the PID file
--let $pid_file= query_get_value(SELECT @@pid_file, @@pid_file, 1)
--let PIDFILE= $pid_file

--echo # Expecting a "crash", but don't restart the server until it is told to
--let $server_id= `SELECT @@server_id`

--echo # Expected max core size is $expected_max_core_size MB
--let MAXCORESIZE= $expected_max_core_size

--let $expect_file_name= $MYSQLTEST_VARDIR/tmp/mysqld.$server_id.expect
--exec echo "wait" > $expect_file_name

--let $server_log= $MYSQLTEST_VARDIR/log/mysqld.1.err
--let SERVERLOGFILE= $server_log

perl;

my $pid_file = $ENV{'PIDFILE'} or die "PIDFILE not set";
my $expected_max_core_size = $ENV{'MAXCORESIZE'} or die "MAXCORESIZE not set";

# The argument is in MB
$expected_max_core_size = $expected_max_core_size * 1024 * 1024;

# Get PID of mysqld
open(my $fh, '<', $pid_file) || die "Cannot open pid file $pid_file\n";
my $pid = <$fh>;
$pid =~ s/\s//g;
close($fh);

if ($pid eq "") {
  die "Couldn't retrieve PID from PID file.\n";
}

my $server_log= $ENV{'SERVERLOGFILE'} or die "SERVERLOGFILE not set";

print "# Perl: Save the current server log content.\n";
my $log_content;
open(my $rlogh, '<', $server_log) or die "Cannot open server log $server_log for read!\n";
{
  local $/;
  $log_content = <$rlogh>;
}
close($rlogh);

# The current time in seconds since epoch
$cur_time = time;

# Kill mysqld to dump a core
system("kill", "-s", "SIGSEGV", "$pid");
print "# Perl: Sent a SIGSEGV to mysqld to dump a core.\n";

# Get the core file pattern, e.g. /var/tmp/cores/%e.%p
$core_pattern = `cat /proc/sys/kernel/core_pattern`;

$last_slash = rindex($core_pattern, '/');

# The core file directory, e.g. /var/tmp/cores
$core_dir = substr($core_pattern, 0, $last_slash);

$found_core = 0;
$core_size = 0;
$core_size_good = 0;

# Check the files in the core file directory
$wait_sec = 60;
while ($wait_sec > 0) {
  opendir(my $dir, $core_dir) or die "Failed to open dir $core_dir: $!\n";
  while (my $file = readdir($dir)) {
    # If the core file name contains the PID
    if (index($file, $pid) != -1) {
      # The last write time in seconds since epoch
      $full_path = $core_dir . '/' . $file;
      @stat = stat($full_path);
      $core_size = $stat[7];
      $write_secs = $stat[9];

      # If the file was written within a minute
      if ($cur_time <= $write_secs && $write_secs - $cur_time < 60) {
        $found_core = 1;
        if ($core_size < $expected_max_core_size) {
          $core_size_good = 1;
        }
        # Remove the core file to avoid it get accumulated over time
        unlink $full_path;
        last;
      }
    }
  }
  closedir($dir);

  if ($found_core) {
    last;
  }
  # Sleep 1 second and try again
  --$wait_sec;
  sleep 1;
}

if ($found_core) {
  if ($core_size_good) {
    print "# Perl: OK! Found the core file and it's small!\n";
  } else {
    print "# Perl: Failed! Found the core file but it's too big ($core_size)!\n";
  }
} else {
  print "# Perl: Failed! Didn't find the core file!\n";
}

print "# Perl: Restore server log to exclude the log of crash.\n";
open(my $wlogh, '>', $server_log) or die "Cannot open file $server_log for write!\n";
print $wlogh $log_content;
print $wlogh "[Note] <<<The log of the crash in this test case has been cleaned up to avoid complains>>>\n";
close $wlogh;

EOF

--echo # Make server restart
--exec echo "restart" > $expect_file_name

--enable_reconnect

--echo # Wait for server to be back online
--source include/wait_until_connected_again.inc

--disable_reconnect
