# Copyright (C) 2009 Sun Microsystems, Inc. All rights reserved.
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

package GenTest::App::Gendata;

@ISA = qw(GenTest);

use strict;
use DBI;
use Carp;
use GenTest;
use GenTest::Constants;
use GenTest::Random;
use GenTest::Executor;

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


use constant GD_SPEC => 0;
use constant GD_DEBUG => 1;
use constant GD_DSN => 2;
use constant GD_SEED => 3;
use constant GD_ENGINE => 4;
use constant GD_ROWS => 5;
use constant GD_VIEWS => 6;
use constant GD_VARCHAR_LENGTH => 7;
use constant GD_SERVER_ID => 8;
use constant GD_SQLTRACE => 9;
use constant GD_NOTNULL => 10;
use constant GD_SHORT_COLUMN_NAMES => 11;
use constant GD_STRICT_FIELDS => 12;

sub new {
    my $class = shift;
    
    my $self = $class->SUPER::new({
        'spec_file' => GD_SPEC,
        'debug' => GD_DEBUG,
        'dsn' => GD_DSN,
        'seed' => GD_SEED,
        'engine' => GD_ENGINE,
        'rows' => GD_ROWS,
        'views' => GD_VIEWS,
        'varchar_length' => GD_VARCHAR_LENGTH,
        'notnull' => GD_NOTNULL,	
        'short_column_names' => GD_SHORT_COLUMN_NAMES,	
        'strict_fields' => GD_STRICT_FIELDS,	
        'server_id' => GD_SERVER_ID,
        'sqltrace' => GD_SQLTRACE},@_);

    if (not defined $self->[GD_SEED]) {
        $self->[GD_SEED] = 1;
    } elsif ($self->[GD_SEED] eq 'time') {
        $self->[GD_SEED] = time();
        say("Converting --seed=time to --seed=".$self->[GD_SEED]);
    }
    
    return $self;
}


sub spec_file {
return $_[0]->[GD_SPEC];
}


sub debug {
    return $_[0]->[GD_DEBUG];
}


sub dsn {
    return $_[0]->[GD_DSN];
}


sub seed {
    return $_[0]->[GD_SEED];
}


sub engine {
    return $_[0]->[GD_ENGINE];
}


sub rows {
    return $_[0]->[GD_ROWS];
}


sub views {
    return $_[0]->[GD_VIEWS];
}


sub varchar_length {
    return $_[0]->[GD_VARCHAR_LENGTH];
}


sub server_id {
    return $_[0]->[GD_SERVER_ID];
}

sub sqltrace {
    return $_[0]->[GD_SQLTRACE];
}

sub short_column_names {
    return $_[0]->[GD_SHORT_COLUMN_NAMES];
}


sub strict_fields {
    return $_[0]->[GD_STRICT_FIELDS];
}


sub run {
    my ($self) = @_;

    my $spec_file = $self->spec_file();
    
    my $prng = GenTest::Random->new(
        seed => $self->seed(),
        varchar_length => $self->varchar_length()
        );

    my $executor = GenTest::Executor->newFromDSN($self->dsn());
    $executor->init();

#  
# The specification file is actually a perl script, so we read it by
# eval()-ing it
#  
    
    my ($tables, $fields, $data, $schemas);  # Specification as read
                                             # from the spec file.
    my (@table_perms, @field_perms, @data_perms, @schema_perms);	# Specification
                                                                    # after
                                                                    # defaults
                                                                    # have
                                                                    # been
                                                                    # substituted

    if ($spec_file ne '') {
        open(CONF , $spec_file) or croak "unable to open specification file '$spec_file': $!";
        read(CONF, my $spec_text, -s $spec_file);
        eval ($spec_text);
        croak "Unable to load $spec_file: $@" if $@;
    }

    $executor->execute("SET SQL_MODE= 'NO_ENGINE_SUBSTITUTION'") if $executor->type == DB_MYSQL;
    $executor->execute("SET STORAGE_ENGINE='".$self->engine()."'") 
        if $self->engine() ne '' and ($executor->type == DB_MYSQL or $executor->type == DB_DRIZZLE);

    if (defined $schemas) {
        push(@schema_perms, @$schemas);
        $executor->defaultSchema(@schema_perms[0]);
    } else {
        push(@schema_perms, $executor->defaultSchema());
    }

    $table_perms[TABLE_ROW] = (defined $self->rows() ? [ $self->rows() ] : undef ) || $tables->{rows} || [0, 1, 2, 10, 100];
    $table_perms[TABLE_ENGINE] = defined $self->engine() ? [ $self->engine() ] : $tables->{engines};
    $table_perms[TABLE_CHARSET] = $tables->{charsets} || [ undef ];
    $table_perms[TABLE_COLLATION] = $tables->{collations} || [ undef ];
    $table_perms[TABLE_PARTITION] = $tables->{partitions} || [ undef ];
    $table_perms[TABLE_PK] = $tables->{pk} || $tables->{primary_key} || [ 'integer auto_increment' ];
    $table_perms[TABLE_ROW_FORMAT] = $tables->{row_formats} || [ undef ];
    
    $table_perms[TABLE_VIEWS] = $tables->{views} || (defined $self->views() ? [ $self->views() ] : undef );
    $table_perms[TABLE_MERGES] = $tables->{merges} || undef ;
    
    $table_perms[TABLE_NAMES] = $tables->{names} || [ ];
    
    $field_perms[FIELD_TYPE] = $fields->{types} || [ 'int', 'varchar', 'date', 'time', 'datetime' ];
    if (not ($executor->type == DB_MYSQL or $executor->type == DB_DRIZZLE or $executor->type==DB_DUMMY)) {
        my @datetimestuff = grep(/date|time/,@{$fields->{types}});
        if ($#datetimestuff > -1) {
            croak "Dates and times are severly broken. Cannot be used for other than MySQL/Drizzle";
        }
    }
    if ($self->strict_fields) {
        $field_perms[FIELD_NULLABILITY] = $fields->{null} || $fields->{nullability} || [ undef ];
        $field_perms[FIELD_DEFAULT] = $fields->{default} || [ undef ];
        $field_perms[FIELD_SIGN] = $fields->{sign} || [ undef ];
        $field_perms[FIELD_INDEX] = $fields->{indexes} || $fields->{keys} || [ undef ];
        $field_perms[FIELD_CHARSET] =  $fields->{charsets} || [ undef ];
        $field_perms[FIELD_COLLATION] = $fields->{collations} || [ undef ];
    } else {
        $field_perms[FIELD_NULLABILITY] = $fields->{null} || $fields->{nullability} || [ (defined $self->[GD_NOTNULL] ? 'NOT NULL' : undef) ];
        $field_perms[FIELD_SIGN] = $fields->{sign} || [ undef ];
        $field_perms[FIELD_INDEX] = $fields->{indexes} || $fields->{keys} || [ undef, 'KEY' ];
        $field_perms[FIELD_CHARSET] =  $fields->{charsets} || [ undef ];
        $field_perms[FIELD_COLLATION] = $fields->{collations} || [ undef ];
    }
    

    $data_perms[DATA_NUMBER] = $data->{numbers} || ['digit', 'digit', 'digit', 'digit', (defined $self->[GD_NOTNULL] ? 'digit' : 'null') ];	# 20% NULL values
    $data_perms[DATA_STRING] = $data->{strings} || ['letter', 'letter', 'letter', 'letter', (defined $self->[GD_NOTNULL] ? 'letter' : 'null') ];
    $data_perms[DATA_BLOB] = $data->{blobs} || [ 'data', 'data', 'data', 'data', (defined $self->[GD_NOTNULL] ? 'data' : 'null') ];
    $data_perms[DATA_TEMPORAL] = $data->{temporals} || [ 'date', 'time', 'datetime', 'year', 'timestamp', (defined $self->[GD_NOTNULL] ? 'date' : 'null') ];
    $data_perms[DATA_ENUM] = $data->{enum} || ['letter', 'letter', 'letter', 'letter', (defined $self->[GD_NOTNULL] ? 'letter' : 'null') ];

    my @tables = (undef);
    my @myisam_tables;
    
    foreach my $cycle (TABLE_ROW, TABLE_ENGINE, TABLE_CHARSET, TABLE_COLLATION, TABLE_PARTITION, TABLE_PK, TABLE_ROW_FORMAT) {
        @tables = map {
            my $old_table = $_;
            if (not defined $table_perms[$cycle]) {
                $old_table;	# Retain old table, no permutations at this stage.
            } else {
                # Create several new tables, one for each allowed value in the current $cycle
                map {
                    my $new_perm = $_;
                    my @new_table = defined $old_table ? @$old_table : [];
                    $new_table[$cycle] = lc($new_perm);
                    \@new_table;
                } @{$table_perms[$cycle]};
            }
        } @tables;
    }
    
#
# Iteratively build the array of tables. We start with an empty array, and on each iteration
# we increase the size of the array to contain more combinations.
# 
# Then we do the same for fields.
#
    
    my @fields = (undef);
    
    foreach my $cycle (FIELD_TYPE, FIELD_NULLABILITY, FIELD_DEFAULT, FIELD_SIGN, FIELD_INDEX, FIELD_CHARSET, FIELD_COLLATION) {
        @fields = map {
            my $old_field = $_;
            if (not defined $field_perms[$cycle]) {
                $old_field;	# Retain old field, no permutations at this stage.
            } elsif (
                ($cycle == FIELD_SIGN) &&
                ($old_field->[FIELD_TYPE] !~ m{int|float|double|dec|numeric|fixed}sio) 
                ) {
                $old_field;	# Retain old field, sign does not apply to non-integer types
            } elsif (
                ($cycle == FIELD_CHARSET) &&
                ($old_field->[FIELD_TYPE] =~ m{bit|int|bool|float|double|dec|numeric|fixed|blob|date|time|year|binary}sio)
                ) {
                $old_field;	# Retain old field, charset does not apply to integer types
            } else {
                # Create several new fields, one for each allowed value in the current $cycle
                map {
                    my $new_perm = $_;
                    my @new_field = defined $old_field ? @$old_field : [];
                    $new_field[$cycle] = lc($new_perm);
                    \@new_field;
                } @{$field_perms[$cycle]};
            }
        } @fields;
    }
    
# If no fields were defined, continue with just the primary key.
    @fields = () if ($#fields == 0) && ($fields[0]->[FIELD_TYPE] eq '');
    my $field_no=0;
    foreach my $field_id (0..$#fields) {
        my $field = $fields[$field_id];
        next if not defined $field;
        my @field_copy = @$field;
        
#	$field_copy[FIELD_INDEX] = 'nokey' if $field_copy[FIELD_INDEX] eq '';
        
        my $field_name;
        if ($self->short_column_names) {
            $field_name = 'c'.($field_no++);
        } else {
            $field_name = "col_".join('_', grep { $_ ne '' } @field_copy);
            $field_name =~ s{[^A-Za-z0-9]}{_}sgio;
            $field_name =~ s{ }{_}sgio;
            $field_name =~ s{_+}{_}sgio;
            $field_name =~ s{_+$}{}sgio;
            
        }
        $field->[FIELD_NAME] = $field_name;
        
        if (
            ($field_copy[FIELD_TYPE] =~ m{set|enum}sio) &&
            ($field_copy[FIELD_TYPE] !~ m{\(}sio )
            ) {
            $field_copy[FIELD_TYPE] .= " (".join(',', map { "'$_'" } ('a'..'z') ).")";
        }
        
        if (
            ($field_copy[FIELD_TYPE] =~ m{char}sio) &&
            ($field_copy[FIELD_TYPE] !~ m{\(}sio)
            ) {
            $field_copy[FIELD_TYPE] .= ' (1)';
        }
        
        $field_copy[FIELD_CHARSET] = "/*+mysql: CHARACTER SET ".$field_copy[FIELD_CHARSET]."*/" if $field_copy[FIELD_CHARSET] ne '';
        $field_copy[FIELD_COLLATION] = "/*mysql: COLLATE ".$field_copy[FIELD_COLLATION]."*/" if $field_copy[FIELD_COLLATION] ne '';
        
        my $key_len;
        
        if (
            ($field_copy[FIELD_TYPE] =~ m{blob|text|binary}sio ) &&  
            ($field_copy[FIELD_TYPE] !~ m{\(}sio )
            ) {
            $key_len = " (255)";
        }
        
        if (
            ($field_copy[FIELD_INDEX] ne 'nokey') &&
            ($field_copy[FIELD_INDEX] ne '')
            ) {
            $field->[FIELD_INDEX_SQL] = $field_copy[FIELD_INDEX]." (`$field_name` $key_len)";
        }
        
        delete $field_copy[FIELD_INDEX]; # do not include FIELD_INDEX in the field description
        
        $fields[$field_id]->[FIELD_SQL] = "`$field_name` ". join(' ' , grep { $_ ne '' } @field_copy);
        
        if (!$self->strict_fields && $field_copy[FIELD_TYPE] =~ m{timestamp}sio ) {
            if (defined $self->[GD_NOTNULL]) {
                $field->[FIELD_SQL] .= ' NOT NULL';
            } else {
                $field->[FIELD_SQL] .= ' NULL DEFAULT 0';
            }
        }
    }

    foreach my $table_id (0..$#tables) {
        my $table = $tables[$table_id];
        my @table_copy = @$table;
        
        if ($#{$table_perms[TABLE_NAMES]} > -1) {
            $table->[TABLE_NAME] = shift @{$table_perms[TABLE_NAMES]};
        } else {
            my $table_name;
            $table_name = "table".join('_', grep { $_ ne '' } @table_copy);
            $table_name =~ s{[^A-Za-z0-9]}{_}sgio;
            $table_name =~ s{ }{_}sgio;
            $table_name =~ s{_+}{_}sgio;
            $table_name =~ s{auto_increment}{autoinc}siog;
            $table_name =~ s{partition_by}{part_by}siog;
            $table_name =~ s{partition}{part}siog;
            $table_name =~ s{partitions}{parts}siog;
            $table_name =~ s{values_less_than}{}siog;
            $table_name =~ s{integer}{int}siog;
            
            if (
                (uc($table_copy[TABLE_ENGINE]) eq 'MYISAM') ||
                ($table_copy[TABLE_ENGINE] eq '')
                ) {
                push @myisam_tables, $table_name;
            }
            
            $table->[TABLE_NAME] = $table_name;
        }
        
        $table_copy[TABLE_ENGINE] = "ENGINE=".$table_copy[TABLE_ENGINE] if $table_copy[TABLE_ENGINE] ne '';
        $table_copy[TABLE_ROW_FORMAT] = "ROW_FORMAT=".$table_copy[TABLE_ROW_FORMAT] if $table_copy[TABLE_ROW_FORMAT] ne '';
        $table_copy[TABLE_CHARSET] = "CHARACTER SET ".$table_copy[TABLE_CHARSET] if $table_copy[TABLE_CHARSET] ne '';
        $table_copy[TABLE_COLLATION] = "COLLATE ".$table_copy[TABLE_COLLATION] if $table_copy[TABLE_COLLATION] ne '';
        $table_copy[TABLE_PARTITION] = "/*!50100 PARTITION BY ".$table_copy[TABLE_PARTITION]." */" if $table_copy[TABLE_PARTITION] ne '';
        
        delete $table_copy[TABLE_ROW];	# Do not include number of rows in the CREATE TABLE
        delete $table_copy[TABLE_PK];	# Do not include PK definition at the end of CREATE TABLE
        
        $table->[TABLE_SQL] = join(' ' , grep { $_ ne '' } @table_copy);
    }	

    foreach my $schema (@schema_perms) {
        $executor->execute("CREATE SCHEMA /*!IF NOT EXISTS*/ $schema");
        $executor->sqltrace($self->sqltrace);
        $executor->currentSchema($schema);

    foreach my $table_id (0..$#tables) {
        my $table = $tables[$table_id];
        my @table_copy = @$table;
        my @fields_copy = @fields;
        
        if (uc($table->[TABLE_ENGINE]) eq 'FALCON') {
            @fields_copy =  grep {
                !($_->[FIELD_TYPE] =~ m{blob|text}io && $_->[FIELD_INDEX] ne '')
            } @fields ;
        }
        
        say("# Creating ".$executor->getName().
            " table: $schema.$table_copy[TABLE_NAME]; engine: $table_copy[TABLE_ENGINE]; rows: $table_copy[TABLE_ROW] .");
        
        if ($table_copy[TABLE_PK] ne '') {
            my $pk_field;
            $pk_field->[FIELD_NAME] = 'pk';
            $pk_field->[FIELD_TYPE] = $table_copy[TABLE_PK];
            $pk_field->[FIELD_INDEX] = 'primary key';
            $pk_field->[FIELD_INDEX_SQL] = 'primary key (pk)';
            $pk_field->[FIELD_SQL] = 'pk '.$table_copy[TABLE_PK];
            push @fields_copy, $pk_field;
        }
        
        # Make field ordering in every table different.
        # This exposes bugs caused by different physical field placement
        
        $prng->shuffleArray(\@fields_copy);
        
        $executor->execute("DROP TABLE /*! IF EXISTS*/ $table->[TABLE_NAME]");
        
        # Compose the CREATE TABLE statement by joining all fields and indexes and appending the table options
        
        my @field_sqls = join(",\n", map { $_->[FIELD_SQL] } @fields_copy);
        
        my @index_fields;
        if ($executor->type() == DB_MYSQL || $executor->type() == DB_DRIZZLE) {
            @index_fields = grep { $_->[FIELD_INDEX_SQL] ne '' } @fields_copy;
        } else {
            ## Just keep the primary keys.....
            @index_fields = grep { $_->[FIELD_INDEX_SQL] =~ m/primary/ } @fields_copy;
        }
        
        my $index_sqls = $#index_fields > -1 ? join(",\n", map { $_->[FIELD_INDEX_SQL] } @index_fields) : undef;

        $executor->execute("CREATE TABLE `$table->[TABLE_NAME]` (\n".join(",\n/*Indices*/\n", grep { defined $_ } (@field_sqls, $index_sqls) ).") ".$table->[TABLE_SQL]);
        
        if (not ($executor->type() == DB_MYSQL || 
                 $executor->type() == DB_DRIZZLE)) {
            @index_fields = grep { $_->[FIELD_INDEX_SQL] ne '' } @fields_copy;
            foreach my $idx (@index_fields) {
                my $key_sql = $idx->[FIELD_INDEX_SQL];
                if ($key_sql =~ m/^key \((`[a-z0-9_]*)/) {
                    $executor->execute("CREATE INDEX idx_".
                                       $table->[TABLE_NAME]."_$1".
                                       " ON ".$table->[TABLE_NAME]."($1)");
                }
            }
        }
        
        
        
        if (defined $table_perms[TABLE_VIEWS]) {
            foreach my $view_id (0..$#{$table_perms[TABLE_VIEWS]}) {
		my $view_name;
		if ($#{$table_perms[TABLE_VIEWS]} == 0) {
		   $view_name = 'view_'.$table->[TABLE_NAME];
		} else {
		   $view_name = 'view_'.$table->[TABLE_NAME]."_$view_id";
		}

		if ($table_perms[TABLE_VIEWS]->[$view_id] ne '') {
	                $executor->execute("CREATE OR REPLACE ALGORITHM=".uc($table_perms[TABLE_VIEWS]->[$view_id])." VIEW `$view_name` AS SELECT * FROM `$table->[TABLE_NAME]`");
		} else {
	                $executor->execute("CREATE OR REPLACE VIEW `$view_name` AS SELECT * FROM `$table->[TABLE_NAME]`");
		}
            }
        }
        
        if ($executor->type == DB_MYSQL ) {
            $executor->execute("ALTER TABLE `$table->[TABLE_NAME]` DISABLE KEYS");
            
            if ($table->[TABLE_ROW] > 100) {
                $executor->execute("SET AUTOCOMMIT=OFF");
                $executor->execute("START TRANSACTION");
            }
        }
        
        my @row_buffer;
        foreach my $row_id (1..$table->[TABLE_ROW]) {
            my @data;
            foreach my $field (@fields_copy) {
                my $value;
                my $quote = 0;
                if ($field->[FIELD_INDEX] eq 'primary key') {
                    if ($field->[FIELD_TYPE] =~ m{auto_increment}sio) {
                        if ($executor->type == DB_MYSQL or $executor->type == DB_DRIZZLE) {
                            $value = undef;		# Trigger auto-increment by inserting NULLS for PK
                        } else {
                            $value = 'DEFAULT';
                        }
                    } else {
			if ($field->[FIELD_TYPE] =~ m{datetime|timestamp}sgio) {
				$value = "FROM_UNIXTIME(UNIX_TIMESTAMP('2000-01-01') + $row_id)";
			} elsif ($field->[FIELD_TYPE] =~ m{date}sgio) {
				$value = "FROM_DAYS(TO_DAYS('2000-01-01') + $row_id)";
			} elsif ($field->[FIELD_TYPE] =~ m{time}sgio) {
				$value = "SEC_TO_TIME($row_id)";
			} else {
	                        $value = $row_id;	# Otherwise, insert sequential numbers
			}
                    }
                } else {
                    my (@possible_values, $value_type);
                    
                    if ($field->[FIELD_TYPE] =~ m{date|time|year}sio) {
                        $value_type = DATA_TEMPORAL;
                        $quote = 1;
                    } elsif ($field->[FIELD_TYPE] =~ m{blob|text|binary}sio) {
                        $value_type = DATA_BLOB;
                        $quote = 1;
                    } elsif ($field->[FIELD_TYPE] =~ m{int|float|double|dec|numeric|fixed|bool|bit}sio) {
                        $value_type = DATA_NUMBER;
                    } elsif ($field->[FIELD_TYPE] eq 'enum') {
                        $value_type = DATA_ENUM;
                        $quote = 1;
                    } else {
                        $value_type = DATA_STRING;
                        $quote = 1;
                    }
                    
                    if (($field->[FIELD_NULLABILITY] eq 'not null') || ($self->[GD_NOTNULL])) {
                        # Remove NULL from the list of allowed values
                        @possible_values = grep { lc($_) ne 'null' } @{$data_perms[$value_type]};
                    } else {
                        @possible_values = @{$data_perms[$value_type]};
                    }
                    
                    croak("# Unable to generate data for field '$field->[FIELD_TYPE] $field->[FIELD_NULLABILITY]'") if $#possible_values == -1;
                    
                    my $possible_value = $prng->arrayElement(\@possible_values);
                    $possible_value = $field->[FIELD_TYPE] if not defined $possible_value;
                    
                    if ($prng->isFieldType($possible_value)) {
                        $value = $prng->fieldType($possible_value);
                    } else {
                        $value = $possible_value;		# A simple string literal as specified
                    }
                }
                
                ## Quote if necessary
                if ($value =~ m{load_file}sio) {
                    push @data, defined $value ? $value : "NULL";
                } elsif ($quote) {
                    $value =~ s{'}{\\'}sgio;
                    push @data, defined $value ? "'$value'" : "NULL";
                } else {
                    push @data, defined $value ? $value : "NULL";
                }	
            }
            
            push @row_buffer, " (".join(', ', @data).") ";
            
            if (
                (($row_id % 50) == 0) ||
                ($row_id == $table->[TABLE_ROW])
                ) {
                my $insert_result = $executor->execute("INSERT /*! IGNORE */ INTO $table->[TABLE_NAME] VALUES ".join(', ', @row_buffer));
                return $insert_result->status() if $insert_result->status() >= STATUS_CRITICAL_FAILURE;
                @row_buffer = ();
            }
            
            if (($row_id % 10000) == 0) {
                $executor->execute("COMMIT");
                say("# Progress: loaded $row_id out of $table->[TABLE_ROW] rows");
            }
        }
        if ($executor->type == DB_MYSQL ) {
            $executor->execute("COMMIT");
            
            $executor->execute("ALTER TABLE `$table->[TABLE_NAME]` ENABLE KEYS");
        }
    }
    }
    
    if ($executor->type == DB_MYSQL or $executor->type == DB_DRIZZLE) {
        $executor->execute("COMMIT");
    }
    
    if (
        (defined $table_perms[TABLE_MERGES]) && 
        ($#myisam_tables > -1)
        ) {
        foreach my $merge_id (0..$#{$table_perms[TABLE_MERGES]}) {
            my $merge_name = 'merge_'.$merge_id;
            $executor->execute("CREATE TABLE `$merge_name` LIKE `".$myisam_tables[0]."`");
            $executor->execute("ALTER TABLE `$merge_name` ENGINE=MERGE UNION(".join(',',@myisam_tables).") ".uc($table_perms[TABLE_MERGES]->[$merge_id]));
        }
    }

    $executor->currentSchema(@schema_perms[0]);
    return STATUS_OK;
}

1;
