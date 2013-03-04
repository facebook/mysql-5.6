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

package GenTest::Transform::ExecuteAsPreparedOnce;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';

use GenTest;
use GenTest::Transform;
use GenTest::Constants;

sub transform {
	my ($class, $original_query, $executor) = @_;

	return STATUS_WONT_HANDLE if $original_query !~ m{SELECT|HANDLER}sio;
	# Certain HANDLER statements can not be re-run as prepared because they advance a cursor
	return STATUS_WONT_HANDLE if $original_query =~ m{PREPARE|OPEN|CLOSE|PREV|NEXT}sio;

	return [
		"PREPARE prep_stmt_$$ FROM ".$executor->dbh()->quote($original_query),
		"EXECUTE prep_stmt_$$ /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",
		"DEALLOCATE PREPARE prep_stmt_$$"
	];
}

1;
