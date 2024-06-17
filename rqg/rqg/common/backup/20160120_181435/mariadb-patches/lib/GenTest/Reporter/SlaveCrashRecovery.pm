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


###################################################################
# The reporter makes the slave server crash every 30 seconds,
# restarts it and checks that it started all right. 
# If it's used alone, it can catch errors that do not allow
# slave to restart properly (e.g. if it crashed or if the replication aborted).
# If used in conjunction with ReplicationConsistency reporter,
# the correctness of the data after all the crashes will also be checked
# at the end of the test.
###################################################################

package GenTest::Reporter::SlaveCrashRecovery;

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
my $restart_count = 0;

sub monitor {
	my $reporter = shift;

	$first_reporter = $reporter if not defined $first_reporter;
	return STATUS_OK if $reporter ne $first_reporter;

	$last_crash_time = $reporter->testStart() if not defined $last_crash_time;

	if (time() > $last_crash_time + 30) {
		$last_crash_time = time();
		my $pid = $reporter->properties->servers->[1]->serverpid();
		say("Sending SIGKILL to server with pid $pid in order to force a crash recovery");
		kill(9, $pid);
		sleep(3);
		return restart($reporter);
	} else {
		return STATUS_OK;
	}
}

sub report {
	return STATUS_OK;
}

sub restart {
	my $reporter = shift;

	alarm(3600);

	$first_reporter = $reporter if not defined $first_reporter;
	return STATUS_OK if $reporter ne $first_reporter;

	my $dbh_prev = DBI->connect($reporter->dsn());

	if (defined $dbh_prev) {
		$dbh_prev->disconnect();
	}

	my $server = $reporter->properties->servers->[1];
	$server->setStartDirty(1);

	say("Trying to restart the server ...");

	my $errlog = $server->errorlog();
	move($errlog,"$errlog.$restart_count");

	my $restart_status = $server->startServer();

	open(RESTART, $errlog);
	while (<RESTART>) {
		$_ =~ s{[\r\n]}{}siog;
#		say($_);
		if ($_ =~ m{registration as a STORAGE ENGINE failed.}sio) {
			say("Storage engine registration failed");
			$restart_status = STATUS_DATABASE_CORRUPTION;
		} elsif ($_ =~ m{exception}sio) {
			say("Exception was caught");
			$restart_status = STATUS_DATABASE_CORRUPTION;
		} elsif ($_ =~ m{ready for connections}sio) {
			say("Server restart was apparently successfull.") if $restart_status == STATUS_OK ;
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
			$restart_status = STATUS_DATABASE_CORRUPTION;
			last;
		}
	}
	close(RESTART);

	$restart_count++;
	my $dbh = DBI->connect($reporter->dsn());
	$restart_status = STATUS_DATABASE_CORRUPTION if not defined $dbh && $restart_status == STATUS_OK;

	if ($restart_status > STATUS_OK) {
		say("Restart has failed.");
		return $restart_status;
	}

	return STATUS_OK;

}

sub type {
	return REPORTER_TYPE_PERIODIC;
}

1;
