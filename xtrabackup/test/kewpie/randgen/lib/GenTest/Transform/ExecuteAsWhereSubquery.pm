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

package GenTest::Transform::ExecuteAsWhereSubquery;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';

use GenTest;
use GenTest::Transform;
use GenTest::Constants;


sub transform {
	my ($class, $original_query, $executor, $original_result) = @_;

	return STATUS_WONT_HANDLE if $original_query !~ m{^\s*SELECT}sio 
	        || $original_query =~ m{LIMIT}sio
	        || $original_query =~ m{(AVG|STD|STDDEV_POP|STDDEV_SAMP|STDDEV|SUM|VAR_POP|VAR_SAMP|VARIANCE)\s*\(}sio;
	return STATUS_WONT_HANDLE if $original_result->rows() == 0;

	# This transformation can not work if the result set contains NULLs
	foreach my $orig_row (@{$original_result->data()}) {
		foreach my $orig_col (@$orig_row) {
			return STATUS_WONT_HANDLE if $orig_col eq '';
		}
	}

	my $table_name = 'transforms.where_subselect_'.$$;

	return [
		"DROP TABLE IF EXISTS $table_name",
		"CREATE TABLE $table_name $original_query",
		"SELECT * FROM $table_name WHERE (".join(', ', map { "`$_`" } @{$original_result->columnNames()}).") IN ( $original_query ) /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",
		"SELECT * FROM $table_name WHERE (".join(', ', map { "`$_`" } @{$original_result->columnNames()}).") NOT IN ( $original_query ) /* TRANSFORM_OUTCOME_EMPTY_RESULT */",
		"DROP TABLE $table_name",
	];
}

1;
