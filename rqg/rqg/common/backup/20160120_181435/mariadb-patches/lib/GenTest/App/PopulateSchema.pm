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


package GenTest::App::PopulateSchema;

@ISA = qw(GenTest);

use strict;
use DBI;
use Carp;
use GenTest;
use GenTest::Constants;
use GenTest::Random;
use GenTest::Executor;

use DBServer::MySQL::MySQLd;

use Data::Dumper;

use constant FIELD_TYPE			=> 0;
use constant FIELD_CHARSET		=> 1;
use constant FIELD_COLLATION		=> 2;
use constant FIELD_SIGN			=> 3;
use constant FIELD_NULLABILITY		=> 4;
use constant FIELD_INDEX		=> 5;
use constant FIELD_AUTO_INCREMENT	=> 6;
use constant FIELD_SQL			=> 7;
use constant FIELD_INDEX_SQL		=> 8;
use constant FIELD_NAME			=> 9;
use constant FIELD_DEFAULT => 10;

use constant TABLE_ROW		=> 0;
use constant TABLE_ENGINE	=> 1;
use constant TABLE_CHARSET	=> 2;
use constant TABLE_COLLATION	=> 3;
use constant TABLE_ROW_FORMAT	=> 4;
use constant TABLE_PARTITION	=> 5;
use constant TABLE_PK		=> 6;
use constant TABLE_SQL		=> 7;
use constant TABLE_NAME		=> 8;
use constant TABLE_VIEWS	=> 9;
use constant TABLE_MERGES	=> 10;
use constant TABLE_NAMES	=> 11;

use constant DATA_NUMBER	=> 0;
use constant DATA_STRING	=> 1;
use constant DATA_BLOB		=> 2;
use constant DATA_TEMPORAL	=> 3;
use constant DATA_ENUM		=> 4;


use constant PS_SPEC => 0;
use constant PS_DEBUG => 1;
use constant PS_DSN => 2;
use constant PS_SEED => 3;
use constant PS_ROWS => 4;
use constant PS_SERVER_ID => 5;
use constant PS_SQLTRACE => 6;
use constant PS_BASEDIR => 7;
use constant PS_TABLES => 8;

sub new {
	my $class = shift;

	my $self = $class->SUPER::new({
		'schema_file' => PS_SPEC,
		'debug' => PS_DEBUG,
		'dsn' => PS_DSN,
		'seed' => PS_SEED,
		'rows' => PS_ROWS,
		'server_id' => PS_SERVER_ID,
		'sqltrace' => PS_SQLTRACE,
		'basedir' => PS_BASEDIR,
		'tables' => PS_TABLES,
	},@_);

	if (not defined $self->[PS_SEED]) {
		$self->[PS_SEED] = 1;
	} elsif ($self->[PS_SEED] eq 'time') {
		$self->[PS_SEED] = time();
		say("Converting --seed=time to --seed=".$self->[PS_SEED]);
	}

	return $self;
}


sub schema_file {
	return $_[0]->[PS_SPEC];
}


sub tables {
	return $_[0]->[PS_TABLES];
}


sub debug {
	return $_[0]->[PS_DEBUG];
}


sub dsn {
	return $_[0]->[PS_DSN];
}


sub seed {
	return $_[0]->[PS_SEED];
}


sub rows {
	return $_[0]->[PS_ROWS];
}


sub server_id {
	return $_[0]->[PS_SERVER_ID];
}

sub sqltrace {
	return $_[0]->[PS_SQLTRACE];
}


sub basedir {
	return $_[0]->[PS_BASEDIR];
}

sub run {
	my ($self) = @_;

	my $schema_file = $self->schema_file();
	my $tables = $self->tables();

	my $executor = GenTest::Executor->newFromDSN($self->dsn());
	$executor->init();

	# The specification file should be an SQL script, and we need to feed it to the server through the client

	my $mysql_client_path = 'mysql';
	my $basedir = $self->basedir();
	if ($basedir) {
		$mysql_client_path = DBServer::MySQL::MySQLd->_find( 
			[$basedir,$basedir.'/..',$basedir.'/../debug/',$basedir.'/../relwithdebinfo/'],
		   ['client','bin'],
		   'mysql.exe', 'mysql' );
	} else {
		say("WARNING: basedir was not defined, relying on MySQL client being on the default path");
	}
	unless ($mysql_client_path) {
		say("ERROR: Could not find MySQL client");
		return STATUS_ENVIRONMENT_FAILURE;
	}

	# If the schema file was defined,
	# we only want to populate the tables that were created while loading the schema file, 
	# so we'll store the list of existing tables with their creation times

	my @tables_to_populate = ();

	if ($schema_file) {
		$tables = $executor->execute("SELECT CONCAT(TABLE_SCHEMA, '.', TABLE_NAME), CREATE_TIME FROM INFORMATION_SCHEMA.TABLES "
				. "WHERE TABLE_SCHEMA NOT IN ('mysql','performance_schema','information_schema')")->data();
		my %old_tables = ();
		foreach (@$tables) {
			$old_tables{$_->[0]} = $_->[1];
		}
	
		my $port = $executor->port();
		system("$mysql_client_path --port=$port --protocol=tcp -uroot test < $schema_file");
		if ($?) {
			say("ERROR: failed to load $schema_file through MySQL client");
			return STATUS_ENVIRONMENT_FAILURE;
		}

		# Now we will get the list of tables again. We don't care about those which already have rows in them
		# (even if they are new, we consider them populated from the schema file);
		# for empty ones, we'll compare their creation time with the stored one

		$tables = $executor->execute("SELECT CONCAT(TABLE_SCHEMA, '.', TABLE_NAME), CREATE_TIME FROM INFORMATION_SCHEMA.TABLES "
				. "WHERE TABLE_SCHEMA NOT IN ('mysql','performance_schema','information_schema') AND TABLE_ROWS = 0")->data();
		foreach (@$tables) {
			push @tables_to_populate, $_->[0] unless defined $old_tables{$_->[0]} and $old_tables{$_->[0]} eq $_->[1];
		}
	} 
	# If the table list was defined,
	# we want to populate the given tables
	else {
		@tables_to_populate = @$tables;
	}
	
	say("Tables to populate: @tables_to_populate");

	# TODO: be smarter about rows count
	foreach my $t (@tables_to_populate) {
		populate_table($self, $executor, $t, $self->rows());
	}

	return STATUS_OK;

}

sub populate_table 
{
	my ($self, $executor, $table_name, $rows) = @_;
	$rows = 100 unless defined $rows;
	my $prng = GenTest::Random->new(
		seed => $self->seed()
	);


	# TODO: be smarter about foreign keys
	$executor->execute("SET FOREIGN_KEY_CHECKS = 0");
	$executor->execute("SET GLOBAL FOREIGN_KEY_CHECKS = 0");
	
	my $columns = $executor->execute("SELECT COLUMN_TYPE, COALESCE(COLUMN_DEFAULT,'NULL'), IS_NULLABLE, EXTRA, COLUMN_KEY, COLUMN_NAME "
			. "FROM INFORMATION_SCHEMA.COLUMNS WHERE CONCAT(TABLE_SCHEMA,'.',TABLE_NAME) = '$table_name' ORDER BY ORDINAL_POSITION")->data();

	my $row_count = 0;

	$executor->execute("START TRANSACTION");

	VALUES:
	while ($row_count < $rows) 
	{
		my %unique_values = ();
		my $stmt = "INSERT IGNORE INTO $table_name VALUES (";
		foreach (1..20) {

			foreach my $c (@$columns) {

				my $val;
				my $unique_ok = 0;
				do {
					my @possible_vals = ();

					# only use defaults if not a part of a unique key
					if ( $c->[4] ne 'PRI' and $c->[4] ne 'UNI' ) { 
						if ($c->[2] eq 'YES') {
							push @possible_vals, 'NULL';
						}
						if ($c->[1] ne 'NULL') {
							push @possible_vals, $c->[1];
						}
						$unique_ok = 1;
					}

					if ($c->[3] eq 'auto_increment') {
						push @possible_vals, 'NULL';
						$unique_ok = 1;
					} 
					else {
						if ($c->[0] =~ /^((?:(?:big|small|medium|tiny)?int)|double|float|decimal|numeric)(?:\(\d+,?\d?\))?\s*(unsigned)?/) {
							my $type = $1 . ($2 ? '_unsigned' : '');
							push @possible_vals, ( $type, 'digit', 'tinyint_unsigned' );
						} 
						elsif ($c->[0] =~ /^((?:(?:long|medium|tiny)?text)|(?:var)?(?:char|binary)\(\d+\)?)/) {
							@possible_vals = (@possible_vals, 'letter','_english','_states', 'char(4)', '_english',$1);
						} 
						elsif ($c->[0] =~ /geometry/) {
							push @possible_vals, 'NULL';
						} 
						else {
							push @possible_vals, $c->[0];
						}
					}
					$val = $prng->arrayElement(\@possible_vals);
                    
					if ($val ne 'NULL' and $prng->isFieldType($val)) {
						$val = $prng->fieldType($val);
					} 
					unless ($val eq 'NULL' or $val =~ /^LOAD_.+\(/ or $val =~ /^CURRENT_TIMESTAMP/) {
		            $val =~ s{'}{\\'}sgio;
						$val = "'".$val."'"; 
					}

					unless ($unique_ok) {
						if (not defined ${$unique_values{$c->[5]}}->{$val}) {
							${$unique_values{$c->[5]}}->{$val} = 1;
							$unique_ok = 1;
						}
					}
				} until $unique_ok;

				$stmt .= "$val,";

			}
			# remove the last comma in the value list and add the bracket
			chop $stmt; 
			$stmt .= '),(';
			$row_count++;
			last if $row_count >= $rows;
		}
		chop $stmt; chop $stmt; # Remove the last ,(
		$executor->execute("$stmt");
		if ($row_count >= $rows) {
			say("Inserted ~$row_count rows into $table_name");
		} 
		elsif ($row_count%10000 == 0) {
			say("Inserted ~$row_count rows...")
		}
	}

	$executor->execute("COMMIT");
}

1;
