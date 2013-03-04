# Copyright (C) 2008-2009 Sun Microsystems, Inc. All rights reserved.
# Use is subject to license terms.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
# USA

#####################################################################
#
# Author: Jorgen Loland
# Date: April 2009
#
# Purpose: Implementation of WL#4218: Test that transactions executed
# concurrently with backup are either completely restored or not
# restored at all. No transaction should be partially represented
# after restore.
#
# See further documentation in invariant.yy
#
# Associated files: 
#   mysql-test/gentest/conf/backup/invariant.yy
#   mysql-test/gentest/conf/backup/invariant.zz
#   mysql-test/gentest/lib/GenTest/Validator/Invariant.pm
#   mysql-test/gentest/lib/GenTest/Reporter/BackupAndRestoreInvariant.pm
#
#####################################################################
package GenTest::Reporter::BackupAndRestoreInvariant;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use GenTest;
use GenTest::Reporter;
use GenTest::Constants;

my $count = 0;
my $file = 'rqg_'.time().'.bup';
my $expected_total=100000;

sub monitor {
  my $reporter = shift;
  my $dsn = $reporter->dsn();

  # Return if BACKUP has already been executed
  return STATUS_OK if $count > 0;

  # Execute BACKUP when 20 seconds remains
  if (time() > $reporter->testEnd() - 20) {
      my $dbh = DBI->connect($dsn);

      unlink($file);
      say("Executing BACKUP DATABASE.");
      $dbh->do("BACKUP DATABASE test TO '".$file."'");
      say("Executing BACKUP DATABASE done.");
      $count++;
      if (defined $dbh->err()) {
          return STATUS_DATABASE_CORRUPTION;
      }
  }
  return STATUS_OK;
}

my $tables;
sub report {
  my $reporter = shift;
  my $dsn = $reporter->dsn();
  my $dbh = DBI->connect($dsn);

  ###########################
  # Get data before restore #
  ###########################
  $dbh = DBI->connect($dsn) if not defined $dbh;
  $tables = $dbh->selectcol_arrayref("SHOW TABLES") if not defined $tables;
  my $results;

  my $query="SELECT SUM(alltbls.int_not_null) as sum, 
                    COUNT(alltbls.int_not_null) as count 
             FROM (";

  my $tblno=0;
  foreach my $table (@$tables) {
    if ($tblno>0) {
      $query = $query . " UNION ALL ";
    }
    $query = $query . "SELECT int_not_null FROM " . $table;
    $tblno++;
  }
  $query = $query . ") AS alltbls";

  # Execute query
  my ($sum_before, $count_before) = $dbh->selectrow_array($query);

  if (($sum_before eq '') || ($count_before eq '')) {
    # Server probably crashed, the SELECT returned no data
    say ("Failed to query status before RESTORE");
    return STATUS_UNKNOWN_ERROR;
  }

  if (($sum_before ne $expected_total)) {
    say("Bad sum before RESTORE: sum: $sum_before; count: $count_before");
    return STATUS_DATABASE_CORRUPTION;
  }

  ##################################
  # RESTORE the backed up database #
  ##################################

  say("Executing RESTORE FROM.");
  $dbh->do("RESTORE FROM '".$file."' OVERWRITE");

  if (defined $dbh->err()) {
    return STATUS_DATABASE_CORRUPTION;
  }

  ##########################
  # Get data after restore #
  ##########################

  ######################
  # Consistency test 1 #
  ######################

  # Execute the same query that was executed before RESTORE
  my ($sum_after, $count_after) = $dbh->selectrow_array($query);

  if (($sum_after eq '') && ($count_after eq '')) {
    # Server probably crashed, the SELECT returned no data
    say ("Failed to query status after RESTORE");
    return STATUS_UNKNOWN_ERROR;
  }

  # The total amount of money across all bank account should not have changed
  if (($sum_after ne $expected_total)) {
    say("Bad sum for tables: sum: $sum_after; count: $count_after");
    return STATUS_DATABASE_CORRUPTION;
  }

  say("*** STARTING DATA CONSISTENCY CHECKING ***");
  say("Invariant (total amount of money is ".$expected_total.") is still still OK after RESTORE");

  ######################
  # Consistency test 2 #
  ######################

  my @predicates = ("/* No predicate */");

  push @predicates, (
    " FORCE INDEX(PRIMARY) WHERE `pk` >= -922337",
    " FORCE INDEX(PRIMARY) ORDER BY `pk` LIMIT 4294836225",
  );

  # Use other access methods than in test 1; access the two tables
  # independantly and with different predicates. 
  my $sum=0;
  my $count=0;
  foreach my $predicate (@predicates) {
    foreach my $table (@$tables) {
      $query="SELECT SUM(int_not_null) as sum, 
                     COUNT(int_not_null) as count 
              FROM ".$table.$predicate;
      my ($res1, $res2) = $dbh->selectrow_array($query);
      $sum+=$res1;
      $count+=$res2;
    }

    if ($count ne $count_after) {
      say("Bad count when executing query with predicate: \n\t\"$predicate\"\n\tCount: $count; Count for union: $count_after");
      return STATUS_DATABASE_CORRUPTION;
    }

    if ($sum ne $expected_total) {
      say("Bad sum when executing query with predicate: \n\t\"$predicate\"\n\tSum: $sum; Sum for union: $sum_after");
      return STATUS_DATABASE_CORRUPTION;
    }

    say("Query OK with predicate: \"$predicate\"");

    $sum=0;
    $count=0;
  }

  ######################
  # Consistency test 3 #
  ######################

  # Use MySQL consistency testing functions to further exercise the
  # restored objects
  my $engine = $reporter->serverVariable('storage_engine');
  foreach my $table (@$tables) {
    foreach my $sql (
      "CHECK TABLE `$table` EXTENDED",
      "ANALYZE TABLE `$table`",
      "OPTIMIZE TABLE `$table`",
      "REPAIR TABLE `$table` EXTENDED",
      "ALTER TABLE `$table` ENGINE = $engine"
    ) {
      say("Executing $sql.");
      my $sth = $dbh->prepare($sql);
      $sth->execute() if defined $sth;
      print Dumper $sth->fetchall_arrayref() if defined $sth && $sth->{NUM_OF_FIELDS} > 0;
      $sth->finish();
      return STATUS_DATABASE_CORRUPTION if $dbh->err() == 2013;
    }
  }

  say("*** CONSISTENCY CHECKING DONE: ALL CHECKS PASSED ***");

  # Cleanup
  $dbh->do("DROP DATABASE test");

  return STATUS_OK;

}

sub type {
  return REPORTER_TYPE_PERIODIC | REPORTER_TYPE_SUCCESS;
}

1;
