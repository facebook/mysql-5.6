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

# We need to revisit the grammar we use / how we validate consistency
# if we need to keep using this reporter

package GenTest::Reporter::DrizzleRecoveryConsistency;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;

sub monitor {
	my $reporter = shift;
                my $dbh = DBI->connect($reporter->dsn(), undef, undef, {PrintError => 0});

	if (time() > $reporter->testEnd() - 20) {
		say("Sending shutdown() call to server.");
		$dbh->selectrow_array('SELECT shutdown()');
		sleep(5);
		return STATUS_SERVER_KILLED;
	} else {
		return STATUS_OK;
	}
}


sub report {
        my $reporter = shift;
        my $dbh = DBI->connect($reporter->dsn(), undef, undef, {PrintError => 0});
	my $basedir = $reporter->serverVariable('basedir');
        say("$basedir");
        my $binary = $basedir.'/drizzled/drizzled' ;
        my $datadir = '';
        if (-e $basedir.'/var')
        {
	    $datadir = $basedir.'/var/';
        }
        else
        {
            $datadir = $basedir.'tests/var/master-data';
        }
	$datadir =~ s{[\\/]$}{}sgio;
	my $recovery_datadir = $datadir.'_recovery';
	my $port = $reporter->serverVariable('mysql_protocol_port');
	
	say("Sending shutdown() call to server.");
	$dbh->selectrow_array('SELECT shutdown()');
	sleep(5);


	system("cp -r $datadir $recovery_datadir");
	
	say("Attempting database recovery...");

	my @drizzled_options = (
		'--no-defaults',
		'--core-file',	
		'--datadir="'.$recovery_datadir.'"',
                '--basedir="'.$basedir.'"',
                '--plugin-add=shutdown_function',
		'--mysql-protocol.port='.$port,

	);

	my $drizzled_command = $binary.' '.join(' ', @drizzled_options).' 2>&1';
	say("Executing $drizzled_command .");

	open(DRIZZLED, "$drizzled_command|");
	my $recovery_status = STATUS_OK;
	while (<DRIZZLED>) {
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

	say("Shutting down the recovered server...");

	if (not defined $dbh) {
		$recovery_status = STATUS_DATABASE_CORRUPTION;
	} else {
		say("Sending shutdown() call to server.");
		$dbh->selectrow_array('SELECT shutdown()');
	}

	close(DRIZZLED);

	if ($recovery_status > STATUS_OK) {
		say("Recovery has failed.");
	}

	return $recovery_status;
}	

sub type {
	return REPORTER_TYPE_CRASH | REPORTER_TYPE_DEADLOCK | REPORTER_TYPE_SUCCESS | REPORTER_TYPE_PERIODIC | REPORTER_TYPE_SERVER_KILLED;
}

1;
