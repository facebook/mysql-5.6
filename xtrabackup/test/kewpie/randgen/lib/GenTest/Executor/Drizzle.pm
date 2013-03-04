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

package GenTest::Executor::Drizzle;

require Exporter;

@ISA = qw(GenTest::Executor);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Executor;
use Time::HiRes;

use constant RARE_QUERY_THRESHOLD	=> 5;

my %reported_errors;

my @errors = (
	"Duplicate entry '.*?' for key '.*?'",
	"Can't DROP '.*?'",
	"Duplicate key name '.*?'",
	"Duplicate column name '.*?'",
	"Record has changed since last read in table '.*?'",
	"savepoint does not exist",
	"'.*?' doesn't exist",
	" .*? does not exist",
	"'.*?' already exists",
	"Unknown database '.*?'",
	"Unknown table '.*?'",
	"Unknown column '.*?'",
	"Column '.*?' specified twice",
	"Column '.*?' cannot be null",
	"Duplicate partition name .*?",
	"Tablespace '.*?' not empty",
	"Tablespace '.*?' already exists",
	"Tablespace data file '.*?' already exists",
	"Can't find file: '.*?'",
	"Table '.*?' already exists",
	"You can't specify target table '.*?' for update",
	"Illegal mix of collations .*?, .*?, .*? for operation '.*?'",
	"Illegal mix of collations .*? and .*? for operation '.*?'",
	"Invalid .*? character string: '.*?'",
	"This version of Drizzle doesn't yet support '.*?'",
	"PROCEDURE .*? already exists",
	"FUNCTION .*? already exists",
	"'.*?' isn't in GROUP BY",
	"non-grouping field '.*?' is used in HAVING clause",
	"Table has no partition for value .*?"
);

my @patterns = map { qr{$_}i } @errors;

use constant EXECUTOR_DRIZZLE_AUTOCOMMIT => 10;

#
# Column positions for SHOW SLAVES
# 

use constant SLAVE_INFO_HOST => 1;
use constant SLAVE_INFO_PORT => 2;

#
# Drizzle status codes
#
# Syntax error
use constant	ER_PARSE_ERROR	=> 1064;
use constant	ER_SYNTAX_ERROR	=> 1149;

# Semantic errors
use constant	ER_CANT_LOCK			=> 1015;
use constant	ER_NO_DB_ERROR			=> 1046;
use constant	ER_BAD_NULL_ERROR		=> 1048;
use constant	ER_BAD_DB_ERROR			=> 1049;
use constant	ER_TABLE_EXISTS_ERROR		=> 1050;
use constant	ER_BAD_TABLE_ERROR		=> 1051;
use constant	ER_BAD_FIELD_ERROR		=> 1054;
use constant	ER_WRONG_FIELD_WITH_GROUP	=> 1055;
use constant	ER_DUP_FIELDNAME		=> 1060;
use constant	ER_DUP_KEYNAME			=> 1061;
use constant	ER_MULTIPLE_PRI_KEY		=> 1068;
use constant 	ER_CANT_DROP_FIELD_OR_KEY	=> 1091;
use constant	ER_UPDATE_TABLE_USED		=> 1093;
use constant	ER_FIELD_SPECIFIED_TWICE	=> 1110;
use constant	ER_NO_SUCH_TABLE		=> 1146;
use constant	ER_CHECK_NOT_IMPLEMENTED	=> 1178;
use constant	ER_UNKNOWN_SYSTEM_VARIABLE	=> 1193;
use constant	ER_VAR_CANT_BE_READ		=> 1233;
use constant	ER_NOT_SUPPORTED_YET		=> 1235;
use constant	ER_CANT_AGGREGATE_2COLLATIONS	=> 1267;
use constant	ER_CANT_AGGREGATE_3COLLATIONS	=> 1270;
use constant	ER_CANT_AGGREGATE_NCOLLATIONS 	=> 1271;
use constant	ER_TRUNCATED_WRONG_VALUE 	=> 1292;
use constant	ER_INVALID_CHARACTER_STRING	=> 1300;
use constant	ER_SP_DOES_NOT_EXIST		=> 1305;
use constant	ER_STMT_NOT_ALLOWED_IN_SF_OR_TRG => 1336;
use constant	ER_TRG_ALREADY_EXISTS		=> 1359;
use constant	ER_TRG_DOES_NOT_EXIST		=> 1360;
use constant	ER_NO_DEFAULT_FOR_FIELD 	=> 1364;
use constant	ER_TRUNCATED_WRONG_VALUE_FOR_FIELD => 1366;
use constant	ER_COMMIT_NOT_ALLOWED_IN_SF_OR_TRG => 1422;
use constant	ER_NON_GROUPING_FIELD_USED	=> 1463;
use constant	ER_NO_PARTITION_FOR_GIVEN_VALUE	=> 1526;
use constant	ER_INVALID_DATETIME_VALUE	=> 1686;
use constant	ER_INVALID_ENUM_VALUE		=> 1691;

# Transaction errors
use constant	ER_CHECKREAD		=> 1020;
use constant	ER_DUP_KEY		=> 1022;
use constant	ER_DUP_ENTRY		=> 1062;
use constant	ER_LOCK_WAIT_TIMEOUT	=> 1205;
use constant	ER_LOCK_DEADLOCK	=> 1213;

# Storage engine failures
use constant	ER_GET_ERRNO		=> 1030;

# Database corruption
use constant	ER_NOT_KEYFILE		=> 1034;
use constant	ER_UNEXPECTED_EOF	=> 1039;
use constant	ER_CRASHED_ON_USAGE	=> 1194;

# Backup

# Out of disk space, quotas, etc.
use constant	ER_RECORD_FILE_FULL     => 1114;
use constant	ER_DISK_FULL            => 1021;
use constant	ER_OUTOFMEMORY		=> 1037;
use constant	ER_CON_COUNT_ERROR	=> 1040;
use constant	ER_OUT_OF_RESOURCES	=> 1041;
use constant	ER_SERVER_SHUTDOWN     	=> 1053;
use constant	ER_CANT_CREATE_THREAD	=> 1135;

my %err2type = (
	ER_BAD_DB_ERROR()	=> STATUS_SEMANTIC_ERROR,
	ER_BAD_FIELD_ERROR()	=> STATUS_SEMANTIC_ERROR,
	ER_BAD_NULL_ERROR()	=> STATUS_SEMANTIC_ERROR,
	ER_BAD_TABLE_ERROR()	=> STATUS_SEMANTIC_ERROR,
	ER_CANT_AGGREGATE_2COLLATIONS() => STATUS_SEMANTIC_ERROR,
	ER_CANT_AGGREGATE_3COLLATIONS() => STATUS_SEMANTIC_ERROR,
	ER_CANT_AGGREGATE_NCOLLATIONS() => STATUS_SEMANTIC_ERROR,
	ER_CANT_CREATE_THREAD()	=> STATUS_ENVIRONMENT_FAILURE,
	ER_CANT_DROP_FIELD_OR_KEY()	=> STATUS_SEMANTIC_ERROR,
	ER_CANT_LOCK()		=> STATUS_SEMANTIC_ERROR,
	ER_CHECK_NOT_IMPLEMENTED() => STATUS_SEMANTIC_ERROR,
	ER_CHECKREAD()		=> STATUS_TRANSACTION_ERROR,
	ER_COMMIT_NOT_ALLOWED_IN_SF_OR_TRG() => STATUS_SEMANTIC_ERROR,
	ER_CON_COUNT_ERROR()	=> STATUS_ENVIRONMENT_FAILURE,
	ER_CRASHED_ON_USAGE()	=> STATUS_DATABASE_CORRUPTION,
	ER_DISK_FULL()          => STATUS_ENVIRONMENT_FAILURE,
	ER_DUP_ENTRY()		=> STATUS_TRANSACTION_ERROR,
	ER_DUP_FIELDNAME()	=> STATUS_SEMANTIC_ERROR,
	ER_DUP_KEYNAME()	=> STATUS_SEMANTIC_ERROR,
	ER_DUP_KEY()		=> STATUS_TRANSACTION_ERROR,
	ER_FIELD_SPECIFIED_TWICE()	=> STATUS_SEMANTIC_ERROR,
	ER_GET_ERRNO()		=> STATUS_SEMANTIC_ERROR,
	ER_INVALID_CHARACTER_STRING()	=> STATUS_SEMANTIC_ERROR,
	ER_INVALID_DATETIME_VALUE()	=> STATUS_SEMANTIC_ERROR,
	ER_INVALID_ENUM_VALUE()		=> STATUS_SEMANTIC_ERROR,
	ER_LOCK_DEADLOCK()	=> STATUS_TRANSACTION_ERROR,
	ER_LOCK_WAIT_TIMEOUT()	=> STATUS_TRANSACTION_ERROR,
	ER_MULTIPLE_PRI_KEY()	=> STATUS_SEMANTIC_ERROR,
	ER_NO_DB_ERROR()	=> STATUS_SEMANTIC_ERROR,
	ER_NO_DEFAULT_FOR_FIELD()	=> STATUS_SEMANTIC_ERROR,
	ER_NON_GROUPING_FIELD_USED()	=> STATUS_SEMANTIC_ERROR,
	ER_NO_PARTITION_FOR_GIVEN_VALUE()	=> STATUS_SEMANTIC_ERROR,
	ER_NO_SUCH_TABLE()	=> STATUS_SEMANTIC_ERROR,
	ER_NOT_KEYFILE()	=> STATUS_DATABASE_CORRUPTION,
	ER_NOT_SUPPORTED_YET()	=> STATUS_SEMANTIC_ERROR,
	ER_OUTOFMEMORY()	=> STATUS_ENVIRONMENT_FAILURE,
	ER_OUT_OF_RESOURCES()	=> STATUS_ENVIRONMENT_FAILURE,
	ER_PARSE_ERROR()	=> STATUS_SYNTAX_ERROR,
	ER_RECORD_FILE_FULL()   => STATUS_ENVIRONMENT_FAILURE,
	ER_SERVER_SHUTDOWN()    => STATUS_SERVER_KILLED,
	ER_SP_DOES_NOT_EXIST()	=> STATUS_SEMANTIC_ERROR,
	ER_STMT_NOT_ALLOWED_IN_SF_OR_TRG() => STATUS_SEMANTIC_ERROR,
	ER_SYNTAX_ERROR()	=> STATUS_SYNTAX_ERROR,
	ER_TABLE_EXISTS_ERROR() => STATUS_SEMANTIC_ERROR,
	ER_TRG_ALREADY_EXISTS()		=> STATUS_SEMANTIC_ERROR,
	ER_TRG_DOES_NOT_EXIST() => STATUS_SEMANTIC_ERROR,
	ER_TRUNCATED_WRONG_VALUE_FOR_FIELD()	=> STATUS_SEMANTIC_ERROR,
	ER_TRUNCATED_WRONG_VALUE()	=> STATUS_SEMANTIC_ERROR,
	ER_UNEXPECTED_EOF()	=> STATUS_DATABASE_CORRUPTION,
	ER_UNKNOWN_SYSTEM_VARIABLE() => STATUS_SEMANTIC_ERROR,
	ER_UPDATE_TABLE_USED()	=> STATUS_SEMANTIC_ERROR,
	ER_VAR_CANT_BE_READ()	=> STATUS_SEMANTIC_ERROR,
	ER_WRONG_FIELD_WITH_GROUP()	=> STATUS_SEMANTIC_ERROR,
);

my %caches;
	
sub init {
	my $executor = shift;
	my $dbh = DBI->connect($executor->dsn(), undef, undef, {
		PrintError => 0,
		RaiseError => 0,
		AutoCommit => 1
	} );

	if (not defined $dbh) {
		say("connect() to dsn ".$executor->dsn()." failed: ".$DBI::errstr);
		return STATUS_ENVIRONMENT_FAILURE;
	}

	$executor->setDbh($dbh);

    $executor->defaultSchema($executor->currentSchema());
    say "Default schema: ".$executor->defaultSchema();

	say("Executor initialized, id ".$executor->id());

	return STATUS_OK;
}

sub execute {
	my ($executor, $query, $silent) = @_;

	my $dbh = $executor->dbh();
       
        if ($executor->sqltrace) 
        {
            print "$query;\n\n";
        }

	return GenTest::Result->new( query => $query, status => STATUS_UNKNOWN_ERROR ) if not defined $dbh;

	$query = $executor->preprocess($query);

	if (
		(not defined $executor->[EXECUTOR_DRIZZLE_AUTOCOMMIT]) &&
		(
			($query =~ m{^\s*start transaction}io) ||
			($query =~ m{^\s*begin}io) 
		)
	) {	
		$dbh->do("SET AUTOCOMMIT=OFF");
		$executor->[EXECUTOR_DRIZZLE_AUTOCOMMIT] = 0;
	}

	my $start_time = Time::HiRes::time();
	my $sth = $dbh->prepare($query);

	if (not defined $sth) {			# Error on PREPARE
		my $errstr = $executor->normalizeError($sth->errstr());
		$executor->[EXECUTOR_ERROR_COUNTS]->{$errstr}++ if rqg_debug() && !$silent;
		return GenTest::Result->new(
			query		=> $query,
			status		=> $executor->getStatusFromErr($dbh->err()) || STATUS_UNKNOWN_ERROR,
			err		=> $dbh->err(),
			errstr	 	=> $dbh->errstr(),
			sqlstate	=> $dbh->state(),
			start_time	=> $start_time,
			end_time	=> Time::HiRes::time()
		);
	}
       

	my $affected_rows = $sth->execute();
	my $end_time = Time::HiRes::time();

	my $err = $sth->err();
	my $result;

	if (defined $err) {			# Error on EXECUTE
		my $err_type = $err2type{$err};

		if (
			($err_type == STATUS_SYNTAX_ERROR) ||
			($err_type == STATUS_SEMANTIC_ERROR) ||
			($err_type == STATUS_TRANSACTION_ERROR)
		) {
			my $errstr = $executor->normalizeError($sth->errstr());
			$executor->[EXECUTOR_ERROR_COUNTS]->{$errstr}++ if rqg_debug() && !$silent;
			if (not defined $reported_errors{$errstr} and not $executor->noErrFilter()) 
                        {
			    say("Query: $query failed: $err $errstr. Further errors of this kind will be suppressed.") if !$silent;
			    $reported_errors{$errstr}++;
			}
                        else
                        {
                            say("Query: $query failed: $err $errstr \n\n");
                        }
                           
		} elsif (
			($err_type == STATUS_SERVER_CRASHED) ||
			($err_type == STATUS_SERVER_KILLED)
		) {
			$dbh = DBI->connect($executor->dsn(), undef, undef, {
				PrintError => 0,
				RaiseError => 0,
				AutoCommit => 1
			} );

			# If server is still connectable, it is not a real crash, but most likely a KILL query

			if (defined $dbh) {
				$err_type = STATUS_SEMANTIC_ERROR;
				$executor->setDbh($dbh);
			}

			say("Query: $query failed: $err ".$sth->errstr()) if !$silent;
		} else {
			$executor->[EXECUTOR_ERROR_COUNTS]->{$sth->errstr()}++ if rqg_debug() && !$silent;
			say("Query: $query failed: $err ".$sth->errstr()) if !$silent;
		}

		$result = GenTest::Result->new(
			query		=> $query,
			status		=> $err_type || STATUS_UNKNOWN_ERROR,
			err		=> $err,
			errstr		=> $sth->errstr(),
			sqlstate	=> $sth->state(),
			start_time	=> $start_time,
			end_time	=> $end_time
		);
	} elsif ((not defined $sth->{NUM_OF_FIELDS}) || ($sth->{NUM_OF_FIELDS} == 0)) {
		$result = GenTest::Result->new(
			query		=> $query,
			status		=> STATUS_OK,
			affected_rows	=> $affected_rows,
			start_time	=> $start_time,
			end_time	=> $end_time
		);
		$executor->[EXECUTOR_ERROR_COUNTS]->{'(no error)'}++ if rqg_debug() && !$silent;
	} else {
		#
		# We do not use fetchall_arrayref() due to a memory leak
		# We also copy the row explicitly into a fresh array
		# otherwise the entire @data array ends up referencing row #1 only
		#
		my @data;
		while (my $row = $sth->fetchrow_arrayref()) {
			my @row = @$row;
			push @data, \@row;
		}	

		$result = GenTest::Result->new(
			query		=> $query,
			status		=> STATUS_OK,
			affected_rows 	=> $affected_rows,
			data		=> \@data,
			start_time	=> $start_time,
			end_time	=> $end_time
		);

		$executor->[EXECUTOR_ERROR_COUNTS]->{'(no error)'}++ if rqg_debug() && !$silent;
	}

	$sth->finish();

	if ($sth->{drizzle_warning_count} > 0) {
		my $warnings = $dbh->selectcol_arrayref("SHOW WARNINGS");
		$result->setWarnings($warnings);
	}

        if ( (rqg_debug()) && (!$silent) ) {
                if ($query =~ m{^\s*select}sio) {
                        $executor->explain($query);
                        my $row_group = $sth->rows() > 100 ? '>100' : ($sth->rows() > 10 ? ">10" : sprintf("%5d",$sth->rows()) );
                        $executor->[EXECUTOR_RETURNED_ROW_COUNTS]->{$row_group}++;
                } elsif ($query =~ m{^\s*(update|delete|insert|replace)}sio) {
                        my $row_group = $affected_rows > 100 ? '>100' : ($affected_rows > 10 ? ">10" : sprintf("%5d",$affected_rows) );
                        $executor->[EXECUTOR_AFFECTED_ROW_COUNTS]->{$row_group}++;
                }
        }

	return $result;
}

#
# Run EXPLAIN on the query in question, recording all notes in the EXPLAIN's Extra field into the statistics
#

sub id {
	my $executor = shift;

	# if no ID string has been defined yet, define one.

	if ($executor->SUPER::id() eq '') {
		my $dbh = $executor->dbh();
		my $version = $dbh->selectrow_array("SELECT VERSION()");

		my @capabilities;

		push @capabilities, "master" if $dbh->selectrow_array("SHOW SLAVE HOSTS");
		push @capabilities, "slave" if $dbh->selectrow_array("SHOW SLAVE STATUS");
		push @capabilities, "no_semijoin" if $dbh->selectrow_array('SELECT @@optimizer_switch') =~ m{no_semijoin}sio;
		push @capabilities, "no_materialization" if $dbh->selectrow_array('SELECT @@optimizer_switch') =~ m{no_materialization}sio;
		push @capabilities, "mo_mrr" if $dbh->selectrow_array('SELECT @@optimizer_use_mrr') eq '0';
		push @capabilities, "no_condition_pushdown" if $dbh->selectrow_array('SELECT @@engine_condition_pushdown') eq '0';
		$executor->setId(ref($executor)." ".$version." (".join('; ', @capabilities).")");
	}
	
	# Pass the call back to the parent class. It will respond with the id that was (just) defined.

	return $executor->SUPER::id();
}

sub version {
	my $executor = shift;
	my $dbh = $executor->dbh();
	return $dbh->selectrow_array("SELECT VERSION()");
}

sub slaveInfo {
	my $executor = shift;
	my $slave_info = $executor->dbh()->selectrow_arrayref("SHOW SLAVE HOSTS");
	return ($slave_info->[SLAVE_INFO_HOST], $slave_info->[SLAVE_INFO_PORT]);
}

sub masterStatus {
	my $executor = shift;
	return $executor->dbh()->selectrow_array("SHOW MASTER STATUS");
}

sub explain {
	my ($executor, $query) = @_;

	my $sth_output = $executor->dbh()->prepare("EXPLAIN /*!50100 PARTITIONS */ $query");

	$sth_output->execute();

	my @explain_fragments;

	while (my $explain_row = $sth_output->fetchrow_hashref()) {
		push @explain_fragments, "select_type: ".($explain_row->{select_type} || '(empty)');

		push @explain_fragments, "type: ".($explain_row->{type} || '(empty)');

		push @explain_fragments, "partitions: ".$explain_row->{table}.":".$explain_row->{partitions} if defined $explain_row->{partitions};

		foreach my $extra_item (split('; ', ($explain_row->{Extra} || '(empty)')) ) {
			$extra_item =~ s{0x.*?\)}{%d\)}sgio;
			$extra_item =~ s{PRIMARY|[a-z_]+_key}{%s}sgio;
			push @explain_fragments, "extra: ".$extra_item;
		}
	}
	
	foreach my $explain_fragment (@explain_fragments) {
		$executor->[EXECUTOR_EXPLAIN_COUNTS]->{$explain_fragment}++;
		if ($executor->[EXECUTOR_EXPLAIN_COUNTS]->{$explain_fragment} > RARE_QUERY_THRESHOLD) {
			delete $executor->[EXECUTOR_EXPLAIN_QUERIES]->{$explain_fragment};
		} else {
			push @{$executor->[EXECUTOR_EXPLAIN_QUERIES]->{$explain_fragment}}, $query;
		}
	}

}
sub disconnect {
	my $executor = shift;
	if (rqg_debug()) {
		say("Statistics for Executor ".$executor->dsn());
		use Data::Dumper;
		$Data::Dumper::Sortkeys = 1;
		say("Rows returned:");
                print Dumper $executor->[EXECUTOR_RETURNED_ROW_COUNTS];
                say("Rows affected:");
                print Dumper $executor->[EXECUTOR_AFFECTED_ROW_COUNTS];
                say("Explain items:");
		print Dumper $executor->[EXECUTOR_EXPLAIN_COUNTS];
		say("Errors:");
		print Dumper $executor->[EXECUTOR_ERROR_COUNTS];
		say("Rare EXPLAIN items:");
		print Dumper $executor->[EXECUTOR_EXPLAIN_QUERIES];
	}
	$executor->dbh()->disconnect();
}

sub DESTROY {
    my ($self) = @_;
    if (defined $self->dbh) {
        $self->disconnect;
    }
}


sub currentSchema {
	my ($executor,$schema) = @_;

	return undef if not defined $executor->dbh();

    if (defined $schema) {
        $executor->execute("USE $schema");
    }
    
	return $executor->dbh()->selectrow_array("SELECT DATABASE()");
}


sub errorType {
	return undef if not defined $_[0];
	return $err2type{$_[0]} || STATUS_UNKNOWN_ERROR ;
}

sub normalizeError {
	my ($executor, $errstr) = @_;

	foreach my $i (0..$#errors) {
		last if $errstr =~ s{$patterns[$i]}{$errors[$i]}si;
	}

	$errstr =~ s{\d+}{%d}sgio if $errstr !~ m{from storage engine}sio; # Make all errors involving numbers the same, e.g. duplicate key errors

	$errstr =~ s{\.\*\?}{%s}sgio;

	return $errstr;
}

sub getSchemaMetaData {
    ## Return the result from a query with the following columns:
    ## 1. Schema (aka database) name
    ## 2. Table name
    ## 3. TABLE for tables VIEW for views and MISC for other stuff
    ## 4. Column name
    ## 5. PRIMARY for primary key, INDEXED for indexed column and "ORDINARY" for all other columns
    my ($self) = @_;
    my $query;
    my $version = $self->dbh()->selectrow_array("SELECT VERSION()");
    # as of build 1320 we start using data_dictionary instead of information_schema
    if ($version lt "2010.03.1320") {
        $query =         "SELECT SQL_BIG_RESULT table_schema, ".
               "table_name, ".
               "CASE WHEN table_type = 'BASE TABLE' THEN 'table' ".
                    "WHEN table_type = 'VIEW' THEN 'view' ".
                    "WHEN table_type = 'SYSTEM VIEW' then 'view' ".
                    "ELSE 'misc' END, ".
               "column_name, ".
               "CASE WHEN column_key = 'PRI' THEN 'primary' ".
                    "WHEN column_key = 'MUL' THEN 'indexed' ".
                    "WHEN column_key = 'UNI' THEN 'indexed' ".
                    "ELSE 'ordinary' END ".
         "FROM information_schema.tables INNER JOIN ".
              "information_schema.columns USING(table_schema, table_name) ORDER BY table_schema, table_name";
     }
     else {
        $query = "SELECT SQL_BIG_RESULT table_schema, ".
               "table_name, ".
               "CASE WHEN table_type = 'STANDARD' THEN 'table' ".
                    "WHEN table_type = 'FUNCTION' THEN 'function' ".
                    "ELSE 'misc' END, ".
    	       "column_name, ".
               "CASE WHEN IS_USED_IN_PRIMARY = 'YES' THEN 'primary' ".
                    "WHEN IS_INDEXED = 'YES' THEN 'indexed' ".
                    "ELSE 'ordinary' END ".
         "FROM data_dictionary.tables INNER JOIN ".
              "data_dictionary.columns USING(table_schema, table_name) ORDER BY table_schema, table_name"; 
    }

    return $self->dbh()->selectall_arrayref($query);
}

sub getCollationMetaData {
    ## Return the result from a query with the following columns:
    ## 1. Collation name
    ## 2. Character set
    my ($self) = @_;
    my $query;
    my $version = $self->dbh()->selectrow_array("SELECT VERSION()");
    # as of build 1320 we start using data_dictionary instead of information_schema
    if ($version lt "2010.03.1320") {
    	$query = 
       		"SELECT collation_name,character_set_name FROM information_schema.collations";
    }
    else {
    	$query = 
         	"SELECT collation_name,character_set_name FROM data_dictionary.collations";
    }

    return $self->dbh()->selectall_arrayref($query);
}

1;
