# Copyright (c) 2008, 2012 Oracle and/or its affiliates. All rights reserved.
# Copyright (c) 2013, Monty Program Ab.
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

package GenTest::Transform::ExecuteAsUpdateDelete;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';

use GenTest;
use GenTest::Transform;
use GenTest::Constants;


sub transform {
	my ($class, $orig_query, $executor, $original_result) = @_;

	# We skip: - [OUTFILE | INFILE] queries because these are not data producing and fail (STATUS_ENVIRONMENT_FAILURE)
	return STATUS_WONT_HANDLE if $orig_query =~ m{(OUTFILE|INFILE|PROCESSLIST)}sio
		|| $orig_query !~ m{^\s*SELECT}sio
	        || $orig_query =~ m{LIMIT}sio
	        || $orig_query =~ m{(AVG|STD|STDDEV_POP|STDDEV_SAMP|STDDEV|SUM|VAR_POP|VAR_SAMP|VARIANCE)\s*\(}sio
	        || $orig_query =~ m{(SYSDATE)\s*\(}sio
		|| $original_result->rows() == 0
		|| $#{$original_result->data()->[0]} != 0;	# Only single-column resultsets

	# This transformation can not work if the result set contains NULLs
	foreach my $orig_row (@{$original_result->data()}) {
		foreach my $orig_col (@$orig_row) {
			return STATUS_WONT_HANDLE if $orig_col eq '';
		}
	}

	my $table_name = 'transforms.where_updatedelete_'.abs($$);
	my $col_name = $original_result->columnNames()->[0];

	return STATUS_WONT_HANDLE if $col_name =~ m{`}sgio;

	return [
		#Include database transforms creation DDL so that it appears in the simplified testcase.
		"CREATE DATABASE IF NOT EXISTS transforms",
		"DROP TABLE IF EXISTS $table_name",
		"CREATE TABLE $table_name $orig_query",

		# If the result set has more than 1 row, we can not use it in the SET clause
		( $original_result->rows() == 1 ? 
			"UPDATE $table_name SET `$col_name` = ( $orig_query ) + 9999 WHERE `$col_name` NOT IN ( $orig_query ) " :
			"UPDATE $table_name SET `$col_name` = $col_name + 9999 WHERE `$col_name` NOT IN ( $orig_query ) "
		),

		# The queries above should not have updated any rows. Sometimes ROW_COUNT() returns -1 
		"SELECT IF((ROW_COUNT() = 0 OR ROW_COUNT() = -1), 1, 0) /* TRANSFORM_OUTCOME_SINGLE_INTEGER_ONE */",
		"SELECT * FROM $table_name /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",

		( $original_result->rows() == 1 ? 
			"UPDATE $table_name SET `$col_name` = ( $orig_query ) WHERE `$col_name` IN ( $orig_query ) " :
			"UPDATE $table_name SET `$col_name` = $col_name WHERE `$col_name` IN ( $orig_query ) "
		),

		# The queries above should have updated all rows
		"SELECT IF((ROW_COUNT() = ".$original_result->rows()." OR ROW_COUNT() = -1), 1, 0) /* TRANSFORM_OUTCOME_SINGLE_INTEGER_ONE */",
		"SELECT * FROM $table_name /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",

		# All rows should end up deleted
		"DELETE FROM $table_name WHERE `$col_name` IN ( $orig_query ) ",
		"SELECT IF((ROW_COUNT() = ".$original_result->rows()." OR ROW_COUNT() = -1), 1, 0) /* TRANSFORM_OUTCOME_SINGLE_INTEGER_ONE */",
		"SELECT * FROM $table_name /* TRANSFORM_OUTCOME_EMPTY_RESULT */",
		"DROP TABLE IF EXISTS $table_name",
	];
}

1;
