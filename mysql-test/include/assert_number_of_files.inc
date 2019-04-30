# ==== Purpose ====
#
# Assert that number of files matching given $file_spec is equal to expected.
#
# ==== Usage ====
#
# --let file_spec = PATH_REGEX
# --let expected_number = INTEGER
# --source include/assert_number_of_files.inc
#
# Parameters:
#   $file_spec
#     The path with wildcards e.g. $MYSQLTEST_VARDIR/abcd*
#
#   $expected_number
#     The expected number of files

if ($VALGRIND_TEST)
{
  --sleep 5
}

--perl
  use strict;
  my $dir = $ENV{'MYSQL_TMP_DIR'} or die "MYSQL_TMP_DIR not set";
  my $file_spec = $ENV{'file_spec'} or die "file_spec not set";
  my @files = <$ENV{'file_spec'}>;
  open (OUTPUT, ">$dir/number_of_files.inc") ;
  print OUTPUT "--let \$number_of_files = ", scalar(@files), "\n";
  close (OUTPUT);
EOF
--source $MYSQL_TMP_DIR/number_of_files.inc
--remove_file $MYSQL_TMP_DIR/number_of_files.inc


if ($expected_number != $number_of_files)
{
  --echo ====================== Test assertion failed: ======================
  --echo The number of files ($number_of_files) is different that expected $expected_number
  --echo ====================================================================
  --die Test assertion failed in assert_number_of_files.inc
}

--echo include/assert_number_of_files.inc [The number of files matches expected $expected_number]
