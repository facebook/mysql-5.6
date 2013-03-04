# Copyright (C) 2008-2009 Sun Microsystems, Inc. All rights reserved.
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

package GenTest::Transform::InlineSubqueries;

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

sub transform {
	my ($class, $query, $executor) = @_;

	my $inline_successful = 0;
	$query =~ s{(\(\s*SELECT\s+(??{$paren_rx})\))}{
		my $result = $executor->execute($1, 1);

		if (
			($result->status() != STATUS_OK) ||
			($result->rows() < 1)
		) {
			$1;				# return original query
		} else {
			$inline_successful = 1;		# return inlined literals
			" ( ".join(', ', map {
				if (not defined $_->[0]) {
					"NULL";
				} elsif ($_->[0] =~ m{^\d+$}sio){
					$_->[0];
				} else {
					"'".$_->[0]."'"
				}
			} @{$result->data()})." ) ";
		}
	}sgexi;

	my $final_result = $executor->execute($query, 1);
	
	if (
		($inline_successful) &&
		($final_result->status() == STATUS_OK)
	) {
		return $query." /* TRANSFORM_OUTCOME_UNORDERED_MATCH */";
	} else {
		return STATUS_WONT_HANDLE;
	}
}

1;
