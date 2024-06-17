# Copyright (c) 2008,2012 Oracle and/or its affiliates. All rights reserved.
# Copyright (c) 2013, Monty Program Ab.
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

package GenTest::Executor;

require Exporter;
@ISA = qw(GenTest Exporter);

@EXPORT = qw(
	EXECUTOR_RETURNED_ROW_COUNTS
	EXECUTOR_AFFECTED_ROW_COUNTS
	EXECUTOR_EXPLAIN_COUNTS
	EXECUTOR_EXPLAIN_QUERIES
	EXECUTOR_ERROR_COUNTS
	EXECUTOR_STATUS_COUNTS

	FETCH_METHOD_AUTO
	FETCH_METHOD_STORE_RESULT
	FETCH_METHOD_USE_RESULT

	EXECUTOR_FLAG_SILENT
	EXECUTOR_FLAG_PERFORMANCE
	EXECUTOR_FLAG_HASH_DATA
);

use strict;
use Carp;
use Data::Dumper;
use GenTest;
use GenTest::Constants;

use constant EXECUTOR_DSN			=> 0;
use constant EXECUTOR_DBH			=> 1;
use constant EXECUTOR_ID			=> 2;
use constant EXECUTOR_RETURNED_ROW_COUNTS	=> 3;
use constant EXECUTOR_AFFECTED_ROW_COUNTS	=> 4;
use constant EXECUTOR_EXPLAIN_COUNTS		=> 5;
use constant EXECUTOR_EXPLAIN_QUERIES		=> 6;
use constant EXECUTOR_ERROR_COUNTS		=> 7;
use constant EXECUTOR_STATUS_COUNTS		=> 8;
use constant EXECUTOR_DEFAULT_SCHEMA		=> 9;
use constant EXECUTOR_SCHEMA_METADATA		=> 10;
use constant EXECUTOR_COLLATION_METADATA	=> 11;
use constant EXECUTOR_META_CACHE		=> 12;
use constant EXECUTOR_CHANNEL			=> 13;
use constant EXECUTOR_SQLTRACE			=> 14;
use constant EXECUTOR_NO_ERR_FILTER             => 15;
use constant EXECUTOR_FETCH_METHOD		=> 16;
use constant EXECUTOR_CONNECTION_ID		=> 17;
use constant EXECUTOR_FLAGS			=> 18;
use constant EXECUTOR_HOST          => 19;
use constant EXECUTOR_PORT          => 20;
use constant EXECUTOR_END_TIME	    => 21;
use constant EXECUTOR_CURRENT_USER	    => 22;

use constant FETCH_METHOD_AUTO		=> 0;
use constant FETCH_METHOD_STORE_RESULT	=> 1;
use constant FETCH_METHOD_USE_RESULT	=> 2;

use constant EXECUTOR_FLAG_SILENT	=> 1;
use constant EXECUTOR_FLAG_PERFORMANCE	=> 2;
use constant EXECUTOR_FLAG_HASH_DATA	=> 4;

my %global_schema_cache;

1;

sub new {
	my $class = shift;
	
	my $executor = $class->SUPER::new({
		'dsn'	=> EXECUTOR_DSN,
		'dbh'	=> EXECUTOR_DBH,
		'channel' => EXECUTOR_CHANNEL,
		'sqltrace' => EXECUTOR_SQLTRACE,
		'no-err-filter' => EXECUTOR_NO_ERR_FILTER,
		'fetch_method' => EXECUTOR_FETCH_METHOD,
		'end_time' => EXECUTOR_END_TIME
	}, @_);

	$executor->[EXECUTOR_FETCH_METHOD] = FETCH_METHOD_AUTO if not defined $executor->[EXECUTOR_FETCH_METHOD];
    
	return $executor;
}

sub newFromDSN {
	my ($self,$dsn,$channel) = @_;
	
	if ($dsn =~ m/^dbi:mysql:/i) {
		require GenTest::Executor::MySQL;
		return GenTest::Executor::MySQL->new(dsn => $dsn, channel => $channel);
	} elsif ($dsn =~ m/^dbi:drizzle:/i) {
		require GenTest::Executor::Drizzle;
		return GenTest::Executor::Drizzle->new(dsn => $dsn);
	} elsif ($dsn =~ m/^dbi:JDBC:.*url=jdbc:derby:/i) {
		require GenTest::Executor::JavaDB;
		return GenTest::Executor::JavaDB->new(dsn => $dsn);
	} elsif ($dsn =~ m/^dbi:Pg:/i) {
		require GenTest::Executor::Postgres;
		return GenTest::Executor::Postgres->new(dsn => $dsn);
    } elsif ($dsn =~ m/^dummy/) {
		require GenTest::Executor::Dummy;
		return GenTest::Executor::Dummy->new(dsn => $dsn);
	} else {
		say("Unsupported dsn: $dsn");
		exit(STATUS_ENVIRONMENT_FAILURE);
	}
}

sub channel {
    return $_[0]->[EXECUTOR_CHANNEL];
}

sub sendError {
    my ($self, $msg) = @_;
    $self->channel->send($msg);
}


sub dbh {
	return $_[0]->[EXECUTOR_DBH];
}

sub setDbh {
	$_[0]->[EXECUTOR_DBH] = $_[1];
}

sub host {
	return $_[0]->[EXECUTOR_HOST];
}

sub setHost {
	$_[0]->[EXECUTOR_HOST] = $_[1];
}

sub port {
	return $_[0]->[EXECUTOR_PORT];
}

sub setPort {
	$_[0]->[EXECUTOR_PORT] = $_[1];
}

sub currentUser {
	return $_[0]->[EXECUTOR_CURRENT_USER];
}

sub setCurrentUser {
	$_[0]->[EXECUTOR_CURRENT_USER] = $_[1];
}

sub setDbh {
	$_[0]->[EXECUTOR_DBH] = $_[1];
}

sub sqltrace {
    my ($self, $sqltrace) = @_;
    $self->[EXECUTOR_SQLTRACE] = $sqltrace if defined $sqltrace;
    return $self->[EXECUTOR_SQLTRACE];
}

sub noErrFilter {
    my ($self, $no_err_filter) = @_;
    $self->[EXECUTOR_NO_ERR_FILTER] = $no_err_filter if defined $no_err_filter;
    return $self->[EXECUTOR_NO_ERR_FILTER];
}

sub dsn {
	return $_[0]->[EXECUTOR_DSN];
}

sub setDsn {
	$_[0]->[EXECUTOR_DSN] = $_[1];
}

sub end_time {
	return $_[0]->[EXECUTOR_END_TIME];
}

sub set_end_time {
	$_[0]->[EXECUTOR_END_TIME] = $_[1];
}

sub id {
	return $_[0]->[EXECUTOR_ID];
}

sub setId {
	$_[0]->[EXECUTOR_ID] = $_[1];
}

sub fetchMethod {
	return $_[0]->[EXECUTOR_FETCH_METHOD];
}

sub connectionId {
	return $_[0]->[EXECUTOR_CONNECTION_ID];
}

sub setConnectionId {
	$_[0]->[EXECUTOR_CONNECTION_ID] = $_[1];
}

sub flags {
	return $_[0]->[EXECUTOR_FLAGS];
}

sub setFlags {
	$_[0]->[EXECUTOR_FLAGS] = $_[1];
}

sub type {
	my ($self) = @_;
	
	if (ref($self) eq "GenTest::Executor::JavaDB") {
		return DB_JAVADB;
	} elsif (ref($self) eq "GenTest::Executor::MySQL") {
		return DB_MYSQL;
	} elsif (ref($self) eq "GenTest::Executor::Drizzle") {
		return DB_DRIZZLE;
	} elsif (ref($self) eq "GenTest::Executor::Postgres") {
		return DB_POSTGRES;
    } elsif (ref($self) eq "GenTest::Executor::Dummy") {
        if ($self->dsn =~ m/mysql/) {
            return DB_MYSQL;
        } elsif ($self->dsn =~ m/postgres/) {
            return DB_POSTGRES;
        } if ($self->dsn =~ m/javadb/) {
            return DB_JAVADB;
        } else {
            return DB_DUMMY;
        }
	} else {
		return DB_UNKNOWN;
	}
}

my @dbid = ("Unknown","Dummy", "MySQL","Postgres","JavaDB","Drizzle");

sub getName {
    my ($self) = @_;
    return $dbid[$self->type()];
}

sub preprocess {
    my ($self, $query) = @_;

    my $id = $dbid[$self->type()];
    
    # Keep if match (+)

    # print "... $id before: $query \n";

    if (index($query, '/*+') > -1) {
        $query =~ s/\/\*\+[a-z:]*$id[a-z:]*:([^*]*)\*\//$1/gi;
    }

    # print "... after: $query \n";

    return $query;
}

## This array maps SQL State class (2 first letters) to a status. This
## list needs to be extended
my %class2status = (
    "07" => STATUS_SEMANTIC_ERROR, # dynamic SQL error
    "08" => STATUS_SEMANTIC_ERROR, # connection exception
    "22" => STATUS_SEMANTIC_ERROR, # data exception
    "23" => STATUS_SEMANTIC_ERROR, # integrity constraint violation
    "25" => STATUS_TRANSACTION_ERROR, # invalid transaction state
    "42" => STATUS_SYNTAX_ERROR    # syntax error or access rule
                                   # violation
    
    );

sub findStatus {
    my ($self, $state) = @_;

    my $class = substr($state, 0, 2);
    if (defined $class2status{$class}) {
        return $class2status{$class};
    } else {
        return STATUS_UNKNOWN_ERROR;
    }
}

sub defaultSchema {
    my ($self, $schema) = @_;
    if (defined $schema) {
        $self->[EXECUTOR_DEFAULT_SCHEMA] = $schema;
    }
    return $self->[EXECUTOR_DEFAULT_SCHEMA];
}

sub currentSchema {
    croak "FATAL ERROR: currentSchema not defined for ". (ref $_[0]);
}

sub getSchemaMetaData {
    croak "FATAL ERROR: getSchemaMetaData not defined for ". (ref $_[0]);
}

sub getCollationMetaData {
    carp "getCollationMetaData not defined for ". (ref $_[0]);
    return [[undef,undef]];
}


########### Metadata routines

sub cacheMetaData {
    my ($self, $redo) = @_;
    
    my $meta = {};

    if ($redo or not exists $global_schema_cache{$self->dsn()}) {
        say ("Caching schema metadata for ".$self->dsn());
        foreach my $row (@{$self->getSchemaMetaData()}) {
            my ($schema, $table, $type, $col, $key, $datatype) = @$row;
            $meta->{$schema}={} if not exists $meta->{$schema};
            $meta->{$schema}->{$type}={} if not exists $meta->{$schema}->{$type};
            $meta->{$schema}->{$type}->{$table}={} if not exists $meta->{$schema}->{$type}->{$table};
            $meta->{$schema}->{$type}->{$table}->{$col}= [$key,$datatype];
        }
	$global_schema_cache{$self->dsn()} = $meta;
    } else {
	$meta = $global_schema_cache{$self->dsn()};
    }

    $self->[EXECUTOR_SCHEMA_METADATA] = $meta;

    my $coll = {};
    foreach my $row (@{$self->getCollationMetaData()}) {
        my ($collation, $charset) = @$row;
        $coll->{$collation} = $charset;
    }
    $self->[EXECUTOR_COLLATION_METADATA] = $coll;

    $self->[EXECUTOR_META_CACHE] = {};
}

sub metaSchemas {
    my ($self) = @_;
    if (not defined $self->[EXECUTOR_META_CACHE]->{SCHEMAS}) {
        my $schemas = [sort keys %{$self->[EXECUTOR_SCHEMA_METADATA]}];
        if (not defined $schemas or $#$schemas < 0) {
            say "WARNING: No schemas found";
            $schemas = [ 'non_existing_schema' ];
        };
        $self->[EXECUTOR_META_CACHE]->{SCHEMAS} = $schemas;
    }
    return $self->[EXECUTOR_META_CACHE]->{SCHEMAS};
}

sub metaTables {
    my ($self, $schema) = @_;
    my $meta = $self->[EXECUTOR_SCHEMA_METADATA];

    $schema = $self->defaultSchema if not defined $schema;
    my $cachekey = "TAB-$schema";

    if (not defined $self->[EXECUTOR_META_CACHE]->{$cachekey}) {

        unless (scalar(keys %{$meta->{$schema}->{table}}) + scalar(keys %{$meta->{$schema}->{view}})) {
            # Give it another chance, maybe we created data after starting the test
            $self->cacheMetaData('redo');
				$meta = $self->[EXECUTOR_SCHEMA_METADATA]; 
        }
        my $tables = [sort ( keys %{$meta->{$schema}->{table}}, keys %{$meta->{$schema}->{view}} )];
        if (not defined $tables or $#$tables < 0) {
            say "WARNING: Schema '$schema' has no tables";
            $tables = [ 'non_existing_table' ];
        };
        $self->[EXECUTOR_META_CACHE]->{$cachekey} = $tables;
    }
    return $self->[EXECUTOR_META_CACHE]->{$cachekey};
    
}

sub metaBaseTables {
    my ($self, $schema) = @_;
    my $meta = $self->[EXECUTOR_SCHEMA_METADATA];

    $schema = $self->defaultSchema if not defined $schema;

    my $cachekey = "BASETAB-$schema";

    if (not defined $self->[EXECUTOR_META_CACHE]->{$cachekey}) {
        my $tables = [sort keys %{$meta->{$schema}->{table}}];
        if (not defined $tables or $#$tables < 0) {
            say "WARNING: Schema '$schema' has no base tables";
            $tables = [ 'non_existing_base_table' ];
        }
        $self->[EXECUTOR_META_CACHE]->{$cachekey} = $tables;
    }
    return $self->[EXECUTOR_META_CACHE]->{$cachekey};
    
}

sub metaViews {
    my ($self, $schema) = @_;
    my $meta = $self->[EXECUTOR_SCHEMA_METADATA];

    $schema = $self->defaultSchema if not defined $schema;

    my $cachekey = "VIEW-$schema";

    if (not defined $self->[EXECUTOR_META_CACHE]->{$cachekey}) {
        my $tables = [sort keys %{$meta->{$schema}->{view}}];
        if (not defined $tables or $#$tables < 0) {
            say "WARNING: Schema '$schema' has no views";
            $tables = [ 'non_existing_view' ];
        }
        $self->[EXECUTOR_META_CACHE]->{$cachekey} = $tables;
    }
    return $self->[EXECUTOR_META_CACHE]->{$cachekey};
    
}

sub metaColumns {
    my ($self, $table, $schema) = @_;
    my $meta = $self->[EXECUTOR_SCHEMA_METADATA];
    
    $schema = $self->defaultSchema if not defined $schema;
    $table = $self->metaTables($schema)->[0] if not defined $table;
    
    my $cachekey="COL-$schema-$table";
    
    if (not defined $self->[EXECUTOR_META_CACHE]->{$cachekey}) {
        my $cols;
        if ($meta->{$schema}->{table}->{$table}) {
            $cols = [sort keys %{$meta->{$schema}->{table}->{$table}}]
        } elsif ($meta->{$schema}->{view}->{$table}) {
            $cols = [sort keys %{$meta->{$schema}->{view}->{$table}}]
        } else {
            say "WARNING: Table '$table' in schema '$schema' has no columns";
            $cols = ['non_existing_column']
        } 
        $self->[EXECUTOR_META_CACHE]->{$cachekey} = $cols;
    }
    return $self->[EXECUTOR_META_CACHE]->{$cachekey};
}

sub metaColumnsIndexType {
    my ($self, $indextype, $table, $schema) = @_;
    my $meta = $self->[EXECUTOR_SCHEMA_METADATA];
    
    $schema = $self->defaultSchema if not defined $schema;
    $table = $self->metaTables($schema)->[0] if not defined $table;
    
    my $cachekey="COL-$indextype-$schema-$table";
    
    if (not defined $self->[EXECUTOR_META_CACHE]->{$cachekey}) {
        my $colref;
        if ($meta->{$schema}->{table}->{$table}) {
            $colref = $meta->{$schema}->{table}->{$table}
        } elsif ($meta->{$schema}->{view}->{$table}) {
            $colref = $meta->{$schema}->{view}->{$table};
        } else {
            say "WARNING: Table/view '$table' in schema '$schema' has no columns";
            $colref = { 'non_existing_column1' => ['ordinary','int'], 'non_existing_column2' => ['indexed','int'] };
        }

        # If the table is a view, don't bother looking for indexed columns, fall back to ordinary
        if ($meta->{$schema}->{view}->{$table} and ($indextype eq 'indexed' or $indextype eq 'primary')) {
            $indextype = 'ordinary'
        }
        my $cols;
        if ($indextype eq 'indexed') {
            $cols = [sort grep {$colref->{$_}->[0] eq $indextype or $colref->{$_}->[0] eq 'primary'} keys %$colref];
        } else {
            $cols = [sort grep {$colref->{$_}->[0] eq $indextype} keys %$colref];
        };
        if (not defined $cols or $#$cols < 0) {
            say "WARNING: Table/view '$table' in schema '$schema' has no '$indextype' columns (Might be caused by use of --views option in combination with grammars containing _field_indexed)";
           $cols = [ 'non_existing_column' ];
        }
        $self->[EXECUTOR_META_CACHE]->{$cachekey} = $cols;
    }
    return $self->[EXECUTOR_META_CACHE]->{$cachekey};
    
}

sub metaColumnsDataType {
    my ($self, $datatype, $table, $schema) = @_;
    my $meta = $self->[EXECUTOR_SCHEMA_METADATA];
    
    $schema = $self->defaultSchema if not defined $schema;
    $table = $self->metaTables($schema)->[0] if not defined $table;
    
    my $cachekey="COL-$datatype-$schema-$table";
    
    if (not defined $self->[EXECUTOR_META_CACHE]->{$cachekey}) {
        my $colref;
        if ($meta->{$schema}->{table}->{$table}) {
            $colref = $meta->{$schema}->{table}->{$table};
        } elsif ($meta->{$schema}->{view}->{$table}) {
            $colref = $meta->{$schema}->{view}->{$table};
        } else {
            say "WARNING: Table/view '$table' in schema '$schema' has no columns";
            $colref = { 'non_existing_column1' => ['ordinary','int'], 'non_existing_column2' => ['indexed','int'] }
        }

        my $cols = [sort grep {$colref->{$_}->[1] eq $datatype} keys %$colref];
        if (not defined $cols or $#$cols < 0) {
            say "WARNING: Table/view '$table' in schema '$schema' has no '$datatype' columns";
            $cols = [ 'non_existing_column' ];
        }
        $self->[EXECUTOR_META_CACHE]->{$cachekey} = $cols;
    }
    return $self->[EXECUTOR_META_CACHE]->{$cachekey};
    
}

sub metaColumnsDataIndexType {
    my ($self, $datatype, $indextype, $table, $schema) = @_;
    my $meta = $self->[EXECUTOR_SCHEMA_METADATA];
    
    $schema = $self->defaultSchema if not defined $schema;
    $table = $self->metaTables($schema)->[0] if not defined $table;
    
    my $cachekey="COL-$datatype-$indextype-$schema-$table";
    
    if (not defined $self->[EXECUTOR_META_CACHE]->{$cachekey}) {
        my $colref;
        if ($meta->{$schema}->{table}->{$table}) {
            $colref = $meta->{$schema}->{table}->{$table};
        } elsif ($meta->{$schema}->{view}->{$table}) {
            $colref = $meta->{$schema}->{view}->{$table};
        } else {
            say "WARNING: Table/view '$table' in schema '$schema' has no columns";
            $colref = { 'non_existing_column1' => ['ordinary','int'], 'non_existing_column2' => ['indexed','int'] };
        }

        # If the table is a view, don't bother looking for indexed columns, fall back to ordinary
        if ($meta->{$schema}->{view}->{$table} and ($indextype eq 'indexed' or $indextype eq 'primary')) {
            $indextype = 'ordinary'
        }
        my $cols_by_datatype = [sort grep {$colref->{$_}->[1] eq $datatype} keys %$colref];
        if (not defined $cols_by_datatype or $#$cols_by_datatype < 0) {
            say "WARNING: Table/view '$table' in schema '$schema' has no '$datatype' columns";
            $cols_by_datatype = [ 'non_existing_column' ];
        }
        my $cols_by_indextype;
        if ($indextype eq 'indexed') {
            $cols_by_indextype = [sort grep {$colref->{$_}->[0] eq $indextype or $colref->{$_}->[0] eq 'primary'} keys %$colref];
        } else {
            $cols_by_indextype = [sort grep {$colref->{$_}->[0] eq $indextype} keys %$colref];
        }
        if (not defined $cols_by_indextype or $#$cols_by_indextype < 0) {
            say "WARNING: Table/view '$table' in schema '$schema' has no '$indextype' columns (Might be caused by use of --views option in combination with grammars containing _field_indexed)";
            $cols_by_indextype = [ 'non_existing_column' ];
        }
            
        my $cols = GenTest::intersect_arrays($cols_by_datatype,$cols_by_indextype);
        $self->[EXECUTOR_META_CACHE]->{$cachekey} = $cols;
    }
    return $self->[EXECUTOR_META_CACHE]->{$cachekey};
    
}

sub metaColumnsDataTypeIndexTypeNot {
    my ($self, $datatype, $indextype, $table, $schema) = @_;
    my $meta = $self->[EXECUTOR_SCHEMA_METADATA];
    
    $schema = $self->defaultSchema if not defined $schema;
    $table = $self->metaTables($schema)->[0] if not defined $table;
    
    my $cachekey="COL-$datatype-$indextype-$schema-$table";
    
    if (not defined $self->[EXECUTOR_META_CACHE]->{$cachekey}) {
        my $colref;
        if ($meta->{$schema}->{table}->{$table}) {
            $colref = $meta->{$schema}->{table}->{$table};
        } elsif ($meta->{$schema}->{view}->{$table}) {
            $colref = $meta->{$schema}->{view}->{$table};
        } else {
            say "WARNING: Table/view '$table' in schema '$schema' has no columns";
            $colref = { 'non_existing_column1' => ['ordinary','int'], 'non_existing_column2' => ['indexed','int'] };
        }

        # If the table is a view, don't bother looking for indexed columns, fall back to ordinary
        $indextype = 'unknown'
            if ($meta->{$schema}->{view}->{$table} and $indextype eq 'ordinary');
        my $cols_by_datatype = [sort grep {$colref->{$_}->[1] eq $datatype} keys %$colref];
        if (not defined $cols_by_datatype or $#$cols_by_datatype < 0) {
            say "WARNING: Table/view '$table' in schema '$schema' has no '$datatype' columns";
            $cols_by_datatype = [ 'non_existing_column' ];
        }
        my $cols_by_indextype = [sort grep {$colref->{$_}->[0] ne $indextype} keys %$colref];
        if (not defined $cols_by_indextype or $#$cols_by_indextype < 0) {
            say "WARNING: Table '$table' in schema '$schema' has no columns which are not '$indextype'";
            $cols_by_indextype = [ 'non_existing_column' ];
        }
        my $cols = intersect_arrays($cols_by_datatype,$cols_by_indextype);
        $self->[EXECUTOR_META_CACHE]->{$cachekey} = $cols;
    }
    return $self->[EXECUTOR_META_CACHE]->{$cachekey};
    
}

sub metaColumnsIndexTypeNot {
    my ($self, $indextype, $table, $schema) = @_;
    my $meta = $self->[EXECUTOR_SCHEMA_METADATA];
    
    $schema = $self->defaultSchema if not defined $schema;
    $table = $self->metaTables($schema)->[0] if not defined $table;
    
    my $cachekey="COLNOT-$indextype-$schema-$table";

    if (not defined $self->[EXECUTOR_META_CACHE]->{$cachekey}) {
        my $colref;
        if ($meta->{$schema}->{table}->{$table}) {
            $colref = $meta->{$schema}->{table}->{$table};
        } elsif ($meta->{$schema}->{view}->{$table}) {
            $colref = $meta->{$schema}->{view}->{$table};
        } else {
            say "WARNING: Table/view '$table' in schema '$schema' has no columns";
            $colref = { 'non_existing_column1' => ['ordinary','int'], 'non_existing_column2' => ['indexed','int'] };
        }

        # If the table is a view, don't bother looking for indexed columns, fall back to ordinary
        $indextype = 'unknown'
            if ($meta->{$schema}->{view}->{$table} and $indextype eq 'ordinary');
        my $cols = [sort grep {$colref->{$_}->[0] ne $indextype} keys %$colref];
        if (not defined $cols or $#$cols < 0) {
            say "WARNING: Table '$table' in schema '$schema' has no columns which are not '$indextype'";
            $cols = [ 'non_existing_column' ];
        }
        $self->[EXECUTOR_META_CACHE]->{$cachekey} = $cols;
    }
    return $self->[EXECUTOR_META_CACHE]->{$cachekey};
}

sub metaCollations {
    my ($self) = @_;
    
    my $cachekey="COLLATIONS";

    if (not defined $self->[EXECUTOR_META_CACHE]->{$cachekey}) {
        my $coll = [sort keys %{$self->[EXECUTOR_COLLATION_METADATA]}];
        croak "FATAL ERROR: No Collations defined" if not defined $coll or $#$coll < 0;
        $self->[EXECUTOR_META_CACHE]->{$cachekey} = $coll;
    }
    return $self->[EXECUTOR_META_CACHE]->{$cachekey};
}

sub metaCharactersets {
    my ($self) = @_;
    
    my $cachekey="CHARSETS";
    
    if (not defined $self->[EXECUTOR_META_CACHE]->{$cachekey}) {
        my $charsets = [values %{$self->[EXECUTOR_COLLATION_METADATA]}];
        croak "FATAL ERROR: No character sets defined" if not defined $charsets or $#$charsets < 0;
        my %seen = ();
        $self->[EXECUTOR_META_CACHE]->{$cachekey} = [sort grep { ! $seen{$_} ++ } @$charsets];
    }
    return $self->[EXECUTOR_META_CACHE]->{$cachekey};
}

################### Public interface to be used from grammars
##

sub tables {
    my ($self, @args) = @_;
    return $self->metaTables(@args);
}

sub baseTables {
    my ($self, @args) = @_;
    return $self->metaBaseTables(@args);
}

sub tableColumns {
    my ($self, @args) = @_;
    return $self->metaColumns(@args);
}

1;
