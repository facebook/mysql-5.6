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

package GenTest::Reporter::DatabaseConsistency;

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
use IPC::Open3;

my $first_reporter;

sub report {
	my $reporter = shift;

	$first_reporter = $reporter if not defined $first_reporter;
	return STATUS_OK if $reporter ne $first_reporter;

	my $binary = $reporter->serverInfo('binary');
	my $language = $reporter->serverVariable('language');
	my $lc_messages_dir = $reporter->serverVariable('lc_messages_dir');
	my $datadir = $reporter->serverVariable('datadir');
	$datadir =~ s{[\\/]$}{}sgio;
	my $socket = $reporter->serverVariable('socket');
	my $port = $reporter->serverVariable('port');
	my $pid = $reporter->serverInfo('pid');
	my $maria_block_size = $reporter->serverVariable('maria_block_size');
	my $plugin_dir = $reporter->serverVariable('plugin_dir');
	my $plugins = $reporter->serverPlugins();

	my $engine = $reporter->serverVariable('storage_engine');

	my $dbh = DBI->connect($reporter->dsn());
	my $consistency_status = STATUS_OK;

	say("Testing database consistency");

	my $databases = $dbh->selectcol_arrayref("SHOW DATABASES");
	foreach my $database (@$databases) {
		next if $database =~ m{^(mysql|information_schema|pbxt|performance_schema)$}sio;
		$dbh->do("USE $database");
		my $tables = $dbh->selectcol_arrayref("SHOW TABLES");
		foreach my $table (@$tables) {
			say("Verifying table: $table; database: $database");

			my $sth_keys = $dbh->prepare("SHOW KEYS FROM `$database`.`$table`");
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
						$main_predicate = "WHERE `$column_name` = '' OR  `$column_name` != ''";
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
					return STATUS_DATABASE_CORRUPTION;
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

				$consistency_status = STATUS_DATABASE_CORRUPTION;
			}

			if (lc($engine) ne 'falcon') {
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
							if ($result =~ m{error|corrupt|repaired|invalid|crashed}sio) {
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
	}

	return $consistency_status;
}

sub type {
	return REPORTER_TYPE_ALWAYS;
}

1;
