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
#

package GenTest::SimPipe::Oracle;

require Exporter;
@ISA = qw(GenTest);
@EXPORT = qw();

use strict;
use DBI;
use GenTest;

use constant ORACLE_DSN		=> 0;
use constant ORACLE_DSN2	=> 1;
use constant ORACLE_BASEDIR	=> 2;

1;

sub new {
	my $class = shift;

	my $oracle = $class->SUPER::new({
		'dsn' => ORACLE_DSN,
		'dsn2' => ORACLE_DSN2,
		'basedir' => ORACLE_BASEDIR
	}, @_);
	
	return $oracle;
}

sub dsn {
	return $_[0]->[ORACLE_DSN];
}

sub dsn2 {
	return $_[0]->[ORACLE_DSN2];
}

sub basedir {
	return $_[0]->[ORACLE_BASEDIR];
}
