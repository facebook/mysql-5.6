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

package GenTest::Transform::StraightJoin;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';

use GenTest;
use GenTest::Transform;
use GenTest::Constants;

sub transform {
	my ($class, $orig_query) = @_;

	if ($orig_query =~ m{LIMIT}){
		return STATUS_WONT_HANDLE;
	} elsif ($orig_query =~ s{SELECT\s+STRAIGHT_JOIN}{SELECT}sio) {
		return $orig_query." /* TRANSFORM_OUTCOME_UNORDERED_MATCH */";
	} elsif ($orig_query =~ m{\SELECT\s+(DISTINCT|DISTINCTROW)}io) {
		# Add STRAIGHT_JOIN after DISTINCT|DISTINCTROW

                $orig_query =~ s{SELECT\s+(DISTINCT|DISTINCTROW)}{SELECT $1 STRAIGHT_JOIN}sgio;
                return $orig_query." /* TRANSFORM_OUTCOME_UNORDERED_MATCH */";
	} else {
		# Add STRAIGHT_JOIN immediately after SELECT

		$orig_query =~ s{SELECT}{SELECT STRAIGHT_JOIN}sgio;
		return $orig_query." /* TRANSFORM_OUTCOME_UNORDERED_MATCH */";
	}
}

1;
