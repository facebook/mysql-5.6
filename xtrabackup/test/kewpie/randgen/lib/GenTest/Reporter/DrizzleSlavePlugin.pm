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

package GenTest::Reporter::DrizzleSlavePlugin;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;
use Data::Dumper;
use IPC::Open2;
use IPC::Open3;
use File::Copy;

use constant SERVER1_FILE_NAME  => 0;
use constant SERVER2_FILE_NAME  => 1;

sub report 
  {
	my $reporter = shift;

        
        # do some setup and whatnot
        my $main_port;
	my $validator_port;
        my $basedir;

        if (exists $ENV{'MASTER_MYPORT'})
        {
            $main_port = $ENV{'MASTER_MYPORT'};
        }
        else
        {
            $main_port = '9306';
        }
        if (exists $ENV{'BOT0_S1'})
        {
            $validator_port = $ENV{'BOT0_S1'};
        }
        else
        {
	    $validator_port = '9307';
        }
        if (exists $ENV{'DRIZZLE_BASEDIR'})
        {
            $basedir = $ENV{'DRIZZLE_BASEDIR'};
        }
        else
        {
            $basedir= $reporter->serverVariable('basedir');
        }
        my $drizzledump = $basedir.'/client/drizzledump' ;
        my $drizzle_client = $basedir.'/client/drizzle' ;
        my $transaction_reader; 
        if (exists $ENV{'DRIZZLE_TRX_READER'})
        {
            $transaction_reader = $ENV{'DRIZZLE_TRX_READER'}
        } 
        elsif (-e $basedir.'/drizzled/message/transaction_reader')
        {
            $transaction_reader = $basedir.'/drizzled/message/transaction_reader';
        }
        else 
        {
            $transaction_reader = $basedir.'/plugin/transaction_log/utilities/drizzletrx' ;
        }

        # transaction log location can vary depending on how we start the server
        # we really only account for test-run and drizzle-automation starts
        my $transaction_log = '';
        if (-e $basedir.'/var/local/transaction.log')
        {
          $transaction_log = $basedir.'/var/local/transaction.log' ;
        }
        elsif (-e $basedir.'/tests/workdir/bot0/s0/var/master-data/local/transaction.log')
        {
          $transaction_log = $basedir.'/tests/workdir/bot0/s0/var/master-data/local/transaction.log' ;
        }
        else
        {
          $transaction_log = $basedir.'/tests/var/master-data/local/transaction.log' ;
        }
        my $transaction_log_copy = tmpdir()."/translog_".$$."_.log" ;
        copy($transaction_log, $transaction_log_copy);

        say("Waiting for slave to catch up...");
        #sleep 60;
        my $slave_dsn="dbi:drizzle:host=localhost:port=$validator_port:user=root:password='':database=sys_replication";
        my $slave_dbh = DBI->connect($slave_dsn, undef, undef, {PrintError => 0});
        my $master_dbh = DBI->connect($reporter->dsn(), undef, undef, {PrintError => 0});
        my $not_done = 1;
        while ($not_done)
        {
            my @max_slave_id_res = $slave_dbh->selectrow_array('SELECT last_applied_commit_id from applier_state');
            my $max_slave_id = @max_slave_id_res->[0] ;
            my @max_master_id_res = $master_dbh->selectrow_arrayref("SELECT MAX(commit_id) from DATA_DICTIONARY.SYS_REPLICATION_LOG");
            my $max_master_id = @max_master_id_res->[0]->[0] ;
            if ($max_slave_id == $max_master_id)
            {
                $not_done = 0 ;
            }
            sleep 1;
            #say ("$max_slave_id");
            #say ("$max_master_id");
            #$not_done = 0;
        }
        say("Validating replication via dumpfile compare...");
        my @files;
        my @ports = ($main_port, $validator_port);

        foreach my $port_id (0..1) 
          {
            $files[$port_id] = tmpdir()."/translog_rpl_dump_".$$."_".$ports[$port_id].".sql";
            say("$files[$port_id]");
            say("$drizzledump --compact --skip-extended-insert --host=127.0.0.1 --port=$ports[$port_id] --user=root test >$files[$port_id]");
	    my $drizzledump_result = system("$drizzledump --compact --skip-extended-insert --host=127.0.0.1 --port=$ports[$port_id] --user=root test >$files[$port_id]");
            # disable pipe to 'sort' from drizzledump call above
            #| sort > $files[$port_id]");
	    return STATUS_UNKNOWN_ERROR if $drizzledump_result > 0;
	  }
         say ("Executing diff --unified $files[SERVER1_FILE_NAME] $files[SERVER2_FILE_NAME]");
         my $diff_result = system("diff --unified $files[SERVER1_FILE_NAME] $files[SERVER2_FILE_NAME]");
	 $diff_result = $diff_result >> 8;
       

	 return STATUS_UNKNOWN_ERROR if $diff_result > 1;

	 if ($diff_result == 1) 
         {
	   say("Differences between the two servers were found after comparing dumpfiles");
           say("diff command:  diff --unified $files[SERVER1_FILE_NAME] $files[SERVER2_FILE_NAME]");
           say("Master dumpfile:  $files[SERVER1_FILE_NAME]");
           say("Slave dumpfile:   $files[SERVER2_FILE_NAME]");
           #say ("$max_slave_id");
           #say ("$max_master_id");
	   return STATUS_REPLICATION_FAILURE;
	 } 
         else 
         {
	   foreach my $file (@files) 
           {
	     unlink($file);
	   }
	   return STATUS_OK;
	 }

   }	
	
 

sub type {
	return REPORTER_TYPE_ALWAYS;
}

1;
