# Copyright (c) 2008,2011 Oracle and/or its affiliates. All rights reserved.
# Copyright (c) 2013, Monty Program Ab.
# Copyright (c) 2014, SkySQL Ab
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


# Transformer without any result comparison, only for critical failures -- 
# just transform the query and run it 

package GenTest::Validator::TransformerNoComparator;

require Exporter;
@ISA = qw(GenTest::Validator GenTest);

use strict;

use Carp;
use GenTest;
use GenTest::Constants;

my @transformer_names;
my @transformers;
my $database_created = 0;

sub configure {
	my ($self, $props) = @_;

	my $list = $props->transformers;

	if (defined $list and $#{$list} >= 0) {
		@transformer_names = @$list;
	} else {
		croak "No transformers were defined to be run by TransformerNoComparator\n";
	}

	say("TransformerNoComparator Validator will use the following Transformers: ".join(', ', @transformer_names));

	foreach my $transformer_name (@transformer_names) {
		eval ("require GenTest::Transform::'".$transformer_name) or croak $@;
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

	return STATUS_WONT_HANDLE if defined $results->[0]->warnings();
	return STATUS_WONT_HANDLE if $results->[0]->status() != STATUS_OK;

	my $max_transformer_status; 
	foreach my $transformer (@transformers) {
		my $transformer_status = $validator->transform($transformer, $executor, $results);
		return $transformer_status if $transformer_status > STATUS_CRITICAL_FAILURE;
		$max_transformer_status = $transformer_status if $transformer_status > $max_transformer_status;
	}

	return $max_transformer_status > STATUS_SELECT_REDUCTION ? $max_transformer_status - STATUS_SELECT_REDUCTION : $max_transformer_status;
}

sub transform {
	my ($validator, $transformer, $executor, $results) = @_;

	my $original_result = $results->[0];
	my $original_query = $original_result->query();

	my ($transform_outcome, undef, undef) = $transformer->transformExecuteValidate($original_query, $original_result, $executor, 'skip_validaton');
	return $transform_outcome;
}

sub DESTROY {
	@transformers = ();
}

1;
