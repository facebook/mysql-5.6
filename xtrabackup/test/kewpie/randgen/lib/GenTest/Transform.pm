# Copyright (c) 2008,2011 Oracle and/or its affiliates. All rights reserved.
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

package GenTest::Transform;

require Exporter;
@ISA = qw(GenTest);

use strict;

use lib 'lib';
use GenTest;
use GenTest::Constants;
use GenTest::Executor::MySQL;
use Data::Dumper;

use constant TRANSFORMER_QUERIES_PROCESSED	=> 0;
use constant TRANSFORMER_QUERIES_TRANSFORMED	=> 1;

use constant TRANSFORM_OUTCOME_EXACT_MATCH	=> 1001;
use constant TRANSFORM_OUTCOME_UNORDERED_MATCH	=> 1002;
use constant TRANSFORM_OUTCOME_SUPERSET		=> 1003;
use constant TRANSFORM_OUTCOME_SUBSET		=> 1004;
use constant TRANSFORM_OUTCOME_SINGLE_ROW	=> 1005;
use constant TRANSFORM_OUTCOME_FIRST_ROW	=> 1006;
use constant TRANSFORM_OUTCOME_DISTINCT		=> 1007;
use constant TRANSFORM_OUTCOME_COUNT		=> 1008;
use constant TRANSFORM_OUTCOME_EMPTY_RESULT	=> 1009;
use constant TRANSFORM_OUTCOME_SINGLE_INTEGER_ONE	=> 1010;

my %transform_outcomes = (
	'TRANSFORM_OUTCOME_EXACT_MATCH'		=> 1001,
	'TRANSFORM_OUTCOME_UNORDERED_MATCH'	=> 1002,
	'TRANSFORM_OUTCOME_SUPERSET'		=> 1003,
	'TRANSFORM_OUTCOME_SUBSET'		=> 1004,
	'TRANSFORM_OUTCOME_SINGLE_ROW'		=> 1005,
	'TRANSFORM_OUTCOME_FIRST_ROW'		=> 1006,
	'TRANSFORM_OUTCOME_DISTINCT'		=> 1007,
	'TRANSFORM_OUTCOME_COUNT'		=> 1008,
	'TRANSFORM_OUTCOME_EMPTY_RESULT'	=> 1009,
	'TRANSFORM_OUTCOME_SINGLE_INTEGER_ONE'	=> 1010
);

# Subset of semantic errors that we may want to allow during transforms.
my %mysql_grouping_errors = (
	1004 => 'ER_NON_GROUPING_FIELD_USED',
	1055 => 'ER_WRONG_FIELD_WITH_GROUP',
	1056 => 'ER_WRONG_GROUP_FIELD',
	1140 => 'ER_MIX_OF_GROUP_FUNC_AND_FIELDS',
	1317 => 'ER_QUERY_INTERRUPTED',
	2013 => 'CR_SERVER_LOST',
	2006 => 'CR_SERVER_GONE_ERROR',
	1028 => 'ER_FILSORT_ABORT',
	1111 => 'ER_INVALID_GROUP_FUNC_USE',
	1615 => 'ER_NEED_REPREPARE',
	1060 => 'DUPLICATE_COLUMN_NAME',
	1104 => 'ER_TOO_BIG_SELECT'
);

# List of encountered errors that we want to suppress later in the test run.
my %suppressed_errors = ();

sub transformExecuteValidate {
	my ($transformer, $original_query, $original_result, $executor) = @_;

	$transformer->[TRANSFORMER_QUERIES_PROCESSED]++;

	my $transformer_output = $transformer->transform($original_query, $executor, $original_result);

	my $transform_blocks;

	if ($transformer_output =~ m{^\d+$}sgio) {
		if ($transformer_output == STATUS_WONT_HANDLE) {
			return STATUS_OK;
		} else {
			return $transformer_output;     # Error was returned and no queries
		}
	} elsif (ref($transformer_output) eq 'ARRAY') {
		if (ref($transformer_output->[0]) eq 'ARRAY') {
			# Transformation produced more than one block of queries
			$transform_blocks = $transformer_output;
		} else {
			# Transformation produced a single block of queries
			$transform_blocks = [ $transformer_output ];
		}	
	} else {
		# Transformation produced a single query, convert it to a single block
		$transform_blocks = [ [ $transformer_output ] ];
	}

	foreach my $transform_block (@$transform_blocks) {
		my @transformed_queries = @$transform_block;
		my @transformed_results;
		my $transform_outcome;
	
		$transformed_queries[0] =  "/* ".ref($transformer)." */ ".$transformed_queries[0];

		foreach my $transformed_query_part (@transformed_queries) {
			my $part_result = $executor->execute($transformed_query_part);

			if ($part_result->status() == STATUS_SKIP) {
				$transform_outcome = STATUS_OK;
				next;
			} elsif (
				($part_result->status() == STATUS_SYNTAX_ERROR) || 
				($part_result->status() == STATUS_SEMANTIC_ERROR) ||
				($part_result->status() == STATUS_SERVER_CRASHED) 
			) {
				# We normally return a critical error when a transformer returns
				# a semantic or syntactic error, because we want to detect any
				# faulty transformers, e.g. those which do not produce valid 
				# queries. However, some errors may need to be accepted in
				# certain situations.
				#
				# For example, with MySQL's ONLY_FULL_GROUP_BY sql mode, some
				# queries return grouping related errors, whereas they would
				# not return such errors without this mode, and we want to 
				# continue the test even if such errors occur.
				# We have logic in place to take care of this below.
				#
				if ( 
					($executor->type() == DB_MYSQL) && 
					(exists $mysql_grouping_errors{$part_result->err()}) 
				){
					if (rqg_debug()) {
						say("Ignoring transform ".ref($transformer)." that failed with the error: ".$part_result->errstr());
						say("Offending query is: $transformed_query_part;");
					} else {
						if (not defined $suppressed_errors{$part_result->err()}) {
							say("Ignoring transforms of the type ".ref($transformer)." that fail with an error like: ".$part_result->errstr());
							$suppressed_errors{$part_result->err()}++;
						}
					}
					# Then move on...
					# We "cheat" by returning STATUS_OK, as the validator would otherwise try to access the result.
					return STATUS_OK;
				}
				say("---------- TRANSFORM ISSUE ----------");
				say("Transform ".ref($transformer)." failed with a syntactic or semantic error: ".$part_result->err()." ".$part_result->errstr().
					"; RQG Status: ".status2text($part_result->status())." (".$part_result->status().")");
				say("Offending query is: $transformed_query_part;");
				say("Original query is: $original_query;");
				say("ERROR: Possible syntax or semantic error caused by code in transformer ".ref($transformer).
					". Raising severity to STATUS_ENVIRONMENT_FAILURE.");
				return STATUS_ENVIRONMENT_FAILURE;
			} elsif ($part_result->status() != STATUS_OK) {
				say("---------- TRANSFORM ISSUE ----------");
				say("Transform ".$transformer->name()." failed with an error: ".$part_result->err().'  '.$part_result->errstr());
				say("Transformed query was: ".$transformed_query_part);
				return $part_result->status();
			} elsif (defined $part_result->data()) {
				my $part_outcome = $transformer->validate($original_result, $part_result);
				$transform_outcome = $part_outcome if (($part_outcome > $transform_outcome) || (! defined $transform_outcome));
				push @transformed_results, $part_result if ($part_outcome != STATUS_WONT_HANDLE) && ($part_outcome != STATUS_OK);
			}
		}

		if (
			(not defined $transform_outcome) ||
			($transform_outcome == STATUS_WONT_HANDLE)
		) {
			say("Transform ".ref($transformer)." produced no query which could be validated ($transform_outcome).");
			say("The following queries were produced");
			print Dumper \@transformed_queries;
			return STATUS_ENVIRONMENT_FAILURE;
		}

		$transformer->[TRANSFORMER_QUERIES_TRANSFORMED]++;

		if ($transform_outcome != STATUS_OK) {
			return ($transform_outcome, \@transformed_queries, \@transformed_results);
		}
	}
	
	return STATUS_OK;

}

sub validate {
	my ($transformer, $original_result, $transformed_result) = @_;

	my $transformed_query = $transformed_result->query();

	my $transform_outcome;

	foreach my $potential_outcome (keys %transform_outcomes) {
		if ($transformed_query =~ m{$potential_outcome}s) {
			$transform_outcome = $transform_outcomes{$potential_outcome};
			last;
		}
	}

	if ($transform_outcome == TRANSFORM_OUTCOME_SINGLE_ROW) {
		return $transformer->isSingleRow($original_result, $transformed_result);
	} elsif ($transform_outcome == TRANSFORM_OUTCOME_DISTINCT) {
		return $transformer->isDistinct($original_result, $transformed_result);
	} elsif ($transform_outcome == TRANSFORM_OUTCOME_UNORDERED_MATCH) {
		return GenTest::Comparator::compare($original_result, $transformed_result);
	} elsif ($transform_outcome == TRANSFORM_OUTCOME_SUPERSET) {
		return $transformer->isSuperset($original_result, $transformed_result);
	} elsif ($transform_outcome == TRANSFORM_OUTCOME_FIRST_ROW) {
		return $transformer->isFirstRow($original_result, $transformed_result);
	} elsif ($transform_outcome == TRANSFORM_OUTCOME_COUNT) {
		return $transformer->isCount($original_result, $transformed_result);
	} elsif ($transform_outcome == TRANSFORM_OUTCOME_EMPTY_RESULT) {
		return $transformer->isEmptyResult($original_result, $transformed_result);
	} elsif ($transform_outcome == TRANSFORM_OUTCOME_SINGLE_INTEGER_ONE) {
		return $transformer->isSingleIntegerOne($original_result, $transformed_result);
	} else {
		return STATUS_WONT_HANDLE;
	}
}

sub isFirstRow {
	my ($transformer, $original_result, $transformed_result) = @_;

	if (
		($original_result->rows() == 0) &&
		($transformed_result->rows() == 0)
	) {
		return STATUS_OK;
	} else {
		my $row1 = join('<col>', @{$original_result->data()->[0]});
		my $row2 = join('<col>', @{$transformed_result->data()->[0]});
		return STATUS_CONTENT_MISMATCH if $row1 ne $row2;
	}
	return STATUS_OK;
}

sub isDistinct {
	my ($transformer, $original_result, $transformed_result) = @_;

	my $original_rows;
	my $transformed_rows;

	foreach my $row_ref (@{$original_result->data()}) {
		my $row = lc(join('<col>', @$row_ref));
		$original_rows->{$row}++;
	}

	foreach my $row_ref (@{$transformed_result->data()}) {
		my $row = lc(join('<col>', @$row_ref));
		$transformed_rows->{$row}++;
		return STATUS_LENGTH_MISMATCH if $transformed_rows->{$row} > 1;
	}


	my $distinct_original = join ('<row>', sort keys %{$original_rows} );
	my $distinct_transformed = join ('<row>', sort keys %{$transformed_rows} );

	if ($distinct_original ne $distinct_transformed) {
		return STATUS_CONTENT_MISMATCH;
	} else {
		return STATUS_OK;
	}
}

sub isSuperset {
	my ($transformer, $original_result, $transformed_result) = @_;
	my %rows;

	foreach my $row_ref (@{$original_result->data()}) {
		my $row = join('<col>', @$row_ref);
		$rows{$row}++;
	}

	foreach my $row_ref (@{$transformed_result->data()}) {
		my $row = join('<col>', @$row_ref);
		$rows{$row}--;
	}

	foreach my $row (keys %rows) {
		return STATUS_LENGTH_MISMATCH if $rows{$row} > 0;
	}

	return STATUS_OK;
}

sub isSingleRow {
	my ($transformer, $original_result, $transformed_result) = @_;

	if (
		($original_result->rows() == 0) &&
		($transformed_result->rows() == 0)
	) {
		return STATUS_OK;
	} elsif ($transformed_result->rows() == 1) {
		my $transformed_row = join('<col>', @{$transformed_result->data()->[0]});
		foreach my $original_row_ref (@{$original_result->data()}) {
			my $original_row = join('<col>', @$original_row_ref);
			return STATUS_OK if $original_row eq $transformed_row;
		}
		return STATUS_CONTENT_MISMATCH;
	} else {
		# More than one row, something is messed up
		return STATUS_LENGTH_MISMATCH;
	}
}

sub isCount {
	my ($transformer, $original_result, $transformed_result) = @_;

	my ($large_result, $small_result) ;

	if (
		($original_result->rows() == 0) ||
		($transformed_result->rows() == 0)
	) {
		return STATUS_OK;
	} elsif (
		($original_result->rows() == 1) &&
		($transformed_result->rows() == 1)
	) {
		return STATUS_OK;
	} elsif (
		($original_result->rows() == 1) &&
		($transformed_result->rows() >= 1)
	) {
		$small_result = $original_result;
		$large_result = $transformed_result;
	} elsif (
		($transformed_result->rows() == 1) &&
		($original_result->rows() >= 1)
	) {
		$small_result = $transformed_result;
		$large_result = $original_result;
	} else {
		return STATUS_LENGTH_MISMATCH;
	}

	if ($large_result->rows() != $small_result->data()->[0]->[0]) {
		return STATUS_LENGTH_MISMATCH;
	} else {
		return STATUS_OK;
	}
}

sub isEmptyResult {
	my ($transformer, $original_result, $transformed_result) = @_;

	if ($transformed_result->rows() == 0) {
		return STATUS_OK;
	} else {
		return STATUS_LENGTH_MISMATCH;
	}
}

sub isSingleIntegerOne {
	my ($transformer, $original_result, $transformed_result) = @_;

	if (
		($transformed_result->rows() == 1) &&
		($#{$transformed_result->data()->[0]} == 0) &&
		($transformed_result->data()->[0]->[0] eq '1')
	) {
		return STATUS_OK;
	} else {
		return STATUS_LENGTH_MISMATCH;
	}


}

sub name {
	my $transformer = shift;
	my ($name) = $transformer =~ m{.*::([a-z]*)}sgio;
	return $name;
}

sub DESTROY {
	my $transformer = shift;
	print "# ".ref($transformer).": queries_processed: ".$transformer->[TRANSFORMER_QUERIES_PROCESSED]."; queries_transformed: ".$transformer->[TRANSFORMER_QUERIES_TRANSFORMED]."\n" if rqg_debug();
}

1;
