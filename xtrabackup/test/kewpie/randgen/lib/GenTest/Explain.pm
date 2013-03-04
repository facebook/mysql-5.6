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

package GenTest::Explain;

require Exporter;
@ISA = qw(GenTest);

use strict;
use GenTest;

use constant EXPLAIN_DBH		=> 0;
use constant EXPLAIN_QUERY		=> 1;
use constant EXPLAIN_OUTPUT		=> 2;
use constant EXPLAIN_EXTENDED		=> 3;

1;

sub new {
	my $class = shift;
	my $explain = $class->SUPER::new({
		'dbh'		=> EXPLAIN_DBH,
		'query'		=> EXPLAIN_QUERY,
		'table'		=> EXPLAIN_OUTPUT,
		'extended'	=> EXPLAIN_EXTENDED
	}, @_);


	return $explain;
}

sub extended {
	return $_[0]->[EXPLAIN_EXTENDED];
}

1;
