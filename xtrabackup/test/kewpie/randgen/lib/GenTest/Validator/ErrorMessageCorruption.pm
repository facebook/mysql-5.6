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

package GenTest::Validator::ErrorMessageCorruption;

require Exporter;
@ISA = qw(GenTest::Validator GenTest);

use strict;

use GenTest;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;

use constant ASCII_ALLOWED_MIN	=> chr(32);  # space
use constant ASCII_ALLOWED_MAX  => chr(126); # tilde

1;

sub validate {
        my ($comparator, $executors, $results) = @_;
	my ($ascii_min, $ascii_max) = (ASCII_ALLOWED_MIN, ASCII_ALLOWED_MAX);
	foreach my $result (@$results) {
		if (
			(defined $result->errstr()) &&
			($result->errstr() =~ m{[^$ascii_min-$ascii_max\s]}siox )
		) {
			say("Error: '".$result->errstr()."' indicates memory corruption. Note that this may be a false alarm if the test contains non-ascii character sets and non-alphanumeric characters.");
			return STATUS_DATABASE_CORRUPTION;
		}
	}		

	return STATUS_OK;
}

1;
