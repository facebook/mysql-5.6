--perl
my $dir = $ENV{'MYSQLD_TMPDIR'};
my $size;
my $found = 0;
my $total_size = 0;

opendir(DIR, $dir) or die $!;
while (my $file = readdir(DIR))
{
  if ($file =~ m/\.MYD$/)
  {
    $found = 1;
    $size = -s "$dir/$file";
    $total_size += $size;
    if ($size != $ENV{'EXPECTED_FILE_SIZE'}) {
      print "Size size mismatch: $size != $ENV{'EXPECTED_FILE_SIZE'}\n";
    }
  }

  if ($file =~ m/\.MYI$/)
  {
    $size = -s "$dir/$file";
    $total_size += $size;
  }
}

if ($found == 0)
{
  print "No MYD files found in tmpdir.\n";
}

if ($total_size != $ENV{'EXPECTED_TOTAL_FILE_SIZE'})
{
  print "Total size mismatch: $total_size != $ENV{'EXPECTED_TOTAL_FILE_SIZE'}\n";
}

close(DIR);
EOF
