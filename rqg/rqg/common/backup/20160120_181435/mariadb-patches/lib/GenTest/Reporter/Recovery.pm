# Copyright (C) 2008-2009 Sun Microsystems, Inc. All rights reserved.
# Copyright (c) 2013, Monty Program Ab.
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

package GenTest::Reporter::Recovery;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;

use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;
use GenTest::Comparator;
use Data::Dumper;
use IPC::Open3;
use POSIX;

use DBServer::MySQL::MySQLd;

my $first_reporter;

# constructor - set build mysqld mode

sub new {
        my ($class, @args) = @_;
        my $reporter;
        unless (@args) {
        	$reporter=$class->SUPER::new();
        } else {
        	$reporter=$class->SUPER::new(@args);
        }
        $reporter=bless($reporter,$class);
        
        # 0=build cmd mode / 1=clone cmd mode
        $reporter->customAttribute('cloneCmd',0);
        $reporter->persistentProperty('cmd','');
        $reporter->customAttribute('firstMonitor',1);
        $reporter->customAttribute('firstReport',1);
        $reporter->persistentProperty('iKilledTheServer',0);

	return $reporter;
};

# Should we clone the original mysqld command line or
# build a new one?  (the default is to build a new one)

sub cloneCmd {
	my $reporter = shift;
	if (@_) {
		$reporter->customAttribute('cloneCmd',shift);
	}
	return $reporter->customAttribute('cloneCmd');
}

# the cmd line we will restart

sub cmd {
	my $reporter = shift;
	if (@_) {
		$reporter->persistentProperty('cmd',shift);
	}
	return $reporter->persistentProperty('cmd');
}

sub getCmdFromPid() {
	my $reporter = shift;
	my $pid=$reporter->serverInfo('pid');
	my $ret='';
	if (@_) {
		$pid = shift;
	}
	open CMD,"ps fwwo cmd -p $pid|"; # posix 'ps'
	PS_LOOP: while (<CMD>) {
		chomp;
		next PS_LOOP if /^CMD/;
		$ret=$_;
		last PS_LOOP;
	}
	close CMD;
	return $ret;
}

sub monitor {
	my $reporter = shift;

	# check to see if this is the first monitor() call
	my $firstMonitor = $reporter->customAttribute('firstMonitor');
	$reporter->customAttribute('firstMonitor',0);

	# In case of two servers, we will be called twice.
	# Only kill the first server and ignore the second call.
	
	$first_reporter = $reporter if not defined $first_reporter;
	return STATUS_OK if $reporter ne $first_reporter;

	# report our mode if this is the first monitor() call
	if ($firstMonitor) {
		if ($reporter->cloneCmd()) {
			say("Clone Command Mode: I will re-start server with the same parameters");
		} else {
			say("Build Command Mode: we will build the server command");
		}
	}
	
	# get command from the command line if
	# we are in clone mode and haven't 
	# retrieved the command yet.
	#
	my $pid = $reporter->serverInfo('pid');
	my $cmd = $reporter->getCmdFromPid();
	if ($reporter->cloneCmd() &&
	    $reporter->cmd() eq '' &&
	    $cmd gt ''
	) {
	  	$reporter->cmd($cmd);
		say("Recovery->monitor: captured clone cmd: ".$reporter->cmd());
	}

	# it's ok to kill server if we are in the last few seconds of the test.
	# if we are in clone cmd mode,  make sure we have a command to clone.
	#
	my $okToKill=time() > $reporter->testEnd() - 19;
	if ($okToKill && $reporter->cloneCmd()) {
	  $okToKill=$reporter->cmd() gt '';
	}

	# kill it un-gracefully
	#		
	if ($okToKill) {
		say("Sending SIGKILL to server with pid $pid in order to force a recovery.");
		kill(9, $pid);
		$reporter->persistentProperty('iKilledTheServer',1);
		return STATUS_SERVER_KILLED;
	} else {
		return STATUS_OK;
	}
}

sub report {
	my $reporter = shift;
	
        # check to see if this is the first report() call
        my $firstReport = $reporter->customAttribute('firstReport');
        $reporter->customAttribute('firstReport',0);   
        
	#
	# If there is a hang during recovery in one engine, another engine may continue to print
	# periodic diagnostic output forever. This prevents PB2 timeout mechanisms from kicking in
	# In order to avoid that, we set our own crude alarm as a stop-gap measure
	#
	alarm(3600);

	$first_reporter = $reporter if not defined $first_reporter;
	return STATUS_OK if $reporter ne $first_reporter;

	my $binary = $reporter->serverInfo('binary');
	my $language = $reporter->serverVariable('language');
	my $lc_messages_dir = $reporter->serverVariable('lc_messages_dir');
	my $datadir = $reporter->serverVariable('datadir');
	$datadir =~ s{[\\/]$}{}sgio;
	my $recovery_datadir = $datadir.'_recovery';
	my $socket = $reporter->serverVariable('socket');
	my $port = $reporter->serverVariable('port');
	my $pid = $reporter->serverInfo('pid');
	my $old_pid = $pid;
	my $old_pgrp = getpgrp($old_pid);
	my $aria_block_size = $reporter->serverVariable('maria_block_size') || $reporter->serverVariable('aria_block_size');
	my $plugin_dir = $reporter->serverVariable('plugin_dir');
	my $plugins = $reporter->serverPlugins();
	my $binlog_format = $reporter->serverVariable('binlog_format');
	my $binlog_on = $reporter->serverVariable('log_bin');

	my $engine = $reporter->serverVariable('storage_engine');

	my $dbh_prev = DBI->connect($reporter->dsn());

	# capture the server process cmd before killing
	#
	my $cmd=$reporter->getCmdFromPid();
	if ($reporter->cloneCmd() &&
	    $reporter->cmd() eq '' &&
	    $cmd gt ''
	) {
		$reporter->cmd($cmd);
		say("Recovery->report: captured clone cmd: ".$reporter->cmd());
	}

	# only kill if we established a connection
	# if it's clone cmd mode, only kill if we have a cmd
	# and it's the correct pid.
	my $okToKill=defined $dbh_prev;
	if ($okToKill && $reporter->cloneCmd()) {
	  $okToKill=$reporter->cmd() gt '';
	}

	# If we are unable to contact the server the report that.	
	unless (defined $dbh_prev) {
		say("Recovery could not connect to the database");
		# if I didn't kill it, then say so and return an error
		if (! $reporter->persistentProperty('iKilledTheServer')) {
			say("Recovery never issued a KILL signal, the crash occured elsewhere.");
			return STATUS_SERVER_CRASHED;
		}
	}

	if ($okToKill) {
		# Server is still running, kill it.
		$dbh_prev->disconnect();

		say("Sending SIGKILL to server with pid $pid in order to force a recovery.");
		kill(9, $pid);
		$reporter->persistentProperty('iKilledTheServer',1);
		sleep(5);
	} else {
		if (! $reporter->persistentProperty('iKilledTheServer')) {
			say("Recovery did not kill the server");
		}
		if ($reporter->cmd() eq '') {
			say("Recovery did not have server cmd to execute");
		}
	}
	
	say("Copying datadir... (interrupting the copy operation may cause a false recovery failure to be reported below");
	system("cp -r $datadir $recovery_datadir");
	system("rm -f $recovery_datadir/core*");	# Remove cores from any previous crash

	if ($engine =~ m{aria}sio) {
		say("Attempting database recovery using aria_read_log ...");
		my $recovery_datadir_aria = $recovery_datadir.'-aria';

		# Copy just the *aria* files in an empty location and create a test "database"
		system("mkdir $recovery_datadir_aria");
		system("cp $datadir/*aria_log* $recovery_datadir_aria");
		system("mkdir $recovery_datadir_aria/test");
		system("mkdir $recovery_datadir_aria/smf");
		system("mkdir $recovery_datadir_aria/smf2");

		say("Copying complete");

		my $aria_read_log_path = 
		     DBServer::MySQL::MySQLd->_find([$reporter->serverVariable('basedir'),
		     	                             $reporter->serverVariable('basedir').'/..',
		     	                             $reporter->serverVariable('basedir').'/../debug/'],
		     	                            ['storage/maria','bin'],
		     	                            'aria_read_log',
		     	                            'maria_read_log');
		my $aria_chk_path = 
		     DBServer::MySQL::MySQLd->_find([$reporter->serverVariable('basedir'),
		     	                             $reporter->serverVariable('basedir').'/..',
		     	                             $reporter->serverVariable('basedir').'/../debug/'],
		     	                            ['storage/maria','bin'],
		     	                            'aria_chk',
		     	                            'maria_chk');

		chdir($recovery_datadir_aria);

		my $aria_read_log_result = system("$aria_read_log_path --aria-log-dir-path $recovery_datadir_aria --apply --check --silent");
		return STATUS_RECOVERY_FAILURE if $aria_read_log_result > 0;
		say("$aria_read_log_path apparently returned success");

		my $aria_chk_result = system("$aria_chk_path --extend-check --silent */*.MAI");
		return STATUS_RECOVERY_FAILURE if $aria_chk_result > 0;
		say("$aria_chk_path apparently returned success");
	} else {
		say("Copying complete");
	}

	say("Attempting database recovery using the server ...");

	my $mysqld_command='';
	
	if (!$reporter->cloneCmd()) {
	
		my @mysqld_options = (
			'--no-defaults',
			'--core-file',
			'--loose-console',
			'--loose-maria-block-size='.$aria_block_size,
			'--loose-aria-block-size='.$aria_block_size,
			'--language='.$language,
			'--loose-lc-messages-dir='.$lc_messages_dir,
			'--datadir="'.$recovery_datadir.'"',
			'--log-output=file',
			'--general_log=ON',
			'--general_log_file="'.$recovery_datadir.'/query-recovery.log"',
			'--datadir="'.$recovery_datadir.'"',
			'--socket="'.$socket.'"',
			'--port='.$port,
			'--loose-plugin-dir='.$plugin_dir
		);

		if ($binlog_on =~ m{YES|ON}sio) {
			push @mysqld_options, (
				'--log-bin',
				'--binlog-format='.$binlog_format,
				'--log-bin='.$datadir.'/../log/master-bin',
				'--server-id=1'
			);
		};

		push @mysqld_options, '--loose-skip-pbxt' if lc($engine) ne 'pbxt';

		foreach my $plugin (@$plugins) {
			push @mysqld_options, '--plugin-load='.$plugin->[0].'='.$plugin->[1];
		};
		
		$mysqld_command = $binary.' '.join(' ', @mysqld_options);
		

	} else {

		# get the previous command to clone
		$mysqld_command = $reporter->cmd();

		# mysqld command fixup for plugin-load
		$mysqld_command =~ s/(--plugin-load=)([^\s]+)(\s)/$1'$2'$3/;
	}
	
	# If we don't have a command to restart at this point then report
	# that as an error and level
	#
	if ($mysqld_command eq '') {
		say("Recovery does not have a server command to restart.");
		return STATUS_UNKNOWN_ERROR;
	}

	# Weirdness after call to open3()
	# setting autoflush on seems to
	# fix the issue.
	#
	my $holdAutoFlush=$|++;

	say("Restarting server: $mysqld_command");

	$pid = open3(*MYSQLD_IN,*MYSQLD_OUT,undef,$mysqld_command);

	# replace pid for this run
	setpgrp($pid,$old_pgrp);	
	my $pid_file = $reporter->serverVariable('pid_file');
	open (PF, ">".$pid_file);
	print PF $pid;
	close(PF);
	$reporter->[GenTest::Reporter::REPORTER_SERVER_INFO]->{pid} = $pid;

	say("New server pid: $pid");
		
	#
	# Phase1 - the server is running single-threaded. We consume the error log and parse it for
	# statements that indicate failed recovery
	# 

	my $recovery_status = STATUS_OK;
	while (<MYSQLD_OUT>) {
		$_ =~ s{[\r\n]}{}siog;
		say($_);
		if ($_ =~ m{registration as a STORAGE ENGINE failed.}sio) {
			say("Storage engine registration failed");
			$recovery_status = STATUS_DATABASE_CORRUPTION;
		} elsif ($_ =~ m{corrupt|crashed} && $_ !~ m{unknown option}) {
			say("Log message '$_' indicates database corruption");
			$recovery_status = STATUS_DATABASE_CORRUPTION;
		} elsif ($_ =~ m{exception}sio) {
			$recovery_status = STATUS_DATABASE_CORRUPTION;
		} elsif ($_ =~ m{ready for connections}sio) {
			say("Server Recovery was apparently successful.") if $recovery_status == STATUS_OK ;
			last;
		} elsif ($_ =~ m{device full error|no space left on device}sio) {
			$recovery_status = STATUS_ENVIRONMENT_FAILURE;
			last;
		} elsif (
			($_ =~ m{got signal}sio) ||
			($_ =~ m{segfault}sio) ||
			($_ =~ m{segmentation fault}sio)
		) {
			say("Recovery has apparently crashed.");
			$recovery_status = STATUS_DATABASE_CORRUPTION;
		}
	}
	
	say("Recovery status: $recovery_status");

	my $dbh = DBI->connect($reporter->dsn());
	$recovery_status = STATUS_DATABASE_CORRUPTION if not defined $dbh && $recovery_status == STATUS_OK;

	if ($recovery_status > STATUS_OK) {
		say("Recovery has failed.");
		return $recovery_status;
	}
	
	# 
	# Phase 2 - server is now running, so we execute various statements in order to verify table consistency
	# However, while we do that, we are still responsible for processing the error log and dumping it to our stdout.
	# If we do not do that, and the server calls flish(stdout) , it will hang waiting for us to consume its stdout, which
	# we would no longer be doing. So, we call eater(), which forks a separate process to read the log and dump it to stdout.
	#

	say("Testing database consistency");

	my $eater_pid = eater(*MYSQLD_OUT);
	
	my $databases = $dbh->selectcol_arrayref("SHOW DATABASES");
	foreach my $database (@$databases) {
		next if $database =~ m{^(mysql|information_schema|pbxt|performance_schema)$}sio;
		$dbh->do("USE $database");
		my $tables = $dbh->selectcol_arrayref("SHOW TABLES");
		foreach my $table (@$tables) {
			say("Verifying table: $table; database: $database");

			my $sth_keys = $dbh->prepare("
				SHOW KEYS FROM `$database`.`$table`
			");

			$sth_keys->execute();

			my @walk_queries;

			while (my $key_hashref = $sth_keys->fetchrow_hashref()) {
				my $key_name = $key_hashref->{Key_name};
				my $column_name = $key_hashref->{Column_name};

				foreach my $select_type ('*' , "`$column_name`") {
					my $main_predicate;
					if ($column_name =~ m{int}sio) {
						$main_predicate = "WHERE `$column_name` >= -9223372036854775808";
					} elsif ($column_name =~ m{char}sio) {
						$main_predicate = "WHERE `$column_name` = '' OR `$column_name` != ''";
					} elsif ($column_name =~ m{date}sio) {
						$main_predicate = "WHERE (`$column_name` >= '1900-01-01' OR `$column_name` = '0000-00-00') ";
					} elsif ($column_name =~ m{time}sio) {
						$main_predicate = "WHERE (`$column_name` >= '-838:59:59' OR `$column_name` = '00:00:00') ";
					} else {
						next;
					}
	
					if ($key_hashref->{Null} eq 'YES') {
						$main_predicate = $main_predicate." OR `$column_name` IS NULL OR `$column_name` IS NOT NULL";
					}
			
					push @walk_queries, "SELECT $select_type FROM `$database`.`$table` FORCE INDEX ($key_name) ".$main_predicate;
				}
                        };

			my %rows;
			my %data;

			foreach my $walk_query (@walk_queries) {
				my $sth_rows = $dbh->prepare($walk_query);
				$sth_rows->execute();

				if (defined $sth_rows->err()) {
					say("Failing query is $walk_query.");
					return STATUS_RECOVERY_FAILURE;
				}

				my $rows = $sth_rows->rows();
				$sth_rows->finish();

				push @{$rows{$rows}} , $walk_query;
			}

			if (keys %rows > 1) {
				say("Table `$database`.`$table` is inconsistent.");
				print Dumper \%rows;

				my @rows_sorted = grep { $_ > 0 } sort keys %rows;
			
				my $least_sql = $rows{$rows_sorted[0]}->[0];
				my $most_sql  = $rows{$rows_sorted[$#rows_sorted]}->[0];
			
				say("Query that returned least rows: $least_sql\n");
				say("Query that returned most rows: $most_sql\n");
	
				my $least_result_obj = GenTest::Result->new(
					data => $dbh->selectall_arrayref($least_sql)
				);
				
				my $most_result_obj = GenTest::Result->new(
					data => $dbh->selectall_arrayref($most_sql)
				);

				say(GenTest::Comparator::dumpDiff($least_result_obj, $most_result_obj));

				$recovery_status = STATUS_DATABASE_CORRUPTION;
			}

			foreach my $sql (
				"CHECK TABLE `$database`.`$table` EXTENDED",
				"ANALYZE TABLE `$database`.`$table`",
				"OPTIMIZE TABLE `$database`.`$table`",
				"REPAIR TABLE `$database`.`$table` EXTENDED",
				"ALTER TABLE `$database`.`$table` ENGINE = $engine"
			) {
				say("Executing $sql.");
				my $sth = $dbh->prepare($sql);
				if (defined $sth) {
					$sth->execute();

					return STATUS_DATABASE_CORRUPTION if $dbh->err() > 0 && $dbh->err() != 1178;
					if ($sth->{NUM_OF_FIELDS} > 0) {
						my $result = Dumper($sth->fetchall_arrayref());
						next if $result =~ m{is not BASE TABLE}sio;	# Do not process VIEWs
						if ($result =~ m{error'|corrupt|repaired|invalid|crashed}sio) {
							print $result;
							return STATUS_DATABASE_CORRUPTION
						}
					};

					$sth->finish();
				} else {
					say("Prepare failed: ".$dbh->errrstr());
					return STATUS_DATABASE_CORRUPTION;
				}
			}
		}
	}

	# 
	# We reap the process we spawned to consume the server error log
	# If there have been any log messages such as "table X is marked as crashed", then
	# the eater process will exit with STATUS_RECOVERY_FAILURE which we will pass to the caller.

	waitpid($eater_pid, &POSIX::WNOHANG());
	my $eater_status = $?;
	if ($eater_status > -1) {
		$recovery_status = $eater_status >> 8 if $eater_status > $recovery_status;
	}

	if ($recovery_status > STATUS_OK) {
		say("Recovery has failed.");
		return $recovery_status;
	} else {
		return STATUS_OK;
	}
	
}

sub eater {
	my $fh = shift;
	if (my $eater_pid = fork()) {
		# parent
		return $eater_pid;
	} else {
		# child
		$0 = 'Recovery log eater';
		while (<$fh>) {
			$_ =~ s{[\r\n]}{}siog;
			say($_);
			if ($_ =~ m{corrupt|repaired|invalid|crashed}sio) {
				say ("Server stderr line '$_' indicates recovery failure.");
				exit(STATUS_RECOVERY_FAILURE);
			}
		}

		exit(STATUS_OK);
	}
}

sub type {
	return REPORTER_TYPE_ALWAYS | REPORTER_TYPE_PERIODIC;
}

1;
