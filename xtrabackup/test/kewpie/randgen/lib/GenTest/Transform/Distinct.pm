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

	# At this time we do not handle LIMIT because it may cause
	# both duplicate elimination AND extra rows to appear

	return STATUS_WONT_HANDLE if $orig_query =~ m{LIMIT}io;

	if ($orig_query =~ m{SELECT\s+DISTINCT}io) {
		$orig_query =~ s{SELECT\s+DISTINCT}{SELECT }io;
		return $orig_query." /* TRANSFORM_OUTCOME_SUPERSET */ ";
	} else {
		$orig_query =~ s{SELECT}{SELECT DISTINCT}io;
		return $orig_query." /* TRANSFORM_OUTCOME_DISTINCT */ ";
	}
}

1;
