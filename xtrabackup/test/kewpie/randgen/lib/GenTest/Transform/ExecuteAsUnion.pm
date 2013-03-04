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

package GenTest::Transform::ExecuteAsUnion;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';

use GenTest;
use GenTest::Transform;
use GenTest::Constants;

sub transform {
	my ($class, $original_query, $executor) = @_;
	
	return STATUS_WONT_HANDLE if $original_query !~ m{\s*SELECT}sio;

	my $original_query_no_limit = $original_query;
	$original_query_no_limit =~ s{LIMIT\s+\d+\s+OFFSET\s+\d+\s*$}{}sio;
	$original_query_no_limit =~ s{LIMIT\s+\d+\s*$}{}sio;

	return [
		"( $original_query ) UNION ALL ( $original_query_no_limit LIMIT 0 ) /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",
		"( $original_query_no_limit LIMIT 0 ) UNION ALL ( $original_query ) /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",
		"( $original_query ) UNION DISTINCT ( $original_query ) /* TRANSFORM_OUTCOME_DISTINCT */",
		"( $original_query ) UNION ALL ( $original_query ) /* TRANSFORM_OUTCOME_SUPERSET */"
	];
}

1;
