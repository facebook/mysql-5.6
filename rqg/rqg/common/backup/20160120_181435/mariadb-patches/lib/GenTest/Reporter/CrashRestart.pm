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


#################
# Goal: Check server behavior on restart after a crash.
# 
# The reporter crashes the server and immediately restarts it.
# The test (runall-new) must be run with --restart-timeout=N to wait
# till the server is up again.
#################

package GenTest::Reporter::CrashRestart;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;

use DBServer::MySQL::MySQLd;

my $first_reporter;

sub monitor {
	my $reporter = shift;

	$first_reporter = $reporter if not defined $first_reporter;
	return STATUS_OK if $reporter ne $first_reporter;

	# Do not crash in the first 20 seconds after the test flow started
	return STATUS_OK if (time() < $reporter->reporterStartTime() + 20);

	my $server = $reporter->properties->servers->[0];
	my $status;
	my $vardir = $server->vardir();
	my $datadir = $server->datadir();
	my $port = $server->port();

	# First, check that the server is still available 
	# (or it might happen that it crashed on its own, and by restarting it we will hide the problem)
	my $dbh = DBI->connect($reporter->dsn());

	unless ($dbh) {
		say("CrashRestart reporter: ERROR: Could not connect to the server before shutdown. Status will be set to STATUS_SERVER_CRASHED");
		return STATUS_SERVER_CRASHED;
	}

	my $pid = $reporter->serverInfo('pid');
	if (!defined $pid) {
		say("CrashRestart reporter: ERROR: Server PID is not defined, cannot crash the server");
		return STATUS_ENVIRONMENT_FAILURE;
	} else {
		say("CrashRestart reporter: Sending SIGKILL to server with pid $pid...");
		kill(9, $pid);
	}

	my $dbh;
	foreach (1..5) {
		$dbh = DBI->connect($reporter->dsn(),'','',{PrintError=>0}) ;
		last if not $dbh;
		sleep(1);
	}
	if ($dbh) {
		say("CrashRestart reporter: ERROR: Still can connect to the server, crash did not work. Status will be set to ENVIRONMENT_FAILURE");
		return STATUS_ENVIRONMENT_FAILURE;
	}


	say("CrashRestart reporter: Restarting the server ...");
	my $status = $server->startServer();

	if ($status > STATUS_OK) {
		say("CrashRestart reporter: ERROR: Server startup finished with an error");
		return $status;
	}

	$dbh = DBI->connect($reporter->dsn());

	unless ($dbh) {
		say("CrashRestart reporter: ERROR: Could not connect to the restarted server. Status will be set to ENVIRONMENT_FAILURE");
		return STATUS_ENVIRONMENT_FAILURE;
	}

	$reporter->updatePid();

	return STATUS_OK;
}

sub type {
	return REPORTER_TYPE_PERIODIC;
}


1;

