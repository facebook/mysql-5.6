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

package GenTest::Transform::ExecuteAsSPTwice;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';

use GenTest;
use GenTest::Transform;
use GenTest::Constants;

sub transform {
	my ($class, $orig_query, $executor) = @_;

	# We skip: - [OUTFILE | INFILE] queries because these are not data producing and fail (STATUS_ENVIRONMENT_FAILURE)
	return STATUS_WONT_HANDLE if $orig_query =~ m{(OUTFILE|INFILE|PROCESSLIST)}sio
	        || $orig_query !~ m{SELECT}io;

	return [
		"DROP PROCEDURE IF EXISTS stored_proc_$$",
		"CREATE PROCEDURE stored_proc_$$ () LANGUAGE SQL $orig_query",
		"CALL stored_proc_$$ /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",
                "CALL stored_proc_$$ /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",
		"DROP PROCEDURE IF EXISTS stored_proc_$$"
	];
}

1;
