# Copyright (c) 2008, 2011 Oracle and/or its affiliates. All rights reserved.
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

package GenTest::Transform::OrderBy;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';

use GenTest;
use GenTest::Transform;
use GenTest::Constants;


sub transform {
	my ($class, $original_query) = @_;

       my @selects = $original_query =~ m{(SELECT)}sgio;

       if ($original_query =~ m{GROUP\s+BY}io) {
		return STATUS_WONT_HANDLE;
	} elsif ($original_query =~ m{ORDER\s+BY[^()]*CONCAT\s*\(}sio) {
		# CONCAT() in ORDER BY requires more complex regexes below
		# for correct behavior, so we skip this query.
		return STATUS_WONT_HANDLE;
	} else {
		my $transform_outcome;
		if ($original_query =~ m{LIMIT[^()]*$}sio) {
			$transform_outcome = "TRANSFORM_OUTCOME_SUPERSET";

			if ($original_query =~ s{ORDER\s+BY[^()]*$}{}sio) {
				# Removing ORDER BY
			} elsif ($#selects == 0) {
				return STATUS_WONT_HANDLE if $original_query !~ s{LIMIT[^()]*$}{ORDER BY 1}sio;
			} else {
				return STATUS_WONT_HANDLE;
			}
		} else {
			$transform_outcome = "TRANSFORM_OUTCOME_UNORDERED_MATCH";

			if ($original_query =~ s{ORDER\s+BY[^()]*$}{}sio) {
				# Removing ORDER BY
                       } elsif ($#selects == 0) {
                               return STATUS_WONT_HANDLE if $original_query !~ s{$}{ ORDER BY 1}sio;
				# Add ORDER BY 1 (no LIMIT)
			} else {
				return STATUS_WONT_HANDLE;
			}
		}

		return $original_query." /* $transform_outcome */ ";
	}
}

1;
