#!/usr/bin/perl

my $a = 0;
my $b=0;
my $filename = shift or die "Usage: $0 FILENAME\n";
open(my $f, "<", $filename) or die $!;
while(readline($f)) {
  if (/(\d+) before/) {
    $a = $1;
  }

  if (/(\d+) after/ ) {
    $b = $1;
  }
}

my $is_rocksdb_ddse = shift//0;
my $multiply = 2;
if ($is_rocksdb_ddse) {
$multiply = 1.5
}

if ($a > $b * multiply) {
  printf("Compacted\n");
}
