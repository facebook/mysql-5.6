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

package GenTest::Transform::ConvertLiteralsToSubqueries;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';
use GenTest;
use GenTest::Transform;
use GenTest::Constants;

my $initialized = 0;

sub transform {
	
	my ($class, $orig_query, $executor) = @_;

	# Keep DDL's creation seperate for use in the simplified testcase.
	my $create_db="CREATE DATABASE IF NOT EXISTS literals";
	# Modified integer column datatype to support BIGINT values.
	my $create_tbl_integers="CREATE TABLE IF NOT EXISTS literals.integers (i1 BIGINT NOT NULL PRIMARY KEY)";
	my $create_tbl_strings="CREATE TABLE IF NOT EXISTS literals.strings (s1 VARCHAR(255) NOT NULL PRIMARY KEY)";
	
	if ($initialized != 1) {
		my $dbh = $executor->dbh();
		$dbh->do($create_db);
		$dbh->do($create_tbl_integers);
		$dbh->do($create_tbl_strings);
		                foreach my $i (-128..256) {
                        $dbh->do("INSERT IGNORE INTO literals.integers VALUES ($i)");
                }

		$initialized = 1;
	}


	# We skip: - LIMIT queries because LIMIT N can not be converted into LIMIT ( SELECT ... ) 
	#          - [OUTFILE | INFILE] queries because these are not data producing and fail (STATUS_ENVIRONMENT_FAILURE)
	return STATUS_WONT_HANDLE if $orig_query =~ m{LIMIT}sio
		|| $orig_query =~ m{(OUTFILE|INFILE|PROCESSLIST)}sio
		|| $orig_query =~ m{(GROUP BY|ORDER BY) \d}sio;

	my @transformed_queries;

	{
		my $new_integer_query = $orig_query;
		my @integer_literals;

		# We do not want to match "integers" in parts of dates, times, etc.
		# Thus only using those that are followed by certain characters or space.
		if ( $new_integer_query =~ m{\s+(\d+)(\s|\)|,|;)} ) {
			$new_integer_query =~ s{\s+(\d+)\s}{
				push @integer_literals, $1;
				" (SELECT i1 FROM literals.integers WHERE i1 = $1 ) ";
			}sgexi;
		}

		if ($new_integer_query ne $orig_query) {
			# Pass the DDLs created to the transforms, so it appears in the simplified Testcase.
			push @transformed_queries, [$create_db, $create_tbl_integers,
				"INSERT IGNORE INTO literals.integers VALUES ".join(",", map { "($_)" } @integer_literals).";",
				$new_integer_query." /* TRANSFORM_OUTCOME_UNORDERED_MATCH */"
			];
		}
	}

	{	
		my $new_string_query = $orig_query;
		my @string_literals;

		$new_string_query =~ s{\s+'(.+?)'}{
			push @string_literals, $1;
			" (SELECT s1 FROM literals.strings WHERE s1 = '$1' ) ";
		}sgexi;
	
		if ($new_string_query ne $orig_query) {
			# Pass the DDLs created to the transforms, so it appears in the simplified Testcase.
			push @transformed_queries, [$create_db, $create_tbl_strings,
				"INSERT IGNORE INTO literals.strings VALUES ".join(",", map { "('$_')" } @string_literals).";",
				$new_string_query." /* TRANSFORM_OUTCOME_UNORDERED_MATCH */"
			];
		}
	}

	if ($#transformed_queries > -1) {
		return \@transformed_queries;
	} else {
		return STATUS_WONT_HANDLE;
	}
}

1;
