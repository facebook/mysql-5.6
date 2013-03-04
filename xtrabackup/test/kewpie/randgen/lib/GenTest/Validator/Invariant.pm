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
package GenTest::Validator::Invariant;

require Exporter;
@ISA = qw(GenTest::Validator GenTest);

use strict;

use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;

my $tables;
my $dbh;
my $inconsistent_state = 0;

my $total_sum;
my $total_count;

sub validate {
  my ($validator, $executors, $results) = @_;
  my $dsn = $executors->[0]->dsn();

  # RQG does not stop the script if an error occurs.
  # The following code preserves transactional consistency by
  # explicitly rolling back the rest of the statements in a
  # transaction that has failed.
  foreach my $i (0..$#$results) {

    # Additional rules where the transaction must rollback for the
    # invariant script to work
    #
    # "ERROR 1048 (23000): Column 'x' cannot be null" 
    #   '-> used to check that @val!=null (see the delete_update
    #       rule in invariant.yy)

    if ($results->[$i]->err() == 1048) {
      # say("Found transaction that failed due to insert of NULL value");
      $inconsistent_state=1;
    }

    if ($results->[$i]->status() == STATUS_TRANSACTION_ERROR) {
      #say("entering inconsistent state due to query".$results->[$i]->query());
      $inconsistent_state = 1;
    } elsif ($results->[$i]->query() =~ m{^\s*(COMMIT|START TRANSACTION|BEGIN)}sio) {
      if ($inconsistent_state > 0) {
        #say("leaving inconsistent state due to query ".$results->[$i]->query());
      }
      $inconsistent_state = 0;
    }

    if ($inconsistent_state == 1) {
      #say("$$ Rollback ".$results->[$i]->query());
      $executors->[$i]->dbh()->do("ROLLBACK /* Explicit ROLLBACK after a ".$results->[$i]->errstr()." error. */ ");
    } else {
      #say("$$ Execute: ".$results->[$i]->query()." Affected rows: ".$results->[0]->affectedRows());
    }
  }
  # End transactional consistency code

  # Start code that checks that the total amount of money across all
  # bank accounts has not changed
  $dbh = DBI->connect($dsn) if not defined $dbh;
  $tables = $dbh->selectcol_arrayref("SHOW TABLES") if not defined $tables;

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

  #say("The query: ".$query);
  my ($sum, $count) = $dbh->selectrow_array($query);

  if (($sum eq '') && ($count eq '')) {
    # Server probably crashed, the SELECT returned no data
    return STATUS_UNKNOWN_ERROR;
  }

  if (($sum ne '100000')) {
      say("Invariant: Bad sum for tables: sum: $sum; count: $count; affected_rows: ".$results->[0]->affectedRows()."; query: ".$results->[0]->query());
      return STATUS_DATABASE_CORRUPTION;
  }

  return STATUS_OK;
}

1;
