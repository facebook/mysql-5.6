# Copyright (C) 2009 Sun Microsystems, Inc. All rights reserved.
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
# Author: Hema Sridharan
# Date: May 2009
#
# Purpose: Implementation of WL#4732: The test is intended to verify
# the interoperability of MySQL BACKUP / RESTORE operations with other
# server operations.
# This reporter file will execute periodic backup and restore of db1
# while the test is running.
#
#  Associated files are:
#  mysql-test/gentest/conf/backup/backup_obj.yy
#  mysql-test/gentest/conf/backup/backup_interop.yy
#  mysql-test/gentest/lib/Gentest/Reporter/BackupInterop.pm
#
#####################################################################
package GenTest::Reporter::BackupInterop;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use GenTest;
use GenTest::Reporter;
use GenTest::Constants;

my $count = 0;
my $file = '/tmp/inter.bak';

sub monitor 
{
  my $reporter = shift;
  my $dsn = $reporter->dsn();
  
  # Execute BACKUP periodically
  my $dbh = DBI->connect($dsn);

  unlink('/tmp/inter.bak');
  say("Executing BACKUP DATABASE.");
  $dbh->do("BACKUP DATABASE db1 TO '/tmp/inter.bak'");
  $count++;

  if (defined $dbh->err()) 
  {
  return STATUS_DATABASE_CORRUPTION;
  } 
  else 
  {
  return STATUS_OK;
  }
}

my $tables;
sub report 
{
  my $reporter = shift;
  my $dsn = $reporter->dsn();
  my $dbh = DBI->connect($dsn);
  my $results;

  my $query="SELECT COUNT(table_name) FROM INFORMATION_SCHEMA.TABLES
              WHERE TABLE_SCHEMA='db1'";     

#################################################################
# Dump the database db1 that is being backed up by using 
# mysqldump program
#################################################################

our $database = 'db1';
my $file_bak =  '/tmp/db1_dump.bak';
my $file_res= '/tmp/db1_dump_res.bak';
my $mysqldump_result = system("mysqldump --compact --order-by-primary --skip-triggers --skip-extended-insert --no-create-info --host=127.0.0.1 --port=13000 --user=root $database | sort > $file_bak");
 return STATUS_UNKNOWN_ERROR if $mysqldump_result > 0;
  
# PERFORM RESTORE

  say("Executing RESTORE FROM.");
  $dbh->do("RESTORE FROM '/tmp/inter.bak' OVERWRITE");

  if (defined $dbh->err()) 
  {
  return STATUS_DATABASE_CORRUPTION;
  } 
  else 
  {
  return STATUS_OK;
  }

  my $mysqldump_result = system("mysqldump --compact --order-by-primary --skip-triggers --skip-extended-insert --no-create-info --host=localhost --port=13000 --user=root $database | sort > $file_res");
 return STATUS_UNKNOWN_ERROR if $mysqldump_result > 0;

  my $diff_result = system("diff --unified $file_bak $file_res");
  $diff_result = $diff_result >> 8;
  return STATUS_UNKNOWN_ERROR if $diff_result > 1;

  if ($diff_result == 1)
  {
  say("Differences between the files were found after query ".$results->[0]->query());
  return STATUS_UNKNOWN_ERROR;
  }
  else 
  {
  return STATUS_OK;
  }

# Cleanup
  $dbh->do("DROP DATABASE db1");

  return STATUS_OK;

}

sub type 
{
  return REPORTER_TYPE_PERIODIC | REPORTER_TYPE_SUCCESS;
}

1;

