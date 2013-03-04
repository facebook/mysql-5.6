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

package GenTest::Transform::ConvertLiteralsToDyncols;

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


	# We skip LIMIT queries because LIMIT N can not be converted into LIMIT COLUMN_GET( COLUMN_CREATE () ) 
	return STATUS_WONT_HANDLE if $orig_query =~ m{LIMIT}sio;

	my @transformed_queries;

	{
		my $new_integer_query_const = $orig_query;
		my @integer_literals;

		$new_integer_query_const =~ s{\s+(\d+)}{
			push @integer_literals, $1;
			" COLUMN_GET(COLUMN_CREATE( $1 , $1 AS INTEGER ) , $1 AS INTEGER ) ";
		}sgexi;

		if ($new_integer_query_const ne $orig_query) {
			push @transformed_queries, [
				$new_integer_query_const." /* TRANSFORM_OUTCOME_UNORDERED_MATCH */ "
			];
		}
	}

	{	
		my $new_string_query_const = $orig_query;
		my @string_literals;

		$new_string_query_const =~ s{\s+'(.+?)'}{
			" COLUMN_GET(COLUMN_CREATE( 1 , '$1' AS CHAR), 1 AS CHAR ) ";
		}sgexi;
	
		if ($new_string_query_const ne $orig_query) {
			push @transformed_queries, [
				$new_string_query_const." /* TRANSFORM_OUTCOME_UNORDERED_MATCH */ "
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
