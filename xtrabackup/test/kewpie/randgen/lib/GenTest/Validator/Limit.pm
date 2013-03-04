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

package GenTest::Validator::Limit;

require Exporter;
@ISA = qw(GenTest::Validator GenTest);

use strict;

use GenTest;
use GenTest::Comparator;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;

sub validate {
	my ($validator, $executors, $results) = @_;

	my $executor = $executors->[0];
	my $orig_result = $results->[0];
	my $orig_query = $orig_result->query();

	return STATUS_OK if $orig_query !~ m{^\s*select}io;
	return STATUS_OK if $orig_query =~ m{limit}io;

	my $fields = $executor->metaColumns();
	my @field_orders = map { 'ORDER BY `'.$_.'` LIMIT 1073741824' } @$fields;

	my $new_query = $orig_query;
	# Remove existing ORDER BY if present 
	$new_query =~ s{ORDER BY [^)]*?$}{}io;

	foreach my $predicate ( @field_orders ) {
		my $new_result = $executor->execute($new_query." ".$predicate);
		return STATUS_OK if not defined $new_result->data();
		my $compare_outcome = GenTest::Comparator::compare($orig_result, $new_result);

		if (
			($compare_outcome ==STATUS_LENGTH_MISMATCH) ||
			($compare_outcome ==STATUS_CONTENT_MISMATCH)
		) {
			say("Query: $orig_query returns different result when executed with predicate '$predicate' (".$orig_result->rows()." vs. ".$new_result->rows()." rows).");
			say(GenTest::Comparator::dumpDiff($orig_result, $new_result));
		}

		return $compare_outcome if $compare_outcome > STATUS_OK;
	}

	return STATUS_OK;
}

1;
