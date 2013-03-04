# Copyright (c) 2008, 2011 Oracle and/or its affiliates. All rights reserved.
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

package GenTest::Transform::Count;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';

use GenTest;
use GenTest::Transform;
use GenTest::Constants;

#
# This Transform provides the following transformations
# 
# SELECT COUNT(*) FROM ... -> SELECT * FROM ...
#
# SELECT ... FROM ... -> SELECT COUNT(*), ... FROM ...
#
# It avoids GROUP BY and any other aggregate functions because
# those are difficult to validate with a simple check such as 
# TRANSFORM_OUTCOME_COUNT
#

sub transform {
	my ($class, $orig_query) = @_;

	return STATUS_WONT_HANDLE if $orig_query =~ m{GROUP\s+BY|LIMIT|HAVING}sio;

	my ($select_list) = $orig_query =~ m{SELECT (.*?) FROM}sio;

	if ($select_list =~ m{AVG|BIT|CONCAT|DISTINCT|GROUP|MAX|MIN|STD|SUM|VAR|STRAIGHT_JOIN|SQL_SMALL_RESULT}sio) {
		return STATUS_WONT_HANDLE;
	} elsif ($select_list =~ m{SELECT\s?\*}sio) {
		# "SELECT *" was matched. Cannot have both * and COUNT(...) in SELECT list.
		$orig_query =~ s{SELECT (.*?) FROM}{SELECT COUNT(*) FROM}sio;
	} elsif ($select_list !~ m{COUNT}sio) {
		$orig_query =~ s{SELECT (.*?) FROM}{SELECT COUNT(*) , $1 FROM}sio;
	} elsif ($select_list =~ m{^\s*COUNT\(\s*\*\s*\)}sio) {
		$orig_query =~ s{SELECT .*? FROM}{SELECT * FROM}sio;
	} else {
		return STATUS_WONT_HANDLE;
	}

	return $orig_query." /* TRANSFORM_OUTCOME_COUNT */";
}

1;
