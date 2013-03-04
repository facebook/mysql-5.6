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

package GenTest::Reporter::DrizzleInnoTrxLogCrashRecovery;

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
my $first_reporter;

sub monitor {
	my $reporter = shift;

	# In case of two servers, we will be called twice.
	# Only kill the first server and ignore the second call.
	
	$first_reporter = $reporter if not defined $first_reporter;
	return STATUS_OK if $reporter ne $first_reporter;
        
        #my $dbh = DBI->connect($reporter->dsn(), undef, undef, {PrintError => 0});
        my $pid = $ENV{'BOT0_S0_PID'};
        my $time = time();
        my $testEnd = $reporter->testEnd();
        say("time:  $time");
        say("testEnd:  $testEnd");
	if (time() > $reporter->testEnd() - 3500) 
        {
                #my $ps_result = system("ps -al");
                #say ("$ps_result");
		say("Sending kill -9 to server pid $pid in order to force a recovery.");
		kill(9, $pid);
                #my $ps_result = system("ps -al");
                #say ("$ps_result");
		return STATUS_SERVER_KILLED;
	} 
        else 
        {
		return STATUS_OK;
	}
}


sub report 
  {
        my $reporter = shift;

        $first_reporter = $reporter if not defined $first_reporter;
	return STATUS_OK if $reporter ne $first_reporter;


        my $main_port;
        my $validator_port;
        my $basedir;
        
        # do some setup and whatnot
        if (exists $ENV{'MASTER_MYPORT'})
        {
            $main_port = $ENV{'MASTER_MYPORT'}
        }
        else
        {
            $main_port = '9306';
        }
        if (exists $ENV{'BOT0_S1'})
        {
            $validator_port = $ENV{'BOT0_S1'}
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

        # Server restart razzle-dazzle
        my $binary = $basedir.'/drizzled/drizzled' ;
        my $datadir = $reporter->serverVariable('datadir');

	

	my $dbh_prev = DBI->connect($reporter->dsn());

	if (defined $dbh_prev) {
		# Server is still running, kill it.
		$dbh_prev->disconnect();
                my $time = time();
                my $testEnd = $reporter->testEnd();
                say("time:  $time");
                say("testEnd:  $testEnd");
		say("Sending shutdown() call to server.");
                my $ps_result = system("ps -al");
                say ("$ps_result");
                $dbh_prev->selectrow_array('SELECT SHUTDOWN()');
		sleep(5);
	}

        	say("Attempting database recovery using the server ...");

	my @drizzled_options = (
		'--no-defaults',
		'--core-file',	
		'--datadir="'.$datadir.'"',
                '--basedir="'.$basedir.'"',
                '--plugin-add=shutdown_function',
                '--innodb.replication-log',
		'--mysql-protocol.port='.$main_port,

	);

	my $drizzled_command = $binary.' '.join(' ', @drizzled_options).' 2>&1';
	say("Executing $drizzled_command .");

	my $drizzled_pid = open2(\*RDRFH, \*WTRFH, $drizzled_command);
        say("$drizzled_pid");

	#
	# Phase1 - the server is running single-threaded. We consume the error log and parse it for
	# statements that indicate failed recovery
	# 

	my $recovery_status = STATUS_OK;
	
        sleep(5);
	my $dbh = DBI->connect($reporter->dsn());
	$recovery_status = STATUS_DATABASE_CORRUPTION if not defined $dbh && $recovery_status == STATUS_OK;

	if ($recovery_status > STATUS_OK) {
		say("Recovery has failed.");
		return $recovery_status;
	}

        # We now attempt to replicate from the transaction log
        # We call transaction_reader and send the output
        # via the drizzle client to the validation server (slave)
        my $transaction_log_sql_file = tmpdir()."/translog_".$$."_.sql" ;
        say("transaction_log output file:  $transaction_log_sql_file");
        my $trx_reader_cmd = "$transaction_reader $transaction_log > $transaction_log_sql_file";
        my $trx_reader_cmd = "$transaction_reader -uroot --use-innodb-replication-log -p $main_port --ignore-events > $transaction_log_sql_file";
        say("$trx_reader_cmd");
        system($trx_reader_cmd) ;

        say("Replicating from transaction_log output...");
        my $rpl_command = "$drizzle_client --host=127.0.0.1 --port=$validator_port --user=root test <  $transaction_log_sql_file";
        say ("$rpl_command");
        my $drizzle_rpl_result = system($rpl_command) ;
        return STATUS_UNKNOWN_ERROR if $drizzle_rpl_result > 0 ;

          
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
         say ("Cleaning up validation server...");
         system("$drizzle_client --host=127.0.0.1 --port=$validator_port --user=root -e 'DROP SCHEMA test'");

         say ("Resetting validation server...");
         my $create_schema_result = system("$drizzle_client --host=127.0.0.1 --port=$validator_port --user=root -e 'CREATE SCHEMA test'");
         say("$create_schema_result");      

	 return STATUS_UNKNOWN_ERROR if $diff_result > 1;

	 if ($diff_result == 1) 
         {
	   say("Differences between the two servers were found after comparing dumpfiles");
           say("diff command:  diff --unified $files[SERVER1_FILE_NAME] $files[SERVER2_FILE_NAME]");
           say("Master dumpfile:  $files[SERVER1_FILE_NAME]");
           say("Slave dumpfile:   $files[SERVER2_FILE_NAME]");
           say("transaction_log output file:  $transaction_log_sql_file");
           say("Transaction log:  $transaction_log_copy");
	   return STATUS_REPLICATION_FAILURE;
	 } 
         else 
         {
	   foreach my $file (@files) 
           {
	     unlink($file);
	   }
           unlink($transaction_log_sql_file);
           unlink($transaction_log_copy);
	   return STATUS_OK;
	 }

   }	
	
 

sub type {
	return REPORTER_TYPE_ALWAYS | REPORTER_TYPE_PERIODIC;
}

1;
