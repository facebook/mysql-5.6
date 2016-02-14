################################################################################
# This Perl script takes two input arguments:
#   argv[0]: the mysqld var directory path that contains the build type.
#   argv[1]: the PID of the mysqld process.
#   argv[2]: the expected max size of the core file.
# The mysqld process will be killed to dump a core. We get the core file name
# pattern only for retrieving the core file directory. We won't analyze the pattern
# and we always assume the PID is part of the core file name. This script search
# the files in the core directory and if a file whose name contains the PID and
# the last write time is less than one minute ago, we will assume this is the core
# file that was successfully dumped by the kill command.
# This script prints out message of success only when the core file is found and
# its size is less than the expected max size.
#

# The input argument is the PID file name
my $var_path = $ARGV[0];
my $pid_file = $ARGV[1];
my $expected_max_core_size = $ARGV[2];

# Get PID of mysqld
open(my $fh, '<', $pid_file) || die "Cannot open pid file $pid_file\n";
my $pid = <$fh>;
$pid =~ s/\s//g;
close($fh);

if ($pid eq "") {
   die "Couldn't retrieve PID from PID file.\n";
}

# Kill mysqld to dump a core
system("kill", "-s", "SIGSEGV", "$pid");
print "# Perl: Sent a SIGSEGV to mysqld to dump a core.\n";

# sleep 2 seconds to make sure the core was fully dumped
sleep 2;

# Skip ASan and Valgrind build since it has different fatal signal
# handling so core won't be dumped
$asan_or_valgrind_build = 0;
if (index(lc($var_path), "asan") != -1 ||
    index(lc($var_path), "valgrind") != -1) {
  $asan_or_valgrind_build = 1;
  goto END;
}

# Get the core file pattern, e.g. /var/tmp/cores/%e.%p
$core_pattern = `cat /proc/sys/kernel/core_pattern`;

$last_slash = rindex($core_pattern, '/');

# The core file directory, e.g. /var/tmp/cores
$core_dir = substr($core_pattern, 0, $last_slash);

$found_core = 0;
$core_size_good = 0;

# Check the files in the core file directory
opendir(my $dir, $core_dir) or die "Failed to open dir $core_dir: $!\n";
while (my $file = readdir($dir)) {
  # If the core file name contains the PID
  if (index($file, $pid) != -1) {
    # The current time in seconds since epoch
    $cur_time = time;

    # The last write time in seconds since epoch
    $full_path = $core_dir . '/' . $file;
    @stat = stat($full_path);
    $file_sz = $stat[7];
    $write_secs = $stat[9];

    # If the file was written within a minute
    if ($cur_time > $write_secs && $cur_time - $write_secs < 60) {
      $found_core = 1;
      if ($file_sz < $expected_max_core_size) {
        $core_size_good = 1;
      }
      # Remove the core file to avoid it get accumulated over time
      unlink $full_path;
      last;
    }
  }
}
closedir($dir);

END:

# Skip ASan and Valgrind build since it has different fatal signal
# handling so core won't be dumped
if ($found_core || $asan_or_valgrind_build) {
  if ($core_size_good || $asan_or_valgrind_build) {
    print "# Perl: OK! Found the core file and it's small, or skipped for Asan and Valgrind builds!\n";
  } else {
    print "# Perl: Failed! Found the core file but it's too big!\n";
  }
} else {
  print "# Perl: Failed! Didn't find the core file!\n";
}
