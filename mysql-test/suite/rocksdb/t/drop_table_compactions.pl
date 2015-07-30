sub print_array {
  $str = shift;
  @arr = @_;
  $prev= 0;
  foreach (@arr) {
    if ($prev) {
      $dummy_idx = $_ - $prev;
    }else {
      $dummy_idx = 0;
    }
    $prev= $_;
    print "$str $dummy_idx\n";
  }
}

while (<>) {
  $a{$1} += $2 if /Compacting away elements from dropped index (\d+): (\d+)/;
  if (/Begin filtering dropped index (\d+)/) {
    push @b, $1;
  }
  if (/Finished filtering dropped index (\d+)/) {
    push @c, $1;
  }
}
$prev= 0;
foreach (sort {$a <=> $b} keys %a){
  if ($prev) {
    $dummy_idx= $_ - $prev;
  }else {
    $dummy_idx= 0;
  }
  $prev= $_;
  print "Compacted+ $dummy_idx: $a{$_}\n";
}
print_array("Begin filtering dropped index+", sort {$a <=> $b} @b);
print_array("Finished filtering dropped index+", sort {$a <=> $b} @c);
