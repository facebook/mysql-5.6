# Copyright (c) 2008,2010 Oracle and/or its affiliates. All rights reserved.
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

package GenTest::Validator::Transformer2;

require Exporter;
@ISA = qw(GenTest::Validator GenTest);

use strict;

use GenTest;
use GenTest::Constants;
use GenTest::Comparator;
use GenTest::Simplifier::SQL;
use GenTest::Simplifier::Test;
use GenTest::Translator;
use GenTest::Translator::Mysqldump2ANSI;
use GenTest::Translator::Mysqldump2javadb;
use GenTest::Translator::MysqlDML2ANSI;

use GenTest::SimPipe::Oracle::FullScan;
use GenTest::SimPipe::Testcase;

my @transformer_names;
my @transformers;
my $database_created = 0;

sub BEGIN {
	@transformer_names = (
		'DisableJoinCache'
	);

	say("Transformer Validator will use the following Transformers: ".join(', ', @transformer_names));

	foreach my $transformer_name (@transformer_names) {
		eval ("require GenTest::Transform::'".$transformer_name) or die $@;
		my $transformer = ('GenTest::Transform::'.$transformer_name)->new();
		push @transformers, $transformer;
	}
}

sub validate {
	my ($validator, $executors, $results) = @_;

	my $executor = $executors->[0];
	my $original_result = $results->[0];
	my $original_query = $original_result->query();

	if ($database_created == 0) {
		$executor->dbh()->do("CREATE DATABASE IF NOT EXISTS transforms");
		$database_created = 1;
	}

	return STATUS_WONT_HANDLE if $original_query !~ m{^\s*SELECT}sio;
	return STATUS_WONT_HANDLE if defined $results->[0]->warnings();
	return STATUS_WONT_HANDLE if $results->[0]->status() != STATUS_OK;

	my $max_transformer_status; 
	foreach my $transformer (@transformers) {
		my $transformer_status = $validator->transform($transformer, $executor, $results);
		$transformer_status = STATUS_OK if ($transformer_status == STATUS_CONTENT_MISMATCH) && ($original_query =~ m{LIMIT}sio);
		return $transformer_status if $transformer_status > STATUS_CRITICAL_FAILURE;
		$max_transformer_status = $transformer_status if $transformer_status > $max_transformer_status;
	}

	return $max_transformer_status > STATUS_SELECT_REDUCTION ? $max_transformer_status - STATUS_SELECT_REDUCTION : $max_transformer_status;
}

sub transform {
	my ($validator, $transformer, $executor, $results) = @_;

	my $original_result = $results->[0];
	my $original_query = $original_result->query();

	my ($transform_outcome, $transformed_queries, $transformed_results) = $transformer->transformExecuteValidate($original_query, $original_result, $executor);
	return $transform_outcome if ($transform_outcome > STATUS_CRITICAL_FAILURE) || ($transform_outcome eq STATUS_OK);

	say("Original query: $original_query failed transformation with Transformer ".$transformer->name());
	say("Transformed query: ".join('; ', @$transformed_queries));

	say(GenTest::Comparator::dumpDiff($original_result, $transformed_results->[0]));

	my $oracle = GenTest::SimPipe::Oracle::FullScan->new( dsn => $executor->dsn() );

        my $simplifier_query = GenTest::Simplifier::SQL->new(
                oracle => sub {
                        my $oracle_query = shift;
                        my $oracle_result = $executor->execute($oracle_query, 1);

                        return ORACLE_ISSUE_STATUS_UNKNOWN if $oracle_result->status() != STATUS_OK;

                        my ($oracle_outcome, $oracle_transformed_queries, $oracle_transformed_results) = $transformer->transformExecuteValidate($oracle_query, $oracle_result, $executor);

                        if (
                                (($oracle_outcome == STATUS_CONTENT_MISMATCH) && ($oracle_query !~ m{LIMIT}sio)) ||
                                ($oracle_outcome == STATUS_LENGTH_MISMATCH)
                        ) {
                                return ORACLE_ISSUE_STILL_REPEATABLE;
                        } else {
                                return ORACLE_ISSUE_NO_LONGER_REPEATABLE;
                        }
                }
        );

        my $simplified_query = $simplifier_query->simplify($original_query);
	my $simplified_result = $executor->execute($simplified_query, 1);

        if (not defined $simplified_query) {
                say("Simplification failed -- failure is likely sporadic.");
		return $transform_outcome;
        } 


                say("Simplified query: $simplified_query");

                my ($simplified_transform_outcome, $simplified_transformed_queries, $simplified_transformed_results) = $transformer->transformExecuteValidate($simplified_query, $simplified_result, $executor);

                my $simplified_transformed_queries_str;
                if (ref($simplified_transformed_queries) eq 'ARRAY') {
                        $simplified_transformed_queries_str = join('; ', @$simplified_transformed_queries);
                } else {
                        $simplified_transformed_queries_str = $simplified_transformed_queries;
                }

                say("Simplified transformed query: $simplified_transformed_queries_str");

                if (defined $simplified_transformed_results->[0]->warnings()) {
                        say("Simplified transformed query produced warnings.");
        #               return STATUS_WONT_HANDLE;
                }

                my @explains;
                $explains[0] = $executor->execute("EXPLAIN ".$simplified_query);
                foreach my $simplified_transformed_query (@$simplified_transformed_queries) {
                        $executor->execute($simplified_transformed_query);
                        if ($simplified_transformed_query eq $simplified_transformed_results->[0]->query()) {
                                $explains[1] = $executor->execute("EXPLAIN ".$simplified_transformed_query)
                        }
                }

                say("EXPLAIN diff:");
                say(GenTest::Comparator::dumpDiff(@explains));

                say("Result set diff:");
                say(GenTest::Comparator::dumpDiff($simplified_result, $simplified_transformed_results->[0]));

	my $testcase = GenTest::SimPipe::Testcase->newFromDSN($executor->dsn(), [ $simplified_query ] );

	my $simplified_testcase = $testcase->simplify($oracle);

	if (defined $simplified_testcase) {
		print $simplified_testcase->toString();
	} else {
		say("Unable to simplify ...");
		return STATUS_PERL_FAILURE;
	}

	return $transform_outcome;
}

sub DESTROY {
	@transformers = ();
}

1;
