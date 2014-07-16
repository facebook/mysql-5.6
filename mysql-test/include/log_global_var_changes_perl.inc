--perl

### Put the logs of initial values and changed values and variable names into hash tables
my %hash = ();
my %hash_names = ();
open LOG_FILE, "$ENV{'MYSQLTEST_VARDIR'}/log/mysqld.1.err" or die;
my @lines = <LOG_FILE>;
my $matched = 0;
foreach $line (@lines) {

  ### If this line is a log of initial value
  if ($line =~ m/.*\[Note\] Global variable initial value (.*)/) {
    $s = $1;
    my $r = "";
    if ($s =~ m/(.*)\/data\/users\/(.*)\/mysql\/build\-([0-9]\.[0-9])\-([a-zA-Z]+)\/(.*)/) {
      $r = "$1\/data\/users\/USER\/mysql\/build-VERSION-TYPE\/$5";
    }
    elsif ($s =~ m/(.*)\/data\/users\/(.*)\/mysql\/([0-9]\.[0-9])/) {
      $r = "$1\/data\/users\/USER\/mysql\/VERSION";
    }
    else {
      $r = $1;
    }
    $hash{$r} = 0;

    ### Extract the variable name
    if ($s =~ m/(.*)@@(.*) = (.*)/) {
      $r = $2;
      chomp $r;
      $hash_name{$r} = 0;
    }
    else {
      print "Couldn't extract variable name from $s\n";
    }
  }

  ### If this line is a log of changed value
  elsif ($line =~ m/.*\[Note\] Global variable (.*) by user (.*) from host (.*)/) {
    my $s = $1;
    my $r = "$s by user USER from host HOST";
    $hash{$r} = 0;

    ### Extract the variable name
    if ($s =~ m/(.*)@@(.*) has been changed(.*)/) {
      $r = $2;
      chomp $r;
      $hash_name{$r} = 0;
    }
    else {
      print "Couldn't extract variable name from $s\n";
    }
  }
}

### Check if all logs in std_data exist in the hash table

### Check the init values
open STD_DATA_INIT, "$ENV{'MYSQLTEST_VARDIR'}/std_data/log_global_var_changes_init.log" or die;
my @lns1 = <STD_DATA_INIT>;
foreach $ln (@lns1) {
  chomp $ln;
  if (exists($hash{$ln})) {
    print "$ln\n";
  }
  else {
    print "Not exists: $ln\n";
  }
}

### Check the changed values
my $test_type = $ENV{'MYSQLTEST_LOG_SYSVAR_CHG'};
my $std_file_name = "$ENV{'MYSQLTEST_VARDIR'}/std_data/";
if ($test_type eq "debug") {
  $std_file_name .= "log_global_var_changes_debug.log";
}
else {
  $std_file_name .= "log_global_var_changes.log";
}

open STD_DATA_CHG, $std_file_name or die;
my @lns2 = <STD_DATA_CHG>;
foreach $ln (@lns2) {
  chomp $ln;
  if (exists($hash{$ln})) {
    print "$ln\n";
  }
  else {
    print "Not exists: $ln\n";
  }
}

### Check the changes of session variables were not logged
$std_file_name = "$ENV{'MYSQLTEST_VARDIR'}/std_data/log_global_var_changes_session_var.log";

open STD_DATA_SESSION, $std_file_name or die;
my @lns3 = <STD_DATA_SESSION>;
foreach $ln (@lns3) {
  chomp $ln;
  if (exists($hash_name{$ln})) {
    print "Shouldn't exist: $ln\n";
  }
  else {
    print "Not exists as expected: $ln\n";
  }
}
EOF
