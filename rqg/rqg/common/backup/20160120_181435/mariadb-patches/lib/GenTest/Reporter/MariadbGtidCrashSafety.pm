# Copyright (C) 2013 Monty Program Ab
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

####################################################################
# ATTENTION! The reporter works with *MariaDB* implementation of GTID.
####################################################################
# The reporter makes the slave server crash every 30 seconds,
# restarts it and checks that it started all right. 
# If it's used alone, it can catch errors that do not allow
# slave to restart properly (e.g. if it crashed or if the replication aborted).
# Besides, it performs a basic sanity check on GTID seq_no behavior, saving 
# the previous seq_no and verifying that the next one is greater or equal
# the stored value.
# 
# If used in conjunction with ReplicationConsistency reporter,
# the correctness of the data after all the crashes will also be checked
# at the end of the test.
####################################################################

package GenTest::Reporter::MariadbGtidCrashSafety;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;
use GenTest::Comparator;
use Data::Dumper;
use IPC::Open2;
use File::Copy;
use POSIX;

use DBServer::MySQL::MySQLd;

my $first_reporter;
my $last_crash_time;
my $slave_dsn;
my %gtid_prev_seq_no = ();
my $dbh;
my $restart_count = 0;
my $crash_interval = 30; # Time interval between slave crashes; also used as a slave connect timeout

sub monitor {
	my $reporter = shift;

	# In case of two or more main servers, we will be called more than once.
	# Ignore all but the first call.
	
	$first_reporter = $reporter if not defined $first_reporter;
	return STATUS_OK if $reporter ne $first_reporter;

	if (!$slave_dsn) {
		# Running for the first time
		my $slave_host = $reporter->serverInfo('slave_host');
		my $slave_port = $reporter->serverInfo('slave_port');
		$slave_dsn = 'dbi:mysql:host='.$slave_host.':port='.$slave_port.':user=root';
		$reporter->properties->servers->[1]->addServerOptions(['--skip-slave-start']);
	}

	$dbh = DBI->connect($slave_dsn);
	my $gtid_status = check_gtid_status('');
	return $gtid_status if $gtid_status != STATUS_OK;

	$last_crash_time = $reporter->testStart() if not defined $last_crash_time;

	if (time() > $last_crash_time + $crash_interval) {
		$last_crash_time = time();
		my $pid = $reporter->properties->servers->[1]->serverpid();
		say("Sending SIGKILL to server with pid $pid in order to force crash recovery");
		kill(9, $pid);
		sleep(2);
		return restart($reporter);
	} else {
		return STATUS_OK;
	}
}

sub check_gtid_status {
	my $log = shift;
	$log = ($log ? ' '.$log : '').':';
	my @gtid_status = $dbh->selectrow_array(
		'SELECT GROUP_CONCAT(domain_id,"-",server_id,"-",seq_no separator ","), @@gtid_current_pos, @@gtid_slave_pos, @@gtid_binlog_pos, MAX(seq_no) '
		.'FROM mysql.gtid_slave_pos WHERE (domain_id, sub_id) IN (SELECT domain_id, MAX(sub_id) FROM mysql.gtid_slave_pos GROUP BY domain_id)'
	);

	say("Slave GTID status". $log. "\n"
		."  table mysql.gtid_slave_pos: $gtid_status[0]\n"
		."  gtid_current_pos: $gtid_status[1]\n"
		."  gtid_slave_pos:   $gtid_status[2]\n"
		."  gtid_binlog_pos:  $gtid_status[3]"
	);
	if (check_seq_no($gtid_status[1]) != STATUS_OK) {
		say("Stopping replication, the test will continue without this reporter");
		$dbh->do("STOP SLAVE");
		return STATUS_REPLICATION_FAILURE;
	} 
	return STATUS_OK;
}	

sub check_seq_no {
	my $curpos = shift;
	my @cur_pos = split /,/, $curpos;
	foreach (@cur_pos) {
		if (/(\d+)-\d+-(\d+)/) {
			if ( $2 < $gtid_prev_seq_no{$1} ) {
				say("ERROR: for domain $1 current seq_no $2 is less than the previously stored seq_no $gtid_prev_seq_no{$1}");
				return STATUS_REPLICATION_FAILURE;
			}
			$gtid_prev_seq_no{$1} = $2;
		}
	}
	return STATUS_OK;
}


sub report {
	return STATUS_OK;
}

sub restart {
	my $reporter = shift;

	$first_reporter = $reporter if not defined $first_reporter;
	return STATUS_OK if $reporter ne $first_reporter;

	my $master_port = $reporter->properties->servers->[0]->port();
	my $server = $reporter->properties->servers->[1];
	$server->setStartDirty(1);
	my $errlog = $server->errorlog();
	move($errlog,"$errlog.$restart_count"); # Rotate error log

	say("Trying to restart the server ...");
	my $restart_status = $server->startServer();
	say("Restart failed") && return $restart_status if $restart_status != STATUS_OK;

	open(RESTART, $errlog);

	while (<RESTART>) {
		$_ =~ s{[\r\n]}{}siog;
		if ($_ =~ m{registration as a STORAGE ENGINE failed.}sio) {
			say("Storage engine registration failed");
			$restart_status = STATUS_DATABASE_CORRUPTION;
		} elsif ($_ =~ m{exception}sio) {
			say("Exception was caught");
			$restart_status = STATUS_DATABASE_CORRUPTION;
		} elsif ($_ =~ m{ready for connections}sio) {
			# Server thinks it has started

			$dbh = DBI->connect($slave_dsn);
			if (defined $dbh) {
				$restart_status = check_gtid_status("after server restart");
				last if $restart_status != STATUS_OK;
				say("Server restart was apparently successfull, running CHANGE MASTER...");

				$dbh->do("CHANGE MASTER TO ".
					" MASTER_PORT = $master_port,".
					" MASTER_HOST = '127.0.0.1',".
					" MASTER_USER = 'root',".
					" MASTER_USE_GTID = current_pos,".
					" MASTER_CONNECT_RETRY = 1");

				$restart_status = check_gtid_status("after CHANGE MASTER");
				last if $restart_status != STATUS_OK;
				say("Starting replication...");
				$dbh->do("START SLAVE");
				my ($sql_thread, $io_thread, $sql_error, $io_error);
				foreach (1..$crash_interval) {
					sleep(1);
		     		my @slave_status = $dbh->selectrow_array("SHOW SLAVE STATUS");
		     		($sql_thread, $io_thread, $sql_error, $io_error) = ($slave_status[11], $slave_status[10], $slave_status[37], $slave_status[35]);
					say("Current replication status: IO thread $io_thread, SQL thread $sql_thread");
					last if ( ( $sql_thread eq 'Yes' and $io_thread eq 'Yes' ) or ( $sql_thread eq 'No' and $sql_error ) or ( $io_thread eq 'No' and $io_error ) );
				}

				if ( $sql_thread ne 'Yes' or $io_thread ne 'Yes' ) {
					say("Slave start failed. SQL thread: status [$sql_thread], error [$sql_error]; IO thread: status [$io_thread], error [$io_error]");
					$restart_status = STATUS_REPLICATION_FAILURE;
					last;
				}

				$restart_status = check_gtid_status("after START SLAVE");
				last;

			} else { 
				say("Could not connect to the slave after restart");
				$restart_status = STATUS_REPLICATION_FAILURE;
			}
			last;
		} elsif ($_ =~ m{device full error|no space left on device}sio) {
			say("No space left on device");
			$restart_status = STATUS_ENVIRONMENT_FAILURE;
			last;
		} elsif ($_ =~ m{slave SQL thread aborted|slave IO thread aborted}sio) {
			say("Replication aborted");
			$restart_status = STATUS_REPLICATION_FAILURE;
			last;
		} elsif (
			($_ =~ m{got signal}sio) ||
			($_ =~ m{segfault}sio) ||
			($_ =~ m{segmentation fault}sio)
		) {
			say("Restarting server has apparently crashed.");
			$restart_status = STATUS_SERVER_CRASHED;
			last;
		}
	}
	close(RESTART);

	$restart_count++;

	if ($restart_status != STATUS_OK) {
		say("Slave restart has failed with status " . status2text($restart_status));
		return $restart_status;
	}

	return STATUS_OK;
}

sub type {
	return REPORTER_TYPE_PERIODIC ;
}

1;
