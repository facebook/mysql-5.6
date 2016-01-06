# include/wait_for_file_lines.inc
#
# SUMMARY
#
#    Waits until the requested number of lines that match a criteria to exist
#    in a file
#
# USAGE
#
#    let $file_lines_count = 6;
#    let $file_lines_filename = "mylog.txt";
#    let $file_lines_pattern = ^.*$;  # Override default that matches all
#    let $wait_timeout= 60; # Override default 30 seconds with 60.
#    --source include/wait_for_file_lines.inc
#
# EXAMPLE
#    main.slow_log_extra_big
#

let $wait_timeout_actual= 30;
if ($wait_timeout)
{
  let $wait_timeout_actual= $wait_timeout;
}
# Reset $wait_timeout so that its value won't be used on subsequent
# calls, and default will be used instead.
let $wait_timeout= 0;

let $file_lines_pattern_actual=;
if ($file_lines_pattern)
{
  let $file_lines_pattern_actual= $file_lines_pattern;
}

# Reset $file_lines_pattern so that its value won't be used on subsequent
# calls, and the default will be used instead.
let $file_lines_pattern= 0;

let FL_WAIT_TIMEOUT= $wait_timeout_actual;
let FL_COUNT= $file_lines_count;
let FL_PATTERN= $file_lines_pattern_actual;
let FL_FILENAME= $file_lines_filename;

perl;
use Time::HiRes;
my $end= Time::HiRes::gettimeofday() + $ENV{'FL_WAIT_TIMEOUT'};
my $success= 0;
my $count_needed= $ENV{'FL_COUNT'};
my $pattern= $ENV{'FL_PATTERN'};
my $filename= $ENV{'FL_FILENAME'};
while (!$success && Time::HiRes::gettimeofday() < $end) {
  open(FH, "<$filename") or die "Failed to open \"$filename\"";
  my $count= 0;
  while (<FH>) {
    $count++ if ($_ =~ /$pattern/);
  }
  if ($count >= $count_needed) {
    $success = true;
  }
  else {
    Time::HiRes::usleep(100000); # 1/10th of a second
  }
  close FH;
}
die "Timeout in wait_for_file_lines.inc expecting $count_needed lines\n"
  unless $success;
EOF
