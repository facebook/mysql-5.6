# Copyright (C) 2013 Monty Program Ab
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


# The module is based on the traditional Recovery.pm, 
# but the new one restarts the server in exactly the same way
# (with the same options) as it was initially running

# It is supposed to be used with the native server startup,
# i.e. with runall-new.pl rather than runall.pl which is MTR-based.

package GenTest::Reporter::CrashRecovery;

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

	alarm(3600);

	$first_reporter = $reporter if not defined $first_reporter;
	return STATUS_OK if $reporter ne $first_reporter;

	my $datadir = $reporter->serverVariable('datadir');
	$datadir =~ s{[\\/]$}{}sgio;
	my $orig_datadir = $datadir.'_orig';
	my $pid = $reporter->serverInfo('pid');

	my $engine = $reporter->serverVariable('storage_engine');

	my $dbh_prev = DBI->connect($reporter->dsn());

	if (defined $dbh_prev) {
		# Server is still running, kill it. Again.
		$dbh_prev->disconnect();

		say("Sending SIGKILL to server with pid $pid in order to force a recovery.");
		kill(9, $pid);
		sleep(5);
	}

	my $server = $reporter->properties->servers->[0];
	say("Copying datadir... (interrupting the copy operation may cause investigation problems later)");
	if (osWindows()) {
		system("xcopy \"$datadir\" \"$orig_datadir\" /E /I /Q");
	} else { 
		system("cp -r $datadir $orig_datadir");
	}
	move($server->errorlog, $server->errorlog.'_orig');
	unlink("$datadir/core*");	# Remove cores from any previous crash

	say("Attempting database recovery using the server ...");

	$server->setStartDirty(1);
	my $recovery_status = $server->startServer();
	open(RECOVERY, $server->errorlog);

	while (<RECOVERY>) {
		$_ =~ s{[\r\n]}{}siog;
		say($_);
		if ($_ =~ m{registration as a STORAGE ENGINE failed.}sio) {
			say("Storage engine registration failed");
			$recovery_status = STATUS_DATABASE_CORRUPTION;
		} elsif ($_ =~ m{corrupt|crashed}) {
			say("Log message '$_' might indicate database corruption");
		} elsif ($_ =~ m{exception}sio) {
			$recovery_status = STATUS_DATABASE_CORRUPTION;
		} elsif ($_ =~ m{ready for connections}sio) {
			say("Server Recovery was apparently successfull.") if $recovery_status == STATUS_OK ;
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

	close(RECOVERY);

	my $dbh = DBI->connect($reporter->dsn());
	$recovery_status = STATUS_DATABASE_CORRUPTION if not defined $dbh && $recovery_status == STATUS_OK;

	if ($recovery_status > STATUS_OK) {
		say("Recovery has failed.");
		return $recovery_status;
	}
	
	# 
	# Phase 2 - server is now running, so we execute various statements in order to verify table consistency
	#

	say("Testing database consistency");

	my $databases = $dbh->selectcol_arrayref("SHOW DATABASES");
	foreach my $database (@$databases) {
		next if $database =~ m{^(mysql|information_schema|pbxt|performance_schema)$}sio;
		$dbh->do("USE $database");
		my $tabl_ref = $dbh->selectcol_arrayref("SHOW FULL TABLES", { Columns=>[1,2] });
		my %tables = @$tabl_ref;
		foreach my $table (keys %tables) {
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
			
			# Should not do CHECK etc., and especially ALTER, on a view
			next if $tables{$table} eq 'VIEW';

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

	return STATUS_OK;
}

sub type {
	return REPORTER_TYPE_ALWAYS | REPORTER_TYPE_PERIODIC;
}

1;
