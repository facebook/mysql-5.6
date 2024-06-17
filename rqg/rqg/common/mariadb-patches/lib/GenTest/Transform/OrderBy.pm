# Copyright (c) 2008, 2012 Oracle and/or its affiliates. All rights reserved.
# Copyright (c) 2014 SkySQL Ab
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

	return STATUS_WONT_HANDLE if $original_query !~ m{^\s*SELECT}sio;
	# We skip: - [OUTFILE | INFILE] queries because these are not data producing and fail (STATUS_ENVIRONMENT_FAILURE)
	#          - CONCAT() in ORDER BY queries, which require more complex regexes below for correct behavior
	#          - INTO, because there will be nothing to compare

	return STATUS_WONT_HANDLE if $original_query =~ m{(OUTFILE|INFILE|PROCESSLIST|INTO|GROUP\s+BY|ORDER\s+BY[^()]*CONCAT\s*\()}sio;
		
	my $transform_outcome;

	if ($original_query =~ m{LIMIT[^()]*$}sio) {
		$transform_outcome = "TRANSFORM_OUTCOME_SUPERSET";

		if ($original_query =~ s{ORDER\s+BY.*$}{}sio) {
			# Removing ORDER BY
		} elsif ($original_query !~ s{LIMIT[^()]*$}{ORDER BY 1}sio) {
			return STATUS_WONT_HANDLE;
		} 
	} else {
		$transform_outcome = "TRANSFORM_OUTCOME_UNORDERED_MATCH";
	}

	return $original_query." /* $transform_outcome */ ";
}

1;
