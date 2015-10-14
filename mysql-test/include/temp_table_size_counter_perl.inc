--perl
my $dir = $ENV{'MYSQLD_TMPDIR'};
my $size;
my $found = 0;

opendir(DIR, $dir) or die $!;
while (my $file = readdir(DIR))
{
  next unless ($file =~ m/\.MYD$/);
  $found = 1;
  $size = -s "$dir/$file";
  if ($size != $ENV{'EXPECTED_FILE_SIZE'}) {
    print "Size size mismatch: $size != $ENV{'EXPECTED_FILE_SIZE'}\n";
  }
}

if ($found == 0)
{
  print "No MYD files found in tmpdir.\n";
}
close(DIR);
EOF
