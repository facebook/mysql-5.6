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

package GenTest::Validator::SelectStability;

require Exporter;
@ISA = qw(GenTest::Validator GenTest);

use strict;

use GenTest;
use GenTest::Comparator;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;
use Time::HiRes;

sub validate {
	my ($validator, $executors, $results) = @_;
	my $executor = $executors->[0];
	my $orig_result = $results->[0];
	my $orig_query = $orig_result->query();

	return STATUS_OK if $orig_query !~ m{^\s*select}io;
	return STATUS_OK if not defined $orig_result->data();

	foreach my $delay (0, 0.01, 0.1) {
		Time::HiRes::sleep($delay);
		my $new_result = $executor->execute($orig_query);
		return STATUS_OK if not defined $new_result->data();
		my $compare_outcome = GenTest::Comparator::compare($orig_result, $new_result);
		if ($compare_outcome > STATUS_OK) {
			say("Query: $orig_query; returns different result when executed after a delay of $delay seconds.");
			say(GenTest::Comparator::dumpDiff($orig_result, $new_result));
			return STATUS_DATABASE_CORRUPTION;
		}
	}

	return STATUS_OK;
}

1;
