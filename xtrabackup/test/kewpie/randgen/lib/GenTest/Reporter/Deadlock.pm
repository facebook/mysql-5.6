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

package GenTest::Reporter::Deadlock;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use GenTest;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Reporter;
use GenTest::Executor::MySQL;

use DBI;
use Data::Dumper;
use POSIX;

use constant PROCESSLIST_PROCESS_TIME		=> 5;
use constant PROCESSLIST_PROCESS_INFO		=> 7;

# The time, in seconds, we will wait for a connect before we declare the server hanged
use constant CONNECT_TIMEOUT_THRESHOLD		=> 20;

# Minimum lifetime of a query before it is considered suspicios
use constant QUERY_LIFETIME_THRESHOLD		=> 600;	# Seconds

# Number of suspicious queries required before a deadlock is declared
use constant STALLED_QUERY_COUNT_THRESHOLD	=> 5;

# Number of times the actual test duration is allowed to exceed the desired one
use constant ACTUAL_TEST_DURATION_MULTIPLIER	=> 2;

sub monitor {
	my $reporter = shift;

	my $actual_test_duration = time() - $reporter->testStart();

	if ($actual_test_duration > ACTUAL_TEST_DURATION_MULTIPLIER * $reporter->testDuration()) {
		say("Actual test duration ($actual_test_duration seconds) is more than ".(ACTUAL_TEST_DURATION_MULTIPLIER)." times the desired duration (".$reporter->testDuration()." seconds)");
		return STATUS_SERVER_DEADLOCKED;
	}

	if (osWindows()) {
		return $reporter->monitor_threaded();
	} else {
		return $reporter->monitor_nonthreaded();
	}
}

sub monitor_nonthreaded {
	my $reporter = shift;
	my $dsn = $reporter->dsn();

	# We connect on every run in order to be able to use the mysql_connect_timeout to detect very debilitating deadlocks.

	my $dbh;

	# We directly call exit() in the handler because attempting to catch and handle the signal in a more civilized 
	# manner does not work for some reason -- the read() call from the server gets restarted instead

	sigaction SIGALRM, new POSIX::SigAction sub {
                exit (STATUS_SERVER_DEADLOCKED);
	} or die "Error setting SIGALRM handler: $!\n";

	my $prev_alarm1 = alarm (CONNECT_TIMEOUT_THRESHOLD);
	$dbh = DBI->connect($dsn, undef, undef, { mysql_connect_timeout => CONNECT_TIMEOUT_THRESHOLD * 2} );

	if (defined GenTest::Executor::MySQL::errorType($DBI::err)) {
		alarm (0);
		return GenTest::Executor::MySQL::errorType($DBI::err);
	} elsif (not defined $dbh) {
		alarm (0);
		return STATUS_UNKNOWN_ERROR;
	}

	my $processlist = $dbh->selectall_arrayref("SHOW FULL PROCESSLIST");
	alarm (0);

	my $stalled_queries = 0;

	foreach my $process (@$processlist) {
		if (
			($process->[PROCESSLIST_PROCESS_INFO] ne '') &&
			($process->[PROCESSLIST_PROCESS_TIME] > QUERY_LIFETIME_THRESHOLD)
		) {
			$stalled_queries++;
#			say("Stalled query: ".$process->[PROCESSLIST_PROCESS_INFO]);
		}
	}

	if ($stalled_queries >= STALLED_QUERY_COUNT_THRESHOLD) {
		say("$stalled_queries stalled queries detected, declaring deadlock at DSN $dsn.");

		foreach my $status_query (
			"SHOW PROCESSLIST",
			"SHOW ENGINE INNODB STATUS"
			# "SHOW OPEN TABLES" - disabled due to bug #46433
		) {
			say("Executing $status_query:");
			my $status_result = $dbh->selectall_arrayref($status_query);
			print Dumper $status_result;
		}

		return STATUS_SERVER_DEADLOCKED;
	} else {
		return STATUS_OK;
	}
}

sub monitor_threaded {
	my $reporter = shift;

	require threads;

#
# We create two threads:
# * alarm_thread keeps a timeout so that we do not hang forever
# * dbh_thread attempts to connect to the database and thus can hang forever because
# there are no network-level timeouts in DBD::mysql
# 

	my $alarm_thread = threads->create( \&alarm_thread );
	my $dbh_thread = threads->create ( \&dbh_thread, $reporter );

	my $status;

	# We repeatedly check if either thread has terminated, and if so, reap its exit status

	while (1) {
		foreach my $thread ($alarm_thread, $dbh_thread) {
			$status = $thread->join() if defined $thread && $thread->is_joinable();
		}
		last if defined $status;
		sleep(1);
	}

	# And then we kill the remaining thread.

	foreach my $thread ($alarm_thread, $dbh_thread) {
		next if !$thread->is_running();
		# Windows hangs when joining killed threads
		if (osWindows()) {
			$thread->kill('SIGKILL');
		} else {
			$thread->kill('SIGKILL')->join();
		}
 	}

	return ($status);
}

sub alarm_thread {
	local $SIG{KILL} = sub { threads->exit() };

	# We sleep in small increments so that signals can get delivered in the meantime

	foreach my $i (1..CONNECT_TIMEOUT_THRESHOLD) {
		sleep(1);
	};

	say("Entire-server deadlock detected.");
	return(STATUS_SERVER_DEADLOCKED);
}

sub dbh_thread {
	local $SIG{KILL} = sub { threads->exit() };
	my $reporter = shift;
	my $dsn = $reporter->dsn();

	# We connect on every run in order to be able to use a timeout to detect very debilitating deadlocks.

	my $dbh = DBI->connect($dsn, undef, undef, { mysql_connect_timeout => CONNECT_TIMEOUT_THRESHOLD * 2, PrintError => 1, RaiseError => 0 });

	if (defined GenTest::Executor::MySQL::errorType($DBI::err)) {
		return GenTest::Executor::MySQL::errorType($DBI::err);
	} elsif (not defined $dbh) {
		return STATUS_UNKNOWN_ERROR;
	}

	my $processlist = $dbh->selectall_arrayref("SHOW FULL PROCESSLIST");
	return GenTest::Executor::MySQL::errorType($DBI::err) if not defined $processlist;

	my $stalled_queries = 0;

	foreach my $process (@$processlist) {
		if (
			($process->[PROCESSLIST_PROCESS_INFO] ne '') &&
			($process->[PROCESSLIST_PROCESS_TIME] > QUERY_LIFETIME_THRESHOLD)
		) {
			$stalled_queries++;
		}
	}

	if ($stalled_queries >= STALLED_QUERY_COUNT_THRESHOLD) {
		say("$stalled_queries stalled queries detected, declaring deadlock at DSN $dsn.");
		print Dumper $processlist;
		return STATUS_SERVER_DEADLOCKED;
	} else {
		return STATUS_OK;
	}
}

sub report {

	my $reporter = shift;
	my $server_pid = $reporter->serverInfo('pid');
	my $datadir = $reporter->serverVariable('datadir');

	if (
		($^O eq 'MSWin32') ||
		($^O eq 'MSWin64')
        ) {
		my $cdb_command = "cdb -p $server_pid -c \".dump /m $datadir\mysqld.dmp;q\"";
		say("Executing $cdb_command");
		system($cdb_command);
	} else {
		say("Killing mysqld with pid $server_pid with SIGHUP in order to force debug output.");
		kill(1, $server_pid);
		sleep(2);

		say("Killing mysqld with pid $server_pid with SIGSEGV in order to capture core.");
		kill(11, $server_pid);
		sleep(20);
	}

	return STATUS_OK;
}

sub type {
	return REPORTER_TYPE_PERIODIC | REPORTER_TYPE_DEADLOCK;
}

1;

