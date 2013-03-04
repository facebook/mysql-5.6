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

package GenTest::Validator::RepeatableRead;

require Exporter;
@ISA = qw(GenTest::Validator GenTest);

use strict;

use GenTest;
use GenTest::Comparator;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;

#
# We check for database consistency using queries which walk the table in various ways
# We should have also probed each row individually, however this is too CPU intensive
#

my @predicates = (
	'/* no additional predicate */',				# Rerun original query (does not cause bugs)
	'AND `pk` > -16777216',						# Index scan on PK
#	'AND `int_key` > -16777216',					# Index scan on key
#	'AND `int_key` > -16777216 ORDER BY `int_key` LIMIT 1000000',	# Falcon LIMIT optimization (broken)
);

sub validate {
	my ($validator, $executors, $results) = @_;
	my $executor = $executors->[0];
	my $orig_result = $results->[0];
	my $orig_query = $orig_result->query();

	return STATUS_OK if $orig_query !~ m{^\s*select}io;
	return STATUS_OK if $orig_result->err() > 0;

	foreach my $predicate (@predicates) {
		my $new_query = $orig_query." ".$predicate;
		my $new_result = $executor->execute($new_query);
		return STATUS_OK if not defined $new_result->data();

		my $compare_outcome = GenTest::Comparator::compare($orig_result, $new_result);
		if ($compare_outcome > STATUS_OK) {
			say("Query: $orig_query returns different result when executed with additional predicate '$predicate' (".$orig_result->rows()." vs. ".$new_result->rows()." rows).");
			say(GenTest::Comparator::dumpDiff($orig_result, $new_result));

			say("Full result from the original query: $orig_query");
			print join("\n", sort map { join("\t", @$_) } @{$orig_result->data()})."\n";
			say("Full result from the follow-up query: $new_query");
			print join("\n", sort map { join("\t", @$_) } @{$new_result->data()})."\n";

			say("Executing the same queries a second time:");

			foreach my $repeat_query ($orig_query, $new_query) {
				my $repeat_result = $executor->execute($repeat_query);
				say("Full result from the repeat of the query: $repeat_query");
				print join("\n", sort map { join("\t", @$_) } @{$repeat_result->data()})."\n";
			}

			return $compare_outcome; # - STATUS_SELECT_REDUCTION;
		}
	}

	return STATUS_OK;
}

1;
