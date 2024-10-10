#!/bin/awk

/^->/ { 
  top_iterator_type[$2]++
  total++
}
END {
   for (i in top_iterator_type) {
		print i, top_iterator_type[i];
	}
   print "Total sql plan entries in slow log file: ", total;
}


