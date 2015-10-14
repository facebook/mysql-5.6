--perl
my $dir = $ENV{'MYSQLD_TMPDIR'};
my $size;

opendir(DIR, $dir) or die $!;
while (my $file = readdir(DIR))
{
  next unless ($file =~ m/\.MYD$/);
  $size = -s "$dir/$file";
  if ($size != $ENV{'EXPECTED_FILE_SIZE'}) {
    print "Size size mismatch: $size != $ENV{'EXPECTED_FILE_SIZE'}\n";
  }
}
close(DIR);
EOF
