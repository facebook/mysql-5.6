# Copyright (c) 2008, 2011 Oracle and/or its affiliates. All rights reserved.
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

package GenTest::Transform::InlineVirtualColumns;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';
use GenTest;
use GenTest::Transform;
use GenTest::Constants;

my $global_inlines = 0;

sub transform {
	my ($class, $query, $executor) = @_;

	return STATUS_WONT_HANDLE if $query !~ m{\s*SELECT}sio;

	my %virtual_columns;

	my $dbh = $executor->dbh();
	my ($table_name) = $query =~ m{FROM (.*?)[ ^]}sio;

	my ($foo, $table_create) = $dbh->selectrow_array("SHOW CREATE TABLE $table_name");

	foreach my $create_row (split("\n", $table_create)) {
		next if $create_row !~ m{ VIRTUAL}sio;
		my ($column_name, $column_def) = $create_row =~ m{`(.*)` [^ ]*? AS (.*) VIRTUAL}sio;
		$virtual_columns{$column_name} = $column_def;
	}

	foreach my $virtual_column (keys %virtual_columns) {
		my $inlines = $query =~ s{$virtual_column}{$virtual_columns{$virtual_column}}sgi;
		$global_inlines = $global_inlines + $inlines;
	}

	return $query." /* TRANSFORM_OUTCOME_UNORDERED_MATCH */";
}

DESTROY {
	say("[Statistics]: InlineVirtualColumns Transformer inlined $global_inlines expressions") if rqg_debug();
}

1;
