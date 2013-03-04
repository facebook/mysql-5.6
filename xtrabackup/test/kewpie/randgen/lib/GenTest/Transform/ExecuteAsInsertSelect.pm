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

package GenTest::Transform::ExecuteAsInsertSelect;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';

use GenTest;
use GenTest::Transform;
use GenTest::Constants;


sub transform {
	my ($class, $original_query, $executor) = @_;

	return STATUS_WONT_HANDLE if $original_query !~ m{^\s*SELECT}sio;

	my $table_name = 'transforms.insert_select_'.$$;

	return [
		"DROP TABLE IF EXISTS $table_name",

		"CREATE TABLE $table_name $original_query",
		"SELECT * FROM $table_name /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",
		"DELETE FROM $table_name",

		"INSERT INTO $table_name $original_query",
		"SELECT * FROM $table_name /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",
		"DELETE FROM $table_name",

		"REPLACE INTO $table_name $original_query",
		"SELECT * FROM $table_name /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",
		"DROP TABLE $table_name"
	];
}

1;
