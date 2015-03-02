while (<>) {
  $a{$1} += $2 if /Compacting away elements from dropped index (\d+): (\d+)/;
}
print "Compacted $_: $a{$_}\n" foreach (sort keys %a);
