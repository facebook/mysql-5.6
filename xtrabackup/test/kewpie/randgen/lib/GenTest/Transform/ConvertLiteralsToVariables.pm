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

package GenTest::Transform::ConvertLiteralsToVariables;

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

	return STATUS_WONT_HANDLE if $orig_query =~ m{LIMIT}sgio;

	my $new_query = $orig_query;
	my $var_counter = 0;
	my @var_variables;

	# Do not match partial dates, timestamps, etc.
	if ($new_query =~ m{\s+(\d+)(\s|\)|,|;)}) {
		$new_query =~ s{\s+(\d+)}{
		    $var_counter++;
		    push @var_variables, '@var'.$var_counter." = $1";
		    ' @var'.$var_counter.' ';
		}sgexi;
	}

	$new_query =~ s{\s+'(.+?)'}{
		$var_counter++;
		push @var_variables, '@var'.$var_counter." = '$1'";
		' @var'.$var_counter.' ';
	}sgexi;

	if ($var_counter > 0) {
		return [
			"SET ".join(", ", @var_variables).";",
			$new_query." /* TRANSFORM_OUTCOME_UNORDERED_MATCH */;"
		];
	} else {
		return STATUS_WONT_HANDLE;
	}
}

1;
