# Copyright (c) 2008, 2012 Oracle and/or its affiliates. All rights reserved.
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

package GenTest::Transform::Distinct;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';

use GenTest;
use GenTest::Transform;
use GenTest::Constants;

sub transform {
	my ($class, $orig_query) = @_;

	# We skip: - [OUTFILE | INFILE] queries because these are not data producing and fail (STATUS_ENVIRONMENT_FAILURE)
	#          - LIMIT queries ftm because it may cause both duplicate elimination AND extra rows to appear
	#          - Certain aggregate functions, as DISTINCT will just change a number, 
	#            not cause the result to be a superset or distinct version of the original.
	return STATUS_WONT_HANDLE if $orig_query =~ m{(OUTFILE|INFILE|PROCESSLIST)}sio
	        || $orig_query =~ m{LIMIT}io
		|| $orig_query =~ m{(COUNT|SUM|AVG|STD|STDDEV_POP|STDDEV_SAMP|STDDEV|SUM|VAR_POP|VAR_SAMP|VARIANCE)\s*\(}io;

	if ($orig_query =~ m{SELECT\s+DISTINCT}io) {
		$orig_query =~ s{SELECT\s+DISTINCT}{SELECT }io;
		return $orig_query." /* TRANSFORM_OUTCOME_SUPERSET */ ";
	} else {
		$orig_query =~ s{SELECT}{SELECT DISTINCT}io;
		return $orig_query." /* TRANSFORM_OUTCOME_DISTINCT */ ";
	}
}

1;
