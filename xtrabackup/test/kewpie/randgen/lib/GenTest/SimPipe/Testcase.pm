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
#

package GenTest::SimPipe::Testcase;

require Exporter;
@ISA = qw(GenTest);
@EXPORT = qw(
	
);

use strict;

use GenTest;
use GenTest::Constants;
use GenTest::SimPipe::DBObject;

use constant TESTCASE_MYSQLD_OPTIONS		=> 0;
use constant TESTCASE_OPTIMIZER_SWITCHES	=> 1;
use constant TESTCASE_DB_OBJECTS		=> 2;
use constant TESTCASE_QUERIES			=> 3;

use constant MYSQLD_OPTION_NAME			=> 0;
use constant MYSQLD_OPTION_VALUE		=> 1;

1;

sub new {
	my $class = shift;

	my $testcase = $class->SUPER::new({
		'mysqld_options'	=> TESTCASE_MYSQLD_OPTIONS,
		'optimizer_switches'	=> TESTCASE_OPTIMIZER_SWITCHES,
		'db_objects'		=> TESTCASE_DB_OBJECTS,
		'queries'		=> TESTCASE_QUERIES,
	}, @_);
	
	return $testcase;
}

sub newFromDSN {
	my ($class, $dsn, $queries) = @_;

	my $dbh = DBI->connect($dsn, undef, undef, { mysql_multi_statements => 1, RaiseError => 1 });

	return $class->newFromDBH($dbh, $queries);
}

sub newFromDBH {
	my ($class, $dbh, $queries) = @_;

	my @table_objs;
	my $table_names = $dbh->selectcol_arrayref("
		SELECT TABLE_NAME
		FROM INFORMATION_SCHEMA.TABLES
		WHERE TABLE_SCHEMA = DATABASE()
                AND TABLE_TYPE = 'BASE TABLE'
		ORDER BY TABLE_ROWS DESC
	");

	foreach my $table_name (@$table_names) {
		print "Loading $table_name\n";
		push @table_objs, GenTest::SimPipe::DBObject->newFromDBH($dbh, $table_name);
	}

	my %mysqld_options = @{$dbh->selectcol_arrayref("
		SELECT LOWER(VARIABLE_NAME), VARIABLE_VALUE
		FROM INFORMATION_SCHEMA.SESSION_VARIABLES
		WHERE VARIABLE_NAME IN (
			'optimizer_use_mrr',
			'mrr_buffer_size',
			'join_cache_level',
			'join_buffer_size',
			'join_buffer_space_limit',
			'rowid_merge_buff_size'
		)
	", { Columns=>[1,2] })};

	my $optimizer_switch_hash;
	my $optimizer_switch_string = $dbh->selectrow_array('SELECT @@optimizer_switch');
	my @optimizer_switch_parts = split(',', $optimizer_switch_string);
	foreach my $optimizer_switch_part (@optimizer_switch_parts) {
		my ($optimizer_switch_name, $optimizer_switch_value) = split ('=', $optimizer_switch_part);
		$optimizer_switch_hash->{$optimizer_switch_name} = $optimizer_switch_value;
	}

	return GenTest::SimPipe::Testcase->new(
		mysqld_options		=> \%mysqld_options,
		optimizer_switches	=> $optimizer_switch_hash,
		db_objects		=> \@table_objs,
		queries			=> $queries
	);
};

sub mysqldOptions {
	return $_[0]->[TESTCASE_MYSQLD_OPTIONS];
}

sub optimizerSwitches {
	return $_[0]->[TESTCASE_OPTIMIZER_SWITCHES];
}

sub dbObjects {
	return $_[0]->[TESTCASE_DB_OBJECTS];
}

sub queries {
	return $_[0]->[TESTCASE_QUERIES];
}

sub mysqldOptionsToString {
	my $testcase = shift;
	
	my @mysqld_option_strings;

	while (my ($option_name, $option_value) = each %{$testcase->mysqldOptions()}) {
		next if not defined $option_value;
		if ($option_value =~ m{^\d*$}sio) {
			push @mysqld_option_strings, "SET SESSION $option_name = $option_value;";
		} else {
			push @mysqld_option_strings, "SET SESSION $option_name = '$option_value';";
		}
	}

	while (my ($optimizer_switch_name, $optimizer_switch_value) = each %{$testcase->optimizerSwitches()}) {
		next if not defined $optimizer_switch_value;
		push @mysqld_option_strings, "SET SESSION optimizer_switch = '$optimizer_switch_name=$optimizer_switch_value';";
	}

	return join("\n", @mysqld_option_strings);
}

sub dbObjectsToString {
	my $testcase = shift;

	my @dbobject_strings;

	foreach my $dbobject (sort @{$testcase->dbObjects()}) {
		next if not defined $dbobject;
		push @dbobject_strings, $dbobject->toString();
	}

	return join("\n", @dbobject_strings);
}

sub toString {
	my $testcase = shift;
	return $testcase->mysqldOptionsToString()."\n".$testcase->dbObjectsToString()."\n".join("\n", @{$testcase->queries()})."\n";
}

sub simplify {
	my ($testcase, $oracle) = @_;

	my %col_map;
	my $col_id = 1;

	foreach my $dbobject (@{$testcase->dbObjects()}) {
		next if not defined $dbobject;
		foreach my $column (@{$dbobject->columns()}) {
			if (exists $col_map{$column->[COLUMN_NAME]}) {		
				$column->[COLUMN_NAME] = $col_map{$column->[COLUMN_NAME]};
			} else {
				$col_map{$column->[COLUMN_NAME]} = 'f'.$col_id++;
				$column->[COLUMN_NAME] = $col_map{$column->[COLUMN_NAME]};
			}
		}

		foreach my $key (@{$dbobject->keys()}) {
			foreach my $i (0..$#{$key->[KEY_COLUMNS]}) {
				$key->[KEY_COLUMNS]->[$i] = $col_map{$key->[KEY_COLUMNS]->[$i]} if exists $col_map{$key->[KEY_COLUMNS]->[$i]};
			}
		}
	}

	foreach my $query (@{$testcase->queries()}) {
		while (my ($old, $new) = each %col_map) {
			$query =~ s{$old([^A-Za-z_0-9]|$)}{$new$1}sgi;
		}
		print "Rewritten: $query\n";
	}

	if ($oracle->oracle($testcase) != ORACLE_ISSUE_STILL_REPEATABLE) {
		say("Initial testcase is not repeatable.");
		return undef;
	}

	foreach my $db_object (@{$testcase->dbObjects()}) {

		print "Attempting to remove table ".$db_object->name()."\n";
		my $saved_db_object = $db_object;
		$db_object = undef;
		next if $oracle->oracle($testcase) == ORACLE_ISSUE_STILL_REPEATABLE;
		$db_object = $saved_db_object;

		next if not defined $db_object;

		my $rows = $db_object->data();

		foreach my $row_group_size (5000,500,50,10,5) {
			my $row_group_count = int(($#$rows + 1) / $row_group_size);
			next if $row_group_count < 1;
	
			foreach my $row_group (0..($row_group_count-1)) {
				next if not defined $rows->[$row_group * $row_group_size];
				print "Trying row_group $row_group, row_group_count $row_group_count, row_group_size $row_group_size\n";
				my @saved_rows;
				foreach my $i (0..($row_group_size-1)) {
					$saved_rows[$i] = $rows->[($row_group * $row_group_size) + $i];
					$rows->[($row_group * $row_group_size) + $i] = undef;
				}

				next if $oracle->oracle($testcase) == ORACLE_ISSUE_STILL_REPEATABLE;

				foreach my $i (0..($row_group_size-1)) {
					$rows->[($row_group * $row_group_size) + $i] = $saved_rows[$i];
				}
			}			
		}
	
		foreach my $row (@{$db_object->data()}) {
			next if not defined $row;
			print "R";
			my $saved_row = $row;
			$row = undef;
			if ($oracle->oracle($testcase) != ORACLE_ISSUE_STILL_REPEATABLE) {
				$row = $saved_row;
			}
		}


		my $saved_engine = $db_object->[DBOBJECT_ENGINE];
		$db_object->[DBOBJECT_ENGINE] = undef;
		if ($oracle->oracle($testcase) != ORACLE_ISSUE_STILL_REPEATABLE) {
			$db_object->[DBOBJECT_ENGINE] = $saved_engine;
		}

		foreach my $key (@{$db_object->keys()}) {
			print "K";
			my $saved_key = $key;
			$key = undef;
			if ($oracle->oracle($testcase) != ORACLE_ISSUE_STILL_REPEATABLE) {
				$key = $saved_key;
			}
		}

		foreach my $column (@{$db_object->columns()}) {
			print "Column";
			my $saved_column = $column;
	
			$column = undef;
			next if $oracle->oracle($testcase) == ORACLE_ISSUE_STILL_REPEATABLE;
			$column = $saved_column;
		
#			$column->[COLUMN_TYPE] = 'int'; $column->[COLUMN_COLLATION] = undef;
#			next if $oracle->oracle($testcase) == ORACLE_ISSUE_STILL_REPEATABLE;
#			$column = $saved_column;

#			$column->[COLUMN_TYPE] = 'varchar(32)'; $column->[COLUMN_COLLATION] = undef;
#			next if $oracle->oracle($testcase) == ORACLE_ISSUE_STILL_REPEATABLE;
#			$column = $saved_column;
		}

		foreach my $row (@{$db_object->data()}) {
			next if not defined $row;
			foreach my $cell (values %$row) {
				print "Cell";
				next if not defined $cell || length($cell) == 1 || $cell =~ m{^\d+$}sgio;
				my $saved_cell = $cell;
				foreach my $new_length (16,32,128,512) {
					last if length($saved_cell) <= $new_length;
					$cell = substr($saved_cell, 0, $new_length);
					print "Replacing $saved_cell with $cell\n";
					if ($oracle->oracle($testcase) !=ORACLE_ISSUE_STILL_REPEATABLE) {
						$cell = $saved_cell;
					} else {
						last;
					}
				}
			}
		}
	}


	my $mysqld_options = $testcase->mysqldOptions();
	foreach my $mysqld_option (keys %{$mysqld_options}) {
		print "M";
		my $saved_mysqld_option_value = $mysqld_options->{$mysqld_option};
		$mysqld_options->{$mysqld_option} = undef;
		if ($oracle->oracle($testcase) != ORACLE_ISSUE_STILL_REPEATABLE) {
			$mysqld_options->{$mysqld_option} = $saved_mysqld_option_value;
		}
	}

	my $optimizer_switches = $testcase->optimizerSwitches();
	foreach my $optimizer_switch (keys %{$optimizer_switches}) {
		print "O";
		my $saved_optimizer_switch_value = $optimizer_switches->{$optimizer_switch};
		$optimizer_switches->{$optimizer_switch} = undef;
		if ($oracle->oracle($testcase) != ORACLE_ISSUE_STILL_REPEATABLE) {
			$optimizer_switches->{$optimizer_switch} = $saved_optimizer_switch_value;
		}
	}

	print "\n";

	if ($oracle->oracle($testcase) != ORACLE_ISSUE_STILL_REPEATABLE) {
		say("Final testcase is not repeatable.");
		return undef;
	} else {
		return $testcase;
	}
}
