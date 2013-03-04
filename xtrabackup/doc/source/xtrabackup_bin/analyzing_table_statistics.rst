============================
 Analyzing Table Statistics
============================

The |xtrabackup| binary can analyze InnoDB data files in read-only mode to give statistics about them. To do this, you should use the :option:`--stats` option. You can combine this with the :option:`--tables` option to limit the files to examine. It also uses the :option:`--use-memory` option.

You can perform the analysis on a running server, with some chance of errors due to the data being changed during analysis. Or, you can analyze a backup copy of the database. Either way, to use the statistics feature, you need a clean copy of the database including correctly sized log files, so you need to execute with :option:`--prepare` twice to use this functionality on a backup.

The result of running on a backup might look like the following: ::

  <INDEX STATISTICS>
    table: test/table1, index: PRIMARY, space id: 12, root page 3
    estimated statistics in dictionary:
      key vals: 25265338, leaf pages 497839, size pages 498304
    real statistics:
       level 2 pages: pages=1, data=5395 bytes, data/pages=32%
       level 1 pages: pages=415, data=6471907 bytes, data/pages=95%
          leaf pages: recs=25958413, pages=497839, data=7492026403 bytes, data/pages=91%

This can be interpreted as follows:

* The first line simply shows the table and index name and its internal identifiers. If you see an index named ``GEN_CLUST_INDEX``, that is the table's clustered index, automatically created because you did not explicitly create a ``PRIMARY KEY``.

* The estimated statistics in dictionary information is similar to the data that's gathered through ``ANALYZE TABLE`` inside of |InnoDB| to be stored as estimated cardinality statistics and passed to the query optimizer.

* The real statistics information is the result of scanning the data pages and computing exact information about the index.

* ``The level <X> pages``: output means that the line shows information about pages at that level in the index tree. The larger ``<X>`` is, the farther it is from the leaf pages, which are level 0. The first line is the root page.

* The ``leaf pages`` output shows the leaf pages, of course. This is where the table's data is stored.

* The ``external pages``: output (not shown) shows large external pages that hold values too long to fit in the row itself, such as long ``BLOB`` and ``TEXT`` values.

* The ``recs`` is the real number of records (rows) in leaf pages.

* The ``pages`` is the page count.

* The ``data`` is the total size of the data in the pages, in bytes.

* The ``data/pages`` is calculated as (``data`` / (``pages`` * ``PAGE_SIZE``)) * 100%. It will never reach 100% because of space reserved for page headers and footers.

A more detailed example is posted as a MySQL Performance Blog post.

Script to Format Output
=======================

The following script can be used to summarize and tabulate the output of the statistics information: ::

    tabulate-xtrabackup-stats.pl

    #!/usr/bin/env perl
    use strict;
    use warnings FATAL => 'all';
    my $script_version = "0.1";
     
    my $PG_SIZE = 16_384; # InnoDB defaults to 16k pages, change if needed.
    my ($cur_idx, $cur_tbl);
    my (%idx_stats, %tbl_stats);
    my ($max_tbl_len, $max_idx_len) = (0, 0);
    while ( my $line = <> ) {
       if ( my ($t, $i) = $line =~ m/table: (.*), index: (.*), space id:/ ) {
          $t =~ s!/!.!;
          $cur_tbl = $t;
          $cur_idx = $i;
          if ( length($i) > $max_idx_len ) {
             $max_idx_len = length($i);
          }
          if ( length($t) > $max_tbl_len ) {
             $max_tbl_len = length($t);
          }
       }
       elsif ( my ($kv, $lp, $sp) = $line =~ m/key vals: (\d+), \D*(\d+), \D*(\d+)/ ) {
          @{$idx_stats{$cur_tbl}->{$cur_idx}}{qw(est_kv est_lp est_sp)} = ($kv, $lp, $sp);
          $tbl_stats{$cur_tbl}->{est_kv} += $kv;
          $tbl_stats{$cur_tbl}->{est_lp} += $lp;
          $tbl_stats{$cur_tbl}->{est_sp} += $sp;
       }
       elsif ( my ($l, $pages, $bytes) = $line =~ m/(?:level (\d+)|leaf) pages:.*pages=(\d+), data=(\d+) bytes/ ) {
          $l ||= 0;
          $idx_stats{$cur_tbl}->{$cur_idx}->{real_pages} += $pages;
          $idx_stats{$cur_tbl}->{$cur_idx}->{real_bytes} += $bytes;
          $tbl_stats{$cur_tbl}->{real_pages} += $pages;
          $tbl_stats{$cur_tbl}->{real_bytes} += $bytes;
       }
    }
     
    my $hdr_fmt = "%${max_tbl_len}s %${max_idx_len}s %9s %10s %10s\n";
    my @headers = qw(TABLE INDEX TOT_PAGES FREE_PAGES PCT_FULL);
    printf $hdr_fmt, @headers;
     
    my $row_fmt = "%${max_tbl_len}s %${max_idx_len}s %9d %10d %9.1f%%\n";
    foreach my $t ( sort keys %tbl_stats ) {
       my $tbl = $tbl_stats{$t};
       printf $row_fmt, $t, "", $tbl->{est_sp}, $tbl->{est_sp} - $tbl->{real_pages},
          $tbl->{real_bytes} / ($tbl->{real_pages} * $PG_SIZE) * 100;
       foreach my $i ( sort keys %{$idx_stats{$t}} ) {
          my $idx = $idx_stats{$t}->{$i};
          printf $row_fmt, $t, $i, $idx->{est_sp}, $idx->{est_sp} - $idx->{real_pages},
             $idx->{real_bytes} / ($idx->{real_pages} * $PG_SIZE) * 100;
       }
    }

Sample Script Output
--------------------

The output of the above Perl script, when run against the sample shown in the previously mentioned blog post, will appear as follows: ::

            TABLE           INDEX TOT_PAGES FREE_PAGES   PCT_FULL
  art.link_out104                    832383      38561      86.8%
  art.link_out104         PRIMARY    498304         49      91.9%
  art.link_out104       domain_id     49600       6230      76.9%
  art.link_out104     domain_id_2     26495       3339      89.1%
  art.link_out104 from_message_id     28160        142      96.3%
  art.link_out104    from_site_id     38848       4874      79.4%
  art.link_out104   revert_domain    153984      19276      71.4%
  art.link_out104    site_message     36992       4651      83.4%

The columns are the table and index, followed by the total number of pages in that index, the number of pages not actually occupied by data, and the number of bytes of real data as a percentage of the total size of the pages of real data. The first line in the above output, in which the ``INDEX`` column is empty, is a summary of the entire table.
