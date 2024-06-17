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

package GenTest::Transform::DisableOptimizations;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';
use GenTest;
use GenTest::Transform;
use GenTest::Constants;

#
# This Transformer simply disables all optimizer switches except for in_to_exists
#

sub transform {
	my ($class, $original_query, $executor, $original_result, $skip_result_validations) = @_;

		return STATUS_WONT_HANDLE 
	if ( $skip_result_validations 
			and $original_query !~ m{^\s*(SELECT|UPDATE|DELETE|CREATE\s+OR\s+REPLACE\s+?TABLE.+SELECT|INSERT.+SELECT)}sio )
		or ( ( ! $skip_result_validations and ( $original_query !~ m{^\s*(SELECT)}sio or $original_query =~ m{\sINTO\s}sio ) ) ) ;

	return [
		'SET @switch_saved = @@optimizer_switch;',
		'SET SESSION optimizer_switch = REPLACE( @@optimizer_switch, "=on", "=off" );',
		'SET SESSION optimizer_switch = "in_to_exists=on";',
		"$original_query /* TRANSFORM_OUTCOME_UNORDERED_MATCH */ ;",
		'SET SESSION optimizer_switch=@switch_saved'
	];
}

1;

