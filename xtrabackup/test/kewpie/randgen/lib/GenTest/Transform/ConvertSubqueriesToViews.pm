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

package GenTest::Transform::ConvertSubqueriesToViews;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';
use GenTest;
use GenTest::Transform;
use GenTest::Constants;

# This is a regexp to match nested brackets, taken from
# here http://www.perlmonks.org/?node_id=547596

my $paren_rx;

$paren_rx = qr{
  (?:
    \((??{$paren_rx})\) # either match another paren-set
    | [^()]+            # or match non-parens (or escaped parens
  )*
}x;

#
# This transform attempts to convert individual subqueries into VIEWs
# CREATE OR REPLACE VIEW is used to determine if a particular subquery is convertable, that is, is not correlated
# If CREATE OR REPLACE VIEW fails, the subquery is not easily convertable and we move on.
#

sub transform {
	my ($class, $query, $executor) = @_;

	my @view_ddl;
	my $view_counter = 0;
	$query =~ s{\((\s*SELECT\s+(??{$paren_rx}))\)}{
		my $subquery = $1;
		my $view_name = "view_".$$."_inline_".$view_counter;
		my $drop_view = "DROP VIEW IF EXISTS $view_name",
		my $create_view = "CREATE OR REPLACE VIEW $view_name AS $subquery;";
		if ($executor->execute($create_view, 1)->err() == 0) {
			push @view_ddl, $drop_view, $create_view;
			$view_counter++;
			"( SELECT * FROM $view_name )";
		} else {
			"( $1 )";
		}
	}sgexi;

	if ($view_counter > 0) {
		return [@view_ddl, "$query /* TRANSFORM_OUTCOME_UNORDERED_MATCH */"];
	} else {
		return STATUS_WONT_HANDLE;
	}
}

1;
