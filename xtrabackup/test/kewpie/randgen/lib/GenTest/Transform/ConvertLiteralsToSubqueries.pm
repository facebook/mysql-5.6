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


	if ($initialized != 1) {
		my $dbh = $executor->dbh();
		$dbh->do("CREATE DATABASE IF NOT EXISTS literals");
		$dbh->do("CREATE TABLE IF NOT EXISTS literals.integers (i1 INTEGER NOT NULL PRIMARY KEY)");
		foreach my $i (-128..256) {
			$dbh->do("INSERT IGNORE INTO literals.integers VALUES ($i)");
		}

		$dbh->do("CREATE TABLE IF NOT EXISTS literals.strings (s1 VARCHAR(255) NOT NULL PRIMARY KEY)");

		$initialized = 1;
	}

	# We skip LIMIT queries because LIMIT N can not be converted into LIMIT ( SELECT ... ) 
	return STATUS_WONT_HANDLE if $orig_query =~ m{LIMIT}sio;
	return STATUS_WONT_HANDLE if $orig_query =~ m{GROUP BY \d}sio;
	return STATUS_WONT_HANDLE if $orig_query =~ m{ORDER BY \d}sio;

	my @transformed_queries;

	{
		my $new_integer_query = $orig_query;
		my @integer_literals;

		# We do not want to match "integers" in parts of dates, times, etc.
		# Thus only using those that are followed by certain characters or space.
		if ( $new_integer_query =~ m{\s+(\d+)(\s|\)|,|;)} ) {
			$new_integer_query =~ s{\s+(\d+)}{
				push @integer_literals, $1;
				" (SELECT i1 FROM literals.integers WHERE i1 = $1 ) ";
			}sgexi;
		}

		if ($new_integer_query ne $orig_query) {
			push @transformed_queries, [
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
			push @transformed_queries, [
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
