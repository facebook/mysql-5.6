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

package GenTest::App::GendataSimple;

@ISA = qw(GenTest);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Random;
use GenTest::Executor;

use Data::Dumper;

use constant GDS_DEFAULT_DSN => 'dbi:mysql:host=127.0.0.1:port=9306:user=root:database=test';

use constant GDS_DSN => 0;
use constant GDS_ENGINE => 1;
use constant GDS_VIEWS => 2;
use constant GDS_SQLTRACE => 3;
use constant GDS_NOTNULL => 4;
use constant GDS_ROWS => 5;
use constant GDS_VARCHAR_LENGTH => 6;

use constant GDS_DEFAULT_ROWS => [0, 1, 20, 100, 1000, 0, 1, 20, 100];
use constant GDS_DEFAULT_NAMES => ['A', 'B', 'C', 'D', 'E', 'AA', 'BB', 'CC', 'DD'];

sub new {
    my $class = shift;

    my $self = $class->SUPER::new({
        'dsn' => GDS_DSN,
        'engine' => GDS_ENGINE,
        'views' => GDS_VIEWS,
        'sqltrace' => GDS_SQLTRACE,
	'notnull' => GDS_NOTNULL,
	'rows' => GDS_ROWS
    },@_);

    if (not defined $self->[GDS_DSN]) {
        $self->[GDS_DSN] = GDS_DEFAULT_DSN;
    }
        
    return $self;
}

sub defaultDsn {
    return GDS_DEFAULT_DSN;
}

sub dsn {
    return $_[0]->[GDS_DSN];
}

sub engine {
    return $_[0]->[GDS_ENGINE];
}

sub views {
    return $_[0]->[GDS_VIEWS];
}

sub sqltrace {
    return $_[0]->[GDS_SQLTRACE];
}

sub rows {
    return $_[0]->[GDS_ROWS];
}

sub varcharLength {
    return $_[0]->[GDS_VARCHAR_LENGTH] || 1;
}

sub run {
    my ($self) = @_;

    my $prng = GenTest::Random->new( seed => 0 );

    my $executor = GenTest::Executor->newFromDSN($self->dsn());
    $executor->sqltrace($self->sqltrace);
    $executor->init();
    
    my $names = GDS_DEFAULT_NAMES;
    my $rows;

    if (defined $self->rows()) {
        $rows = [split(',', $self->rows())];
    } else {
        $rows = GDS_DEFAULT_ROWS;
    }

    foreach my $i (0..$#$names) {
        my $gen_table_result = $self->gen_table($executor, $names->[$i], $rows->[$i], $prng);
        return $gen_table_result if $gen_table_result != STATUS_OK;
    }
    
    # Need to create a dummy supdstituion for non-protable DUAL
    
    $executor->execute("DROP TABLE /*! IF EXISTS */ DUMMY");
    $executor->execute("CREATE TABLE DUMMY (I INTEGER)");
    $executor->execute("INSERT INTO DUMMY VALUES(0)");
    
    $executor->execute("SET SQL_MODE= 'NO_ENGINE_SUBSTITUTION'") if $executor->type == DB_MYSQL;
    return STATUS_OK;
}

sub gen_table {
	my ($self, $executor, $name, $size, $prng) = @_;

    my $nullability = defined $self->[GDS_NOTNULL] ? 'NOT NULL' : '/*! NULL */';  
    ### NULL is not a valid ANSI constraint, (but NOT NULL of course,
    ### is)

    my $varchar_length = $self->varcharLength();

    my $engine = $self->engine();
    my $views = $self->views();

	if (
		($executor->type == DB_MYSQL) ||
		($executor->type == DB_DRIZZLE)
	) {

        say("Creating ".$executor->getName()." table $name, size $size rows, engine $engine .");
    
		### This variant is needed due to
		### http://bugs.mysql.com/bug.php?id=47125

		$executor->execute("DROP TABLE /*! IF EXISTS */ $name");
		$executor->execute("
		CREATE TABLE $name (
			pk INTEGER AUTO_INCREMENT,
			col_int_nokey INTEGER $nullability,
			col_int_key INTEGER $nullability,

			col_date_key DATE $nullability,
			col_date_nokey DATE $nullability,

			col_time_key TIME $nullability,
			col_time_nokey TIME $nullability,

			col_datetime_key DATETIME $nullability,
			col_datetime_nokey DATETIME $nullability,

			col_varchar_key VARCHAR($varchar_length) $nullability,
			col_varchar_nokey VARCHAR($varchar_length) $nullability,

			PRIMARY KEY (pk),
			KEY (col_int_key),
			KEY (col_date_key),
			KEY (col_time_key),
			KEY (col_datetime_key),
			KEY (col_varchar_key, col_int_key)
		) ".(length($name) > 1 ? " AUTO_INCREMENT=".(length($name) * 5) : "").($engine ne '' ? " ENGINE=$engine" : "")
						   # For tables named like CC and CCC, start auto_increment with some offset. This provides better test coverage since
						   # joining such tables on PK does not produce only 1-to-1 matches.
			);
		
    } elsif ($executor->type == DB_POSTGRES) {
        say("Creating ".$executor->getName()." table $name, size $size rows");
    
        my $increment_size = (length($name) > 1 ? (length($name) * 5) : 1);
		$executor->execute("DROP TABLE /*! IF EXISTS */ $name");
        $executor->execute("DROP SEQUENCE ".$name."_seq");
        $executor->execute("CREATE SEQUENCE ".$name."_seq INCREMENT 1 START $increment_size");
		$executor->execute("
		CREATE TABLE $name (
			pk INTEGER DEFAULT nextval('".$name."_seq') NOT NULL,
			col_int_nokey INTEGER $nullability,
			col_int_key INTEGER $nullability,

			col_date_key DATE $nullability,
			col_date_nokey DATE $nullability,

			col_time_key TIME $nullability,
			col_time_nokey TIME $nullability,

			col_datetime_key DATETIME $nullability,
			col_datetime_nokey DATETIME $nullability,

			col_varchar_key VARCHAR($varchar_length) $nullability,
			col_varchar_nokey VARCHAR($varchar_length) $nullability,

			PRIMARY KEY (pk))");

		$executor->execute("CREATE INDEX ".$name."_int_key ON $name(col_int_key)");
		$executor->execute("CREATE INDEX ".$name."_date_key ON $name(col_date_key)");
		$executor->execute("CREATE INDEX ".$name."_time_key ON $name(col_time_key)");
		$executor->execute("CREATE INDEX ".$name."_datetime_key ON $name(col_datetime_key)");
		$executor->execute("CREATE INDEX ".$name."_varchar_key ON $name(col_varchar_key, col_int_key)");

	} else {
        say("Creating ".$executor->getName()." table $name, size $size rows");

		$executor->execute("DROP TABLE /*! IF EXISTS */ $name");
		$executor->execute("
		CREATE TABLE $name (
			pk INTEGER AUTO_INCREMENT,
			col_int_nokey INTEGER $nullability,
			col_int_key INTEGER $nullability,

			col_date_key DATE $nullability,
			col_date_nokey DATE $nullability,

			col_time_key TIME $nullability,
			col_time_nokey TIME $nullability,

			col_datetime_key DATETIME $nullability,
			col_datetime_nokey DATETIME $nullability,

			col_varchar_key VARCHAR($varchar_length) $nullability,
			col_varchar_nokey VARCHAR($varchar_length) $nullability,

			PRIMARY KEY (pk)
		) ".(length($name) > 1 ? " AUTO_INCREMENT=".(length($name) * 5) : "").($engine ne '' ? " ENGINE=$engine" : "")
						   # For tables named like CC and CCC, start auto_increment with some offset. This provides better test coverage since
						   # joining such tables on PK does not produce only 1-to-1 matches.
			);
		
		$executor->execute("CREATE INDEX ".$name."_int_key ON $name(col_int_key)");
		$executor->execute("CREATE INDEX ".$name."_date_key ON $name(col_date_key)");
		$executor->execute("CREATE INDEX ".$name."_time_key ON $name(col_time_key)");
		$executor->execute("CREATE INDEX ".$name."_datetime_key ON $name(col_datetime_key)");
		$executor->execute("CREATE INDEX ".$name."_varchar_key ON $name(col_varchar_key, col_int_key)");
	};

	if (defined $views) {
		if ($views ne '') {
			$executor->execute("CREATE ALGORITHM=$views VIEW view_".$name.' AS SELECT * FROM '.$name);
		} else {
			$executor->execute('CREATE VIEW view_'.$name.' AS SELECT * FROM '.$name);
		}
	}

	my @values;

	foreach my $row (1..$size) {
	
		# 10% NULLs, 10% tinyint_unsigned, 80% digits

		my $pick1 = $prng->uint16(0,9);
		my $pick2 = $prng->uint16(0,9);

		my ($rnd_int1, $rnd_int2);
		if (defined $self->[GDS_NOTNULL]) {
			$rnd_int1 = ($pick1 == 8 ? $prng->int(0,255) : $prng->digit() );
			$rnd_int2 = ($pick1 == 8 ? $prng->int(0,255) : $prng->digit() );
		} else {
			$rnd_int1 = $pick1 == 9 ? "NULL" : ($pick1 == 8 ? $prng->int(0,255) : $prng->digit() );
			$rnd_int2 = $pick2 == 9 ? "NULL" : ($pick1 == 8 ? $prng->int(0,255) : $prng->digit() );
		}

		# 10% NULLS, 10% '1900-01-01', pick real date/time/datetime for the rest

		my $rnd_date = "'".$prng->date()."'";

		$rnd_date = ($rnd_date, $rnd_date, $rnd_date, $rnd_date, $rnd_date, $rnd_date, $rnd_date, $rnd_date, "NULL", "'1900-01-01'")[$prng->uint16(0,9)];
		my $rnd_time = "'".$prng->time()."'";
		$rnd_time = ($rnd_time, $rnd_time, $rnd_time, $rnd_time, $rnd_time, $rnd_time, $rnd_time, $rnd_time, "NULL", "'00:00:00'")[$prng->uint16(0,9)];

		# 10% NULLS, 10% "1900-01-01 00:00:00', 20% date + " 00:00:00"

		my $rnd_datetime = $prng->datetime();
		my $rnd_datetime_date_only = $prng->date();

		if (defined $self->[GDS_NOTNULL]) {
			$rnd_datetime = ($rnd_datetime, $rnd_datetime, $rnd_datetime, $rnd_datetime, $rnd_datetime, $rnd_datetime, $rnd_datetime, $rnd_datetime_date_only." 00:00:00", $rnd_datetime_date_only." 00:00:00", '1900-01-01 00:00:00')[$prng->uint16(0,9)];
		} else {
			$rnd_datetime = ($rnd_datetime, $rnd_datetime, $rnd_datetime, $rnd_datetime, $rnd_datetime, $rnd_datetime, $rnd_datetime_date_only." 00:00:00", $rnd_datetime_date_only." 00:00:00", "NULL", '1900-01-01 00:00:00')[$prng->uint16(0,9)];
		}
		$rnd_datetime = "'".$rnd_datetime."'" if not $rnd_datetime eq "NULL";

		my $rnd_varchar;

		if (defined $self->[GDS_NOTNULL]) {
			$rnd_varchar = "'".$prng->string($varchar_length)."'";
		} else {
			$rnd_varchar = $prng->uint16(0,9) == 9 ? "NULL" : "'".$prng->string($varchar_length)."'";
		}

		push(@values, "($rnd_int1, $rnd_int2, $rnd_date, $rnd_date, $rnd_time, $rnd_time, $rnd_datetime, $rnd_datetime, $rnd_varchar, $rnd_varchar)");

		## We do one insert per 500 rows for speed
		if ($row % 500 == 0 || $row == $size) {
			my $insert_result = $executor->execute("
			INSERT /*! IGNORE */ INTO $name (
				col_int_key, col_int_nokey,
				col_date_key, col_date_nokey,
				col_time_key, col_time_nokey,
				col_datetime_key, col_datetime_nokey,
				col_varchar_key, col_varchar_nokey
			) VALUES " . join(",",@values));
			return $insert_result->status() if $insert_result->status() != STATUS_OK;
			@values = ();
		}
	}
	return STATUS_OK;
}

1;
