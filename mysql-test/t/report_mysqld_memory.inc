let ITEM_COUNT = `$count_statement`;
let MYSQLD_PIDFILE = $pid_file;

--perl
  my $count = $ENV{'ITEM_COUNT'};
  my $pid_filename = $ENV{'MYSQLD_PIDFILE'};
  my $mysqld_pid= `cat $pid_filename`;
  chomp($mysqld_pid);
  my $ret = `pmap $mysqld_pid | grep total | sed 's/ total *//' | sed 's/K//'`;
  #  print STDOUT "$count, $ret";
  printf STDOUT "%8s, %10s", $count, $ret;
EOF


