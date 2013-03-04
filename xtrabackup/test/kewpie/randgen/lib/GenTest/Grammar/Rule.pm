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

package GenTest::Grammar::Rule;
use strict;

1;

use constant RULE_NAME		=> 0;
use constant RULE_COMPONENTS	=> 1;

my %args = (
	'name'		=> RULE_NAME,
	'components'	=> RULE_COMPONENTS
);

sub new {
	my $class = shift;
	my $rule = bless ([], $class);

	my $max_arg = (scalar(@_) / 2) - 1;

	foreach my $i (0..$max_arg) {
		if (exists $args{$_[$i * 2]}) {
			$rule->[$args{$_[$i * 2]}] = $_[$i * 2 + 1];
		} else {
			warn("Unkown argument '$_[$i * 2]' to ".$class.'->new()');
		}
	}
	return $rule;
}

sub name {
	return $_[0]->[RULE_NAME];
}

sub components {
	return $_[0]->[RULE_COMPONENTS];
}

sub setComponents {
	$_[0]->[RULE_COMPONENTS] = $_[1];
}

sub toString {
	my $rule = shift;
	my $components = $rule->components();
	return $rule->name().":\n\t".join(" |\n\t", map { join('', @$_) } @$components).";";
}


1;
