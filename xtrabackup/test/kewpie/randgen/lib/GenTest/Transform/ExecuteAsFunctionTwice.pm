# Copyright (c) 2011 Oracle and/or its affiliates. All rights reserved.
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

package GenTest::Transform::ExecuteAsFunctionTwice;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';

use GenTest;
use GenTest::Transform;
use GenTest::Constants;

sub transform {
	my ($class, $original_query, $executor, $original_result) = @_;

	return STATUS_WONT_HANDLE if $original_query !~ m{SELECT}io;
	return STATUS_WONT_HANDLE if $original_result->rows() != 1;
	return STATUS_WONT_HANDLE if $#{$original_result->data()->[0]} != 0;

	my $return_type = $original_result->columnTypes()->[0];
	if ($return_type =~ m{varchar}sgio) {
		# Though the maxium varchar lenght is 65K, we are using 16K to allow up to 4-byte character sets
		$return_type .= "(16000)"
	} elsif ($return_type =~ m{char}sgio) {
		$return_type .= "(255)"
	} elsif ($return_type =~ m{decimal}sgio) {
		# Change type to avoid false compare diffs due to an incorrect decimal type being used when MAX() (and likely other similar functions) is used in the original query. Knowing what is returning decimal type (DBD or MySQL) may allow further improvement.
		$return_type =~ s{decimal}{char (255)}sio
	}

	return [
		"DROP FUNCTION IF EXISTS stored_func_$$",
		"CREATE FUNCTION stored_func_$$ () RETURNS $return_type NOT DETERMINISTIC BEGIN DECLARE ret $return_type; $original_query INTO ret ; RETURN ret; END",
		"SELECT stored_func_$$() /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",
                "SELECT stored_func_$$() /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",
		"DROP FUNCTION IF EXISTS stored_func_$$"
	];
}

1;
