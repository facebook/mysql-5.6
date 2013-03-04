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

# 2-way and 3-way result set comparartor with minority report in the
# case of 3-way comparision. Does also the same job as
# ResultsetComparator and may replace it for 2-ay comparision.

package GenTest::Validator::ResultsetComparator3Simplify;

require Exporter;
@ISA = qw(GenTest GenTest::Validator);

use strict;

use GenTest;
use GenTest::Constants;
use GenTest::Comparator;
use GenTest::Result;
use GenTest::Validator;
use GenTest::Simplifier::SQL;
use GenTest::Simplifier::Test;


sub compareTwo {
    my ($self, $q, $s1, $s2, $res1, $res2) = @_;
    
    my $outcome = GenTest::Comparator::compare($res1, $res2);

    if ($outcome == STATUS_LENGTH_MISMATCH) {
        if ($q =~ m{^\s*select}io) {
            say("-----------");
            say("Result length mismatch between $s1 and $s2 (".$res1->rows()." vs. ".$res2->rows().")");
            say("Query1: " . $res1->query());
            say("Query2: " . $res2->query());
            say(GenTest::Comparator::dumpDiff($res1,$res2));
        } else {
            say("-----------");
            say("Affected_rows mismatch between $s1 and $s2 (".$res1->affectedRows()." vs. ".$res2->affectedRows().")");
            say("Query1: " . $res1->query());
            say("Query2: " . $res2->query());
        }
    } elsif ($outcome == STATUS_CONTENT_MISMATCH) {
        say("-----------");
        say("Result content mismatch between $s1 and $s2.");
        say("Query1: " . $res1->query());
        say("Query2: " . $res2->query());
        say(GenTest::Comparator::dumpDiff($res1, $res2));
    }

    return $outcome;
    
}

sub simplifyTwo {
    my ($self, $query, $ex1, $ex2, $r1, $r2) = @_;

    my @executors = ( $ex1, $ex2 );

    my @results = ($r1, $r2);

    my $simplifier_sql = GenTest::Simplifier::SQL->new(
        oracle => sub {
            my $oracle_query = shift;
            
            my @oracle_results;
            foreach my $executor (@executors) {
                push @oracle_results, $executor->execute($oracle_query, 1);
            }
            my $oracle_compare = GenTest::Comparator::compare($oracle_results[0], $oracle_results[1]);
            if (
                ($oracle_compare == STATUS_LENGTH_MISMATCH) ||
                ($oracle_compare == STATUS_CONTENT_MISMATCH)
                ) {
                return ORACLE_ISSUE_STILL_REPEATABLE;
            } else {
                return ORACLE_ISSUE_NO_LONGER_REPEATABLE;
            }
        }
        );
    
    my $simplified_query = $simplifier_sql->simplify($query);
        
    if (defined $simplified_query) {
        my $simplified_results = [];

        foreach my $i (0,1) {
            $simplified_results->[$i] = 
                $executors[$i]->execute($simplified_query, 1);
            say("Simplified query".($i+1)." (".
                $executors[$i]->getName()." ".
                $executors[$i]->version()."): ".
                $simplified_results->[$i]->query().";");
        }
        
        say(GenTest::Comparator::dumpDiff($simplified_results->[0], $simplified_results->[1]));
        
        my $simplifier_test = GenTest::Simplifier::Test->new(
            executors   => \@executors ,
            results     => [ $simplified_results , \@results ]
            );
        # show_index is enabled for result difference queries its good to see the index details,
        # the value 1 is used to define if show_index is enabled, to disable dont assign a value.
	my $show_index = 1;
        my $simplified_test = $simplifier_test->simplify($show_index);

        my $tmpfile = tmpdir().$$.time().".test";
        say("Dumping .test to $tmpfile");
        open (TESTFILE, '>'.$tmpfile);
        print TESTFILE $simplified_test;
        close TESTFILE;
    } else {
        say("Could not simplify failure, appears to be sporadic.");
    }
}


sub validate {
    my ($self, $executors, $results) = @_;

    # Skip the whole stuff if only one resultset.

    return STATUS_OK if $#$results < 1;

    return STATUS_WONT_HANDLE 
        if $results->[0]->status() == STATUS_SEMANTIC_ERROR || 
           $results->[1]->status() == STATUS_SEMANTIC_ERROR;
    return STATUS_WONT_HANDLE if $results->[0]->query() =~ m{EXPLAIN}sio;

    ## Compare the two first resultsets

    my $query = $results->[0]->query();

    my $server1 = $executors->[0]->getName()." ".$executors->[0]->version();
    my $server2 = $executors->[1]->getName()." ".$executors->[1]->version();
    
    my $compare_1_2 = $self->compareTwo($query, $server1, $server2, $results->[0], $results->[1]);
    my $compare_result = $compare_1_2;

    # If 3 of them, do some more comparisions

    if ($#$results > 1) {
        my $server3 = $executors->[2]->getName()." ".$executors->[2]->version();

        return STATUS_WONT_HANDLE if $results->[2]->status() == STATUS_SEMANTIC_ERROR;

        my $compare_1_3 = $self->compareTwo($query, $server1, $server3, $results->[0], $results->[2]);

        ## If some of the above were different, we need compare 2 and
        ## 3 too to see if there exists a minority
        if ($compare_1_2 > STATUS_OK or $compare_1_3 > STATUS_OK) {

            return STATUS_WONT_HANDLE if $results->[2]->status() == STATUS_SEMANTIC_ERROR;
            
            my $compare_2_3 = $self->compareTwo($query, $server2, $server3, $results->[1], $results->[2]);

            if ($compare_1_2 > STATUS_OK and $compare_1_3 > STATUS_OK and $compare_2_3 > STATUS_OK) {
                say("Minority report: no minority");
                say("-----------");
            } elsif ($compare_1_2 > STATUS_OK and $compare_1_3 > STATUS_OK and $compare_2_3 == STATUS_OK) {
                say("Minority report: $server1(dsn1) differs from the two others");
                say("-----------");
                ####
                #### In this first shot, we only do simplification if
                #### Server 1, which is assumed to be MySQL is the
                #### minority, and we do the simplification only withe
                #### excutions on 1 and 2. This should be sufficirnt
                #### for hunting down MYSQL bugs.
                #### 
                if (($query =~ m{^\s*select}sio) && (
                        ($compare_1_2 == STATUS_LENGTH_MISMATCH) ||
                        ($compare_1_2 == STATUS_CONTENT_MISMATCH))) {
                    $self->simplifyTwo($query,$executors->[0],$executors->[1],$results->[0],$results->[1]);
                }
            } elsif ($compare_1_2 > STATUS_OK and $compare_1_3 == STATUS_OK and $compare_2_3 > STATUS_OK) {
                say("Minority report: $server2(dsn2) differs from the two others");
                say("-----------");
            } elsif ($compare_1_2 == STATUS_OK and $compare_1_3 > STATUS_OK and $compare_2_3 > STATUS_OK) {
                say("Minority report: $server3(dsn3) differs from the two others");
                say("-----------");
            }

            $compare_result = $compare_2_3 if $compare_2_3 > $compare_result;
        }
        
        $compare_result = $compare_1_3 if $compare_1_3 > $compare_result;
    }
    
    
    #
    # If the discrepancy is found on SELECT, we reduce the severity of
    # the error so that the test can continue hopefully finding
    # further errors in the same run or providing an indication as to
    # how frequent the error is.
    #
    # If the discrepancy is on an UPDATE, then the servers have
    # diverged and the test can not continue safely.
    # 

    # CAVEAT: This may cause false positives if one execution
    # influences the following executions. Failures due to a previous
    # failure have been seen.
    
    if ($query =~ m{^[\s/*!0-9]*(EXPLAIN|SELECT|ALTER|LOAD\s+INDEX|CACHE\s+INDEX)}io) {
        return $compare_result - STATUS_SELECT_REDUCTION;
    } else {
        return $compare_result;
    }
}

1;
