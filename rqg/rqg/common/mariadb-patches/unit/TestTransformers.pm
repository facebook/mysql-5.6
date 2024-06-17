# Copyright (c) 2012 Oracle and/or its affiliates. All rights reserved.
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

################################################################################
# Test various transformers.
# If we end up with too many test cases, break up into individual files.

package TestTransformers;
use base qw(Test::Unit::TestCase);
use lib 'lib';
use GenTest;
use Cwd;
use GenTest::Constants;
use File::Path qw(rmtree);

my $counter;    # something to distinguish test cases to avoid port conflicts etc.

sub new {
    my $self = shift()->SUPER::new(@_);
    $ENV{LD_LIBRARY_PATH}=join(":",map{"$ENV{RQG_MYSQL_BASE}".$_}("/libmysql/.libs","/libmysql","/lib/mysql"));
    return $self;
}

sub set_up {
    my $self = shift;
    $counter++;
    $self->{logfile} = 'unit/tmp/transformer'.$counter.'.log';
    # --mtr-build-thread : Should differ between testcases due to possible
    # parallelism. We use a "unique" portbase for this.
    my $portbase = ($counter*20) + ($ENV{TEST_PORTBASE}>0 ? int($ENV{TEST_PORTBASE}) : 22120);
    $self->{portbase} = int(($portbase - 10000) / 10);
    $self->{workdir} = cwd()."/unit/tmpwd4"; 
}

sub tear_down {
    my $self = shift;
    # Not all tests use the workdir, so we need to check if it exists.
    if (-e $self->{workdir}) {
        rmtree($self->{workdir}) or print("UNABLE TO REMOVE DIR ".$self->{workdir}.": $!\n");
    }
    # clean up after test
    #unlink $self->{logfile};    # comment this if you need the log to debug after the fact
}

# Test that ExecuteAsFunctionTwice works with BIT_AND queries.
# BIT_AND is special because it returns the max value of unsigned bigint if
# it matches no rows. This means that the standard BIT_AND return type bigint
# will not be able to store all of this value, and you will get a diff unless
# bigint unsigned is used as return type.
sub test_transformer_ExecuteAsFunctionTwice_BIT_AND {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a MySQL installation (or in-source build)
    if ($ENV{RQG_MYSQL_BASE}) {
        # Use a grammar that produced a BIT_AND query which matches no rows.
        my $grammar = 'unit/tmp/bit_and.yy';
        open(FILE, "> $grammar") or assert("Unable to create grammar file");
        print FILE "query:\n";
        print FILE "    SELECT BIT_AND(col_int_key) FROM BB WHERE pk < 0 ;\n";
        close FILE;
        
        my $rqg_opts = 
             "--grammar=$grammar " 
            .'--queries=1 --sqltrace '
            .'--transformer=ExecuteAsFunctionTwice '
            .'--threads=1 ' 
            .'--basedir='.$ENV{RQG_MYSQL_BASE}.' '
            .'--vardir='.$self->{workdir};
            
        my $cmd = 'perl -MCarp=verbose ./runall-new.pl '.$rqg_opts
            .' --mtr-build-thread='.$self->{portbase}
            .' > '.$self->{logfile}.' 2>&1';
        $self->annotate("RQG command line: $cmd");
        my $status = system($cmd);
        my $expected = STATUS_OK;
        my $actual = $status >> 8;
        $self->assert_num_equals($expected, $actual, 
            "Wrong exit status from runall-new.pl, expected $expected and got $actual");
        unlink $grammar;
    }
}

# Test that ExecuteAsUnion works with SELECT ... LIMIT queries that contain
# comments after the LIMIT clause. Prior to a bugfix these were not
# recognized as LIMIT queries and the transformed query would end up with
# two LIMIT clauses, resulting in STATUS_ENVIRONMENT_FAILURE due to syntax
# error.
sub test_transformer_ExecuteAsUnion_LIMIT {
    my $self = shift;
    ## This test requires RQG_MYSQL_BASE to point to a MySQL installation (or in-source build)
    if ($ENV{RQG_MYSQL_BASE}) {
        # Use a grammar that produced a BIT_AND query which matches no rows.
        my $grammar = 'unit/tmp/union_limit.yy';
        open(FILE, "> $grammar") or assert("Unable to create grammar file");
        print FILE "query:\n";
        print FILE "    SELECT C.col_int_key AS field1 FROM C ORDER BY field1 LIMIT 10 /* 1 */;\n";
        close FILE;
        
        my $rqg_opts = 
             "--grammar=$grammar " 
            .'--queries=1 '
            .'--transformer=ExecuteAsUnion '
            .'--threads=1 ' 
            .'--basedir='.$ENV{RQG_MYSQL_BASE}.' '
            .'--vardir='.$self->{workdir};
            
        my $cmd = 'perl -MCarp=verbose ./runall-new.pl '.$rqg_opts
            .' --mtr-build-thread='.$self->{portbase}
            .' > '.$self->{logfile}.' 2>&1';
        $self->annotate("RQG command line: $cmd");
        my $status = system($cmd);
        my $expected = STATUS_OK;
        my $actual = $status >> 8;
        $self->assert_num_equals($expected, $actual, 
            "Wrong exit status from runall-new.pl, expected $expected and got $actual");
        unlink $grammar;
    }
}

# Bug13720157
# Fix for KILL QUERY when MAX_ROWS_THRESHOLD is > 500000 queries bug in MySQL.pm
sub test_transformer_DISTINCT_MAX_ROWS_THRESHOLD {
    my $self = shift;
    my $query = "SELECT DISTINCT alias1 . `col_int_key` AS field1 FROM ( C AS alias1
    , ( C AS alias2 , D AS alias3 ) ) ORDER BY field1, field1;";
    ## This test requires RQG_MYSQL_BASE to point to a MySQL installation (or in-source build)
    if ($ENV{RQG_MYSQL_BASE}) {
        # Use a grammar that has a query with more than 500000 rows.
        my $grammar = 'unit/tmp/distinct_max_rows_threshold.yy';
        open(FILE, "> $grammar") or assert("Unable to create grammar file");
        print FILE "query:\n";
        print FILE "    $query\n";
        close FILE;
        
        my $rqg_opts = 
        "--grammar=$grammar " 
        .'--queries=1 '
        .'--transformer=Distinct,ExecuteAsPreparedTwice '
        .'--threads=1 ' 
        .'--basedir='.$ENV{RQG_MYSQL_BASE}.' '
        .'--vardir='.$self->{workdir};
        
        my $cmd = 'perl -MCarp=verbose ./runall-new.pl '.$rqg_opts
        .' --mtr-build-thread='.$self->{portbase}
        .' > '.$self->{logfile}.' 2>&1';
        $self->annotate("RQG command line: $cmd");
        my $status = system($cmd);
        my $expected = STATUS_OK;
        my $actual = $status >> 8;
        $self->assert_num_equals($expected, $actual, 
            "Wrong exit status from runall-new.pl, expected $expected and got $actual");
        unlink $grammar;
    }
}

# Bug14077376
# Fix for transforamed queries which are affected by KILL QUERY
sub test_transformer_ExecuteAsUpdateDelete_KILL_QUERY {
    my $self = shift;
    my $query = "SELECT STRAIGHT_JOIN CONCAT( table1.col_varchar_key ,
    table2.col_varchar_nokey ) AS field1 FROM (( SELECT SUBQUERY1_t2.* FROM ( CC AS
    SUBQUERY1_t1 STRAIGHT_JOIN ( C AS SUBQUERY1_t2 INNER JOIN D AS SUBQUERY1_t3 ON
    (SUBQUERY1_t3.col_varchar_key = SUBQUERY1_t2.col_varchar_nokey )) ON
    (SUBQUERY1_t3.col_varchar_key = SUBQUERY1_t2.col_varchar_key ))) AS table1 LEFT
    JOIN D AS table2 ON (table2.col_varchar_key = table1.col_varchar_nokey )) WHERE
    ( EXISTS ( SELECT SQL_SMALL_RESULT SUBQUERY2_t1.col_int_nokey AS
    SUBQUERY2_field1 FROM ( CC AS SUBQUERY2_t1 STRAIGHT_JOIN ( CC AS SUBQUERY2_t2
    STRAIGHT_JOIN C AS SUBQUERY2_t3 ON (SUBQUERY2_t3.col_varchar_key =
    SUBQUERY2_t2.col_varchar_key )) ON (SUBQUERY2_t3.col_varchar_key =
    SUBQUERY2_t2.col_varchar_key )) WHERE SUBQUERY2_t3.col_int_nokey =
    table2.col_int_nokey OR SUBQUERY2_t3.col_int_nokey >= table2.col_int_key )) AND
    ( table1.pk NOT IN (210) OR table1.col_int_key NOT IN (138, 199)) AND table1.pk
    <> table1.col_int_nokey ORDER BY CONCAT( table2.col_varchar_key,
    table2.col_varchar_key ), field1 ;";
    ## This test requires RQG_MYSQL_BASE to point to a MySQL installation (or in-source build)
    if ($ENV{RQG_MYSQL_BASE}) {
        # Use a grammar that has a query with more than 500000 rows.
        my $grammar = 'unit/tmp/executeasupdatedelete_kill_query.yy';
        open(FILE, "> $grammar") or assert("Unable to create grammar file");
        print FILE "query:\n";
        print FILE "    $query\n";
        close FILE;
        
        my $rqg_opts = 
        "--grammar=$grammar " 
        .'--queries=1 '
        .'--transformer=ExecuteAsUpdateDelete '
        .'--reporter=QueryTimeout '
        .'--threads=1 ' 
        .'--querytimeout=60 '
        .'--basedir='.$ENV{RQG_MYSQL_BASE}.' '
        .'--vardir='.$self->{workdir};
        
        my $cmd = 'perl -MCarp=verbose ./runall-new.pl '.$rqg_opts
        .' --mtr-build-thread='.$self->{portbase}
        .' > '.$self->{logfile}.' 2>&1';
        $self->annotate("RQG command line: $cmd");
        my $status = system($cmd);
        my $expected = STATUS_OK;
        my $actual = $status >> 8;
        $self->assert_num_equals($expected, $actual, 
            "Wrong exit status from runall-new.pl, expected $expected and got $actual");
        unlink $grammar;
    }
}

1;
