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

package GenTest::SimPipe::DBObject;

require Exporter;
@ISA = qw(GenTest);
@EXPORT = qw(COLUMN_NAME COLUMN_TYPE COLUMN_COLLATION KEY_COLUMNS DBOBJECT_NAME DBOBJECT_ENGINE);

use strict;
use DBI;
use GenTest;


use constant DBOBJECT_NAME	=> 0;
use constant DBOBJECT_COLUMNS	=> 1;
use constant DBOBJECT_KEYS	=> 2;
use constant DBOBJECT_DATA	=> 3;
use constant DBOBJECT_ENGINE	=> 4;

use constant COLUMN_NAME		=> 0;
use constant COLUMN_ORIG_NAME		=> 1;
use constant COLUMN_DEFAULT		=> 2;
use constant COLUMN_IS_NULLABLE		=> 3;
use constant COLUMN_TYPE		=> 4;
use constant COLUMN_COLLATION		=> 5;

use constant KEY_NAME		=> 0;
use constant KEY_COLUMNS	=> 1;

my $dbname = 'test';


sub new {
	my $class = shift;

	my $dbobject = $class->SUPER::new({
		'name'		=> DBOBJECT_NAME,
		'columns'	=> DBOBJECT_COLUMNS,
		'keys'		=> DBOBJECT_KEYS,
		'data'		=> DBOBJECT_DATA,
		'engine'	=> DBOBJECT_ENGINE
	}, @_);
	
	return $dbobject;
}

sub newFromDSN {
	my ($class, $dsn, $table_name) = @_;

	my $dbh = DBI->connect($dsn);

	return $class->newFromDBH($dbh, $table_name);
}

sub newFromDBH {
	my ($class, $dbh, $table_name) = @_;
	
	my $table_info = $dbh->selectrow_hashref("
		SELECT * FROM INFORMATION_SCHEMA.TABLES
		WHERE TABLE_NAME = ?
		AND TABLE_SCHEMA = ?
	", undef, $table_name, $dbname);

	my $engine = $table_info->{ENGINE};

	my @columns;
	my $sth_columns = $dbh->prepare("
		SELECT * FROM INFORMATION_SCHEMA.COLUMNS
		WHERE TABLE_NAME = ?
		AND TABLE_SCHEMA = ?
	");
	$sth_columns->execute($table_name, $dbname);

	while (my $column_info = $sth_columns->fetchrow_hashref()) {
		my $new_column;
		$new_column->[COLUMN_NAME] = $new_column->[COLUMN_ORIG_NAME] = $column_info->{'COLUMN_NAME'};
		$new_column->[COLUMN_DEFAULT] = $column_info->{'COLUMN_DEFAULT'};
		$new_column->[COLUMN_IS_NULLABLE] = $column_info->{'IS_NULLABLE'};
		$new_column->[COLUMN_TYPE] = $column_info->{'COLUMN_TYPE'};
		$new_column->[COLUMN_COLLATION] = $column_info->{'COLLATION_NAME'};
		push @columns, $new_column;
	}

	my @keys;
	my $sth_keys = $dbh->prepare("
		SELECT INDEX_NAME, GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX SEPARATOR ',') AS COLUMN_NAMES
		FROM INFORMATION_SCHEMA.STATISTICS
		WHERE TABLE_NAME = ?
		AND TABLE_SCHEMA = ?
		GROUP BY TABLE_SCHEMA, TABLE_NAME, INDEX_NAME
	");
	$sth_keys->execute($table_name, $dbname);

	while (my $key_info = $sth_keys->fetchrow_hashref()) {
		my $new_key;
		$new_key->[KEY_NAME] = $key_info->{'INDEX_NAME'};
		$new_key->[KEY_COLUMNS] = [split(',', $key_info->{'COLUMN_NAMES'})];
		push @keys, $new_key;
	}

	my @data;
	my $sth_data = $dbh->prepare("SELECT * FROM `$dbname`.`$table_name`");
	$sth_data->execute();
	while (my $row = $sth_data->fetchrow_hashref()) {
		push @data, $row;
	}

	my $dbobject = GenTest::SimPipe::DBObject->new(
		name	=> $table_name,
		engine	=> $engine,
		columns	=> \@columns,
		keys	=> \@keys,
		data	=> \@data
	);
}

sub toString {
	my $dbobject = shift;

	my @column_strings;
	foreach my $column (@{$dbobject->columns()}) {
		next if not defined $column;
		next if not defined $column->[COLUMN_NAME];

		my $column_string = $column->[COLUMN_NAME]." ".$column->[COLUMN_TYPE];
		$column_string .= " COLLATE ".$column->[COLUMN_COLLATION] if defined $column->[COLUMN_COLLATION];
		$column_string .= " NOT NULL " if $column->[COLUMN_IS_NULLABLE] eq 'NO';
		push @column_strings, $column_string;
	}

	return "" if ($#column_strings == -1);

	foreach my $key (@{$dbobject->keys()}) {
		next if not defined $key;
		my $underlying_column_exists = 0;
		foreach my $column (@{$dbobject->columns()}) {
			foreach my $key_column_name (@{$key->[KEY_COLUMNS]}) {
				$underlying_column_exists = 1 if $column->[COLUMN_NAME] eq $key_column_name;
			}
		}
		next if !$underlying_column_exists;
		if ($key->[KEY_NAME] eq 'PRIMARY') {
			push @column_strings, "PRIMARY KEY (".join(',', @{$key->[KEY_COLUMNS]}).")";
		} else {
			push @column_strings, "KEY (".join(',', @{$key->[KEY_COLUMNS]}).")";
		}
	}

	my $create_string = "DROP TABLE IF EXISTS ".$dbobject->name().";\n";
	$create_string .= "CREATE TABLE ".$dbobject->name()." ( ".join(", ", @column_strings).") ";
	$create_string .= " ENGINE=".$dbobject->engine() if defined $dbobject->engine();
	$create_string .= " TRANSACTIONAL=0 " if $dbobject->engine() =~ m{Aria}sio;
	$create_string .= ";\n";

	my @rows_data;

	foreach my $row_id (0..$#{$dbobject->[DBOBJECT_DATA]}) {
		next if not defined $dbobject->[DBOBJECT_DATA]->[$row_id];
		my @row_data;
		foreach my $column (@{$dbobject->columns()}) {
			next if not defined $column;
			next if not defined $column->[COLUMN_NAME];
			my $cell = $dbobject->[DBOBJECT_DATA]->[$row_id]->{$column->[COLUMN_NAME]} || $dbobject->[DBOBJECT_DATA]->[$row_id]->{$column->[COLUMN_ORIG_NAME]};
			$cell =~ s{'}{\\'}sgio;
			if (not defined $cell) {
				push @row_data, 'NULL';
			} elsif ($cell =~ m{^\d+$}sgio) {
				push @row_data, $cell;
			} else {
				push @row_data, "'".$cell."'";
			}
		}
		push @rows_data, "(".join(',', @row_data).")";
	}

	print "Object ".$dbobject->name()." has ".($#rows_data + 1)." rows\n";

	my $data_string .= "INSERT IGNORE INTO ".$dbobject->name()." VALUES ".join(',', @rows_data).";\n" if $#rows_data > -1 && $dbobject->[DBOBJECT_ENGINE] ne 'MRG_MYISAM';

	return $create_string.$data_string;
	
}

sub name {
	return $_[0]->[DBOBJECT_NAME];
}

sub columns {
	return $_[0]->[DBOBJECT_COLUMNS];
}

sub keys {
	return $_[0]->[DBOBJECT_KEYS];
}

sub data {
	return $_[0]->[DBOBJECT_DATA];
}

sub engine {
	return $_[0]->[DBOBJECT_ENGINE];
}

1;
