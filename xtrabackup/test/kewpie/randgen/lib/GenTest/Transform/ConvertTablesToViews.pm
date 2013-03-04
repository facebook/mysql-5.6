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

package GenTest::Transform::ConvertTablesToViews;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';
use GenTest;
use GenTest::Transform;
use GenTest::Constants;

sub transform {
	my ($class, $orig_query, $executor) = @_;

	# We replace AA with view_AA, keeping the exact quotes (or lack thereof) from the original query

	$orig_query =~ s{([ `])([A-Z])[ `]}{$1view_$2$1}sgo;
	$orig_query =~ s{([ `])(([A-Z])\3)[ `]}{$1view_$2$1}sgo;
	$orig_query =~ s{([ `])(([A-Z])\3\3)[ `]}{$1view_$2$1}sgo;

	return [ $orig_query." /* TRANSFORM_OUTCOME_UNORDERED_MATCH */" ];
}

1;
