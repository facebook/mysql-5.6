# Copyright (C) 2010 Patrick Crews. All rights reserved.
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

# DrizzledumpMigrate
# Validator for testing drizzledump client's ability to migrate MySQL
# databases to Drizzle
# This requires a MySQL database, a running Drizzle validation server
# with some values hard-coded below (sorry).
# The grammar is conf/drizzle/drizzledump_migrate.yy
# The gendata files are conf/drizzle/drizzledump_migrate.zz
#                       conf/drizzle/drizzledump_migrate_no_blobs.zz
# This is intended for single-threaded scenarios

package GenTest::Validator::DrizzledumpMigrate;

require Exporter;
@ISA = qw(GenTest GenTest::Validator);

use constant SERVER1_FILE_NAME  => 0;
use constant SERVER2_FILE_NAME  => 1;

use strict;

use Data::Dumper;
use GenTest;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;

sub validate {
	my ($validator, $executors, $results) = @_;
        my $fail_count ;
        my $total_count ;
        my $query_value = $results->[0]->[0] ;
        my $mysql_port = '19300';
        say("$query_value");
        if ($query_value eq ' SELECT 1')
        {
          # do some setup and whatnot
          
          # info for MySQL db
	  my $database = 'drizzledump_db' ;
          my @basedir = $executors->[0]->dbh()->selectrow_array('SELECT @@basedir') ;
          # little kludge to get the proper basedir if MySQL was started via 
          #       lib/v1/mysql-test-run.pl --start-and-exit
          # such a situation sets basedir to the drizzle/tests directory and can
          # muck up efforts to get to the client directory
          my @basedir_split = split(/\//, @basedir->[0]) ;
          if (@basedir_split[-1] eq 'mysql-test')
          {
            pop(@basedir_split); 
            @basedir = join('/',@basedir_split);
          }

          # info for Drizzle validation db
          my $drizzle_port = '9306';
          my $drizzle_dsn="dbi:drizzle:host=localhost:port=$drizzle_port:user=root:password='':database=test";
          my $drizzle_dbh = DBI->connect($drizzle_dsn, undef, undef, {PrintError => 0});
          my @drizzle_basedir = $drizzle_dbh->selectrow_array('SELECT @@basedir');
          my $drizzledump = @drizzle_basedir->[0].'/client/drizzledump' ;
          my $drizzle_client = @drizzle_basedir->[0].'/client/drizzle' ;
          if (rqg_debug())
          {
            say ("Cleaning up validation server...");
          }
          system("$drizzle_client --host=127.0.0.1 --port=$drizzle_port --user=root -e 'DROP SCHEMA $database'");

          if (rqg_debug())
          {
            say ("Resetting validation server...");
          }
          system("$drizzle_client --host=127.0.0.1 --port=$drizzle_port --user=root -e 'CREATE SCHEMA $database'");

          if (rqg_debug()) 
          {
            say("Preparing to migrate MySQL database via drizzledump...");
          }
          
          # call to drizzledump / migrate
	  my $drizzledump_result = system("$drizzledump --compact --host=127.0.0.1 --port=$mysql_port --destination-type=database --destination-host=localhost --destination-port=$drizzle_port --destination-user=root --destination-database=$database --user=root $database ") ;
          say("$drizzledump --compact --host=127.0.0.1 --port=$mysql_port --destination-type=database --destination-host=localhost --destination-port=$drizzle_port --destination-user=root --destination-database=$database --user=root $database ");
          say("$drizzledump_result");
	  return STATUS_UNKNOWN_ERROR if $drizzledump_result > 0 ;

          # dump original + migrated DB's and compare dumpfiles
          my @files;
	  my @ports = ($mysql_port, $drizzle_port);

	  foreach my $port_id (0..1) 
          {
	    $files[$port_id] = tmpdir()."/translog_rpl_dump_".$$."_".$ports[$port_id].".sql";
            say("$files[$port_id]");
	    my $drizzledump_result = system("$drizzledump --compact --skip-extended-insert --host=127.0.0.1 --port=$ports[$port_id] --user=root $database >$files[$port_id]");
            # disable pipe to 'sort' from drizzledump call above
            #| sort > $files[$port_id]");
	    return STATUS_UNKNOWN_ERROR if $drizzledump_result > 0;
	  }
          if (rqg_debug())
          {
            say ("Executing diff --unified $files[SERVER1_FILE_NAME] $files[SERVER2_FILE_NAME]");
	  }
          my $diff_result = system("diff --unified $files[SERVER1_FILE_NAME] $files[SERVER2_FILE_NAME]");
	  $diff_result = $diff_result >> 8;
         

	  return STATUS_UNKNOWN_ERROR if $diff_result > 1;

	  if ($diff_result == 1) 
          {
	    say("Differences between the two servers were found after comparing dumpfiles");
            say("diff command:  diff --unified $files[SERVER1_FILE_NAME] $files[SERVER2_FILE_NAME]");
            say("Master dumpfile:  $files[SERVER1_FILE_NAME]");
            say("Slave dumpfile:   $files[SERVER2_FILE_NAME]");
	    return STATUS_REPLICATION_FAILURE;
	  } 
          else 
          {
	    #foreach my $file (@files) 
            #{
	    #  unlink($file);
	    #}
	    return STATUS_OK;
	  }        

  }

return STATUS_OK ;
}


1;
