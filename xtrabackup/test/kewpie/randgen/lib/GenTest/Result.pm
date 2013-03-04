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

package GenTest::Result;

require Exporter;
@ISA = qw(GenTest);

use strict;
use GenTest;

use constant RESULT_QUERY		=> 0;
use constant RESULT_STATUS		=> 1;
use constant RESULT_ERR			=> 2;
use constant RESULT_ERRSTR		=> 3;
use constant RESULT_SQLSTATE		=> 4;
use constant RESULT_AFFECTED_ROWS	=> 5;
use constant RESULT_DATA		=> 6;
use constant RESULT_START_TIME		=> 7;
use constant RESULT_END_TIME		=> 8;
use constant RESULT_WARNINGS		=> 9;
use constant RESULT_COLUMN_NAMES	=> 10;
use constant RESULT_MATCHED_ROWS	=> 11;
use constant RESULT_CHANGED_ROWS	=> 12;
use constant RESULT_INFO		=> 13;
use constant RESULT_COLUMN_TYPES	=> 14;
use constant RESULT_EXPLAIN		=> 15;
use constant RESULT_PERFORMANCE		=> 16;

1;

sub new {
	my $class = shift;
	return $class->SUPER::new({
		'query'		=> RESULT_QUERY,
		'status'	=> RESULT_STATUS,
		'err'		=> RESULT_ERR,
		'errstr'	=> RESULT_ERRSTR,
		'sqlstate'	=> RESULT_SQLSTATE,
		'affected_rows'	=> RESULT_AFFECTED_ROWS,
		'data'		=> RESULT_DATA,
		'start_time'	=> RESULT_START_TIME,
		'end_time'	=> RESULT_END_TIME,
		'warnings'	=> RESULT_WARNINGS,
		'column_names'	=> RESULT_COLUMN_NAMES,
		'matched_rows'	=> RESULT_MATCHED_ROWS,
		'changed_rows'	=> RESULT_CHANGED_ROWS,
		'info'		=> RESULT_INFO,
		'column_types'  => RESULT_COLUMN_TYPES,
		'explain'	=> RESULT_EXPLAIN,
		'performance'	=> RESULT_PERFORMANCE
	}, @_);
}

sub query {
	return $_[0]->[RESULT_QUERY];
}

sub status {
	return $_[0]->[RESULT_STATUS];
}

sub err {
	return $_[0]->[RESULT_ERR];
}

sub errstr {
	return $_[0]->[RESULT_ERRSTR];
}

sub sqlstate {
	return $_[0]->[RESULT_SQLSTATE];
}

sub affectedRows {
	return $_[0]->[RESULT_AFFECTED_ROWS];
}

sub matchedRows {
	return $_[0]->[RESULT_MATCHED_ROWS];
}

sub changedRows {
	return $_[0]->[RESULT_CHANGED_ROWS];
}

sub info {
	return $_[0]->[RESULT_INFO];
}

sub data {
	return $_[0]->[RESULT_DATA];
}

sub rows {
	my $result = shift;
	if (defined $result->[RESULT_DATA]) {
		return $#{$result->[RESULT_DATA]} + 1;
	} else {
		return undef;
	}
}

sub duration {
	return $_[0]->[RESULT_END_TIME] - $_[0]->[RESULT_START_TIME];
}

sub startTime {
	return $_[0]->[RESULT_START_TIME];
}

sub endTime {
	return $_[0]->[RESULT_END_TIME];
}

sub warnings {
	return $_[0]->[RESULT_WARNINGS];
}

sub setWarnings {
	$_[0]->[RESULT_WARNINGS] = $_[1];
}

sub columnNames {
	return $_[0]->[RESULT_COLUMN_NAMES];
}

sub columnTypes {
	return $_[0]->[RESULT_COLUMN_TYPES];
}

sub explain {
	return $_[0]->[RESULT_EXPLAIN];
}

sub performance {
	return $_[0]->[RESULT_PERFORMANCE];
}

1;
