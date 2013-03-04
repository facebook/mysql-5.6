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

package GenTest::Reporter::RecoveryConsistency;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;

sub monitor {
	my $reporter = shift;
	my $pid = $reporter->serverInfo('pid');

	if (time() > $reporter->testEnd() - 10) {
		say("Sending SIGKILL to mysqld with pid $pid in order to force a recovery.");
		kill(9, $pid);
		return STATUS_SERVER_KILLED;
	} else {
		return STATUS_OK;
	}
}

sub report {
	my $reporter = shift;
	my $binary = $reporter->serverInfo('binary');
	my $language = $reporter->serverVariable('language');
	my $datadir = $reporter->serverVariable('datadir');
	$datadir =~ s{[\\/]$}{}sgio;
	my $recovery_datadir = $datadir.'_recovery';
	my $socket = $reporter->serverVariable('socket');
	my $port = $reporter->serverVariable('port');
	my $pid = $reporter->serverInfo('pid');
	
	say("Sending SIGKILL to mysqld with pid $pid in order to force a recovery.");
	kill(9, $pid);
	sleep(10);

	system("cp -r $datadir $recovery_datadir");
	
	say("Attempting database recovery...");

	my @mysqld_options = (
		'--no-defaults',
		'--core-file',
		'--loose-console',
		'--loose-falcon-debug-mask=65535',
		'--language='.$language,
		'--datadir="'.$recovery_datadir.'"',
		'--socket="'.$socket.'"',
		'--port='.$port
	);

	my $mysqld_command = $binary.' '.join(' ', @mysqld_options).' 2>&1';
	say("Executing $mysqld_command .");

	open(MYSQLD, "$mysqld_command|");
	my $recovery_status = STATUS_OK;
	while (<MYSQLD>) {
		$_ =~ s{[\r\n]}{}siog;
		say($_);
		if ($_ =~ m{exception}sio) {
			$recovery_status = STATUS_DATABASE_CORRUPTION;
		} elsif ($_ =~ m{ready for connections}sio) {
			say("Server Recovery was apparently successfull.") if $recovery_status == STATUS_OK ;
			last;
		} elsif ($_ =~ m{got signal}sio) {
			$recovery_status = STATUS_DATABASE_CORRUPTION;

		}
	}

	say("Checking database consistency...");
	my $dbh = DBI->connect($reporter->dsn());

	my $tables = $dbh->selectcol_arrayref("SHOW TABLES");

        foreach my $table (@$tables) {
                my $average = $dbh->selectrow_array("
                        SELECT (SUM(`col_int_key`)  + SUM(`col_int`)) / COUNT(*)
                        FROM `$table`
                ");

                if ($average ne '200.0000') {
                        say("Bad average on table: $table; average: $average");
			$recovery_status = STATUS_DATABASE_CORRUPTION;
                } else {
                        say("Average is $average");
		}
        }

	if ($recovery_status > STATUS_OK) {
		say("Recovery has failed.");
	}

	return $recovery_status;
}	

sub type {
	return REPORTER_TYPE_CRASH | REPORTER_TYPE_DEADLOCK | REPORTER_TYPE_SUCCESS | REPORTER_TYPE_PERIODIC | REPORTER_TYPE_SERVER_KILLED;
}

1;
