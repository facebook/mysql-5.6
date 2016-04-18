perl;
my $existence = $ENV{'EXPECT_EXISTENCE'} or die "EXPECT_EXISTENCE not set";
my $pid_file = $ENV{'PIDFILE'} or die "PIDFILE not set";
my $shutdown_file = $pid_file . ".shutdown";
$wait_sec = 60;
while ($wait_sec > 0) {
  # If the shutdown file has been created
  if ($existence eq "yes" && -e $shutdown_file) {
      print "Found shutodwn file as expected!\n";
      last;
  }
  # If the shutdown file has been deleted
  elsif ($existence eq "no" && ! -e $shutdown_file) {
      print "shutodwn file has been deleted as expected after server starts!\n";
      last;
  }
  # Sleep 1 second and try again
  --$wait_sec;
  sleep 1;
}
EOF
