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

package GenTest::Reporter::AriaDoubleRecovery;

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
use POSIX;

my $first_reporter;

sub monitor {
	my $reporter = shift;

	# In case of two servers, we will be called twice.
	# Only kill the first server and ignore the second call.
	
	$first_reporter = $reporter if not defined $first_reporter;
	return STATUS_OK if $reporter ne $first_reporter;

	my $pid = $reporter->serverInfo('pid');

	if (time() > $reporter->testEnd() - 19) {
		say("Sending SIGKILL to server with pid $pid in order to force a recovery.");
		kill(9, $pid);
		return STATUS_SERVER_KILLED;
	} else {
		return STATUS_OK;
	}
}

sub report {
	my $reporter = shift;

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
	my $aria_block_size = $reporter->serverVariable('maria_block_size') || $reporter->serverVariable('aria_block_size');
	my $plugin_dir = $reporter->serverVariable('plugin_dir');
	my $plugins = $reporter->serverPlugins();
	my $binlog_format = $reporter->serverVariable('binlog_format');
	my $binlog_on = $reporter->serverVariable('log_bin');

	my $engine = $reporter->serverVariable('storage_engine');

	my $dbh_prev = DBI->connect($reporter->dsn());

	if (defined $dbh_prev) {
		# Server is still running, kill it.
		$dbh_prev->disconnect();

		say("Sending SIGKILL to server with pid $pid in order to force a recovery.");
		kill(9, $pid);
		sleep(5);
	}

	say("Copying datadir... (interrupting the copy operation may cause a false recovery failure to be reported below");
	system("cp -r $datadir $recovery_datadir");
	system("rm -f $recovery_datadir/core*");	# Remove cores from any previous crash

	my $recovery_datadir_aria = $recovery_datadir.'-aria';

	say("Attempting database recovery using aria_read_log ...");
	say("Copying Aria log files from $datadir to $recovery_datadir_aria ...");

	# Copy just the *aria* files in an empty location and create a test "database"
	system("mkdir $recovery_datadir_aria");
	system("cp $datadir/*aria_log* $recovery_datadir_aria");
	system("mkdir $recovery_datadir_aria/test");
	system("mkdir $recovery_datadir_aria/smf");
	system("mkdir $recovery_datadir_aria/smf2");

	say("Copying complete.");

	my $aria_read_log_path;
	if (-e $reporter->serverVariable('basedir')."/../storage/maria/aria_read_log") {
		$aria_read_log_path = $reporter->serverVariable('basedir')."/../storage/maria/aria_read_log";
	} else {
		$aria_read_log_path = $reporter->serverVariable('basedir')."/../storage/maria/maria_read_log";			
	}

	my $aria_chk_path;

	if (-e $reporter->serverVariable('basedir')."/../storage/maria/aria_chk") {
		$aria_chk_path = $reporter->serverVariable('basedir')."/../storage/maria/aria_chk";
	} else {
		$aria_chk_path = $reporter->serverVariable('basedir')."/../storage/maria/maria_chk";
	}

	chdir($recovery_datadir_aria);
	my $aria_read_log_command = $aria_read_log_path." --aria-log-dir-path $recovery_datadir_aria --apply --check --silent";

	my $recovery_pid;
	my $recovery_duration = 4;
	my $child_pid = fork();
	if ($child_pid == 0) {
		say("Executing first $aria_read_log_command");
		exec($aria_read_log_command);
		exit();
	} else {
		$recovery_pid = $child_pid;
		say("Will kill the first aria_read_log via kill -9 $recovery_pid in $recovery_duration seconds");
		local $SIG{ALRM} = sub { kill 9 , $recovery_pid } ;
		alarm($recovery_duration);
		sleep($recovery_duration);
	}

	alarm(0);
	print "\n";
	sleep(1);

	say("Executing second $aria_read_log_command");
	my $aria_read_log_result = system($aria_read_log_command);
	return STATUS_RECOVERY_FAILURE if $aria_read_log_result > 0;
	say("$aria_read_log_path apparently returned success");

	my $aria_chk_result = system("$aria_chk_path --extend-check */*.MAI");
	return STATUS_RECOVERY_FAILURE if $aria_chk_result > 0;
	say("$aria_chk_path apparently returned success");
	return STATUS_OK;
}

sub type {
	return REPORTER_TYPE_ALWAYS | REPORTER_TYPE_PERIODIC;
}

1;
