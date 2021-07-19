#!/usr/bin/env perl

use strict;

my $row_count = $ARGV[0];

my $value = 'x' x 250;
for (my $i= 1; $i <= $row_count; $i++) {
  print "$i,$i,$value\n";
}
