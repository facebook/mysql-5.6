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

package GenTest::Transform::ConvertTablesToDerived;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';
use GenTest;
use GenTest::Transform;
use GenTest::Constants;

sub transform {
	my ($class, $orig_query, $executor) = @_;

	my $modified_query = $orig_query;

	# We skip: - [OUTFILE | INFILE] queries because these are not data producing and fail (STATUS_ENVIRONMENT_FAILURE)
	return STATUS_WONT_HANDLE if $orig_query =~ m{(OUTFILE|INFILE|PROCESSLIST)}sio
		|| $orig_query =~ m{LIMIT}sio;

	# Replace AA with ( SELECT * FROM AA ) AS derived1; Add "AS derivedN" if there was no alias originally;

	my $derived_count = 1;

	$modified_query =~ s{([ `])([A-Z])([ `]|$)(\s*AS|)}{
		if ($4 eq '') {
			" ( SELECT * FROM $1$2$1 ) AS derived".$derived_count++." ";
		} else {
			" ( SELECT * FROM $1$2$1 ) AS ";
		}
	}sgoe;

	$modified_query =~ s{([ `])([A-Z])\2([ `]|$)(\s*AS|)}{
		if ($4 eq '') {
			" ( SELECT * FROM $1$2$2$1 ) AS derived".$derived_count++." ";
		} else {
			" ( SELECT * FROM $1$2$2$1 ) AS ";
		}
	}sgoe;

	$modified_query =~ s{([ `])([A-Z])\2\2([ `]|$)(\s*AS|)}{
		if ($4 eq '') {
			" ( SELECT * FROM $1$2$2$2$1 ) AS derived".$derived_count++." ";
		} else {
			" ( SELECT * FROM $1$2$2$2$1 ) AS ";
		}
	}sgoe;

	if ($modified_query ne $orig_query) {
		return [ $modified_query." /* TRANSFORM_OUTCOME_UNORDERED_MATCH */" ];
	} else {
		return STATUS_WONT_HANDLE;
	}
}
