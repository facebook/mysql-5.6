# Copyright (c) 2008, 2012 Oracle and/or its affiliates. All rights reserved.
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

package GenTest::Transform::DisableIndexes;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';

use GenTest;
use GenTest::Transform;
use GenTest::Constants;

sub transform {
	my ($class, $orig_query, $executor) = @_;

	# This transformer could be improved to handle tables that do not accept DISABLE KEYS (like MEMORY for instance better)
	# Currently this produces "Table storage engine for '<tablename>' doesn't have this option" when these engines are used

	# We skip: - [OUTFILE | INFILE] queries because these are not data producing and fail (STATUS_ENVIRONMENT_FAILURE)
	return STATUS_WONT_HANDLE if $orig_query =~ m{(OUTFILE|INFILE|PROCESSLIST)}sio
		|| $orig_query !~ m{SELECT}io
		|| $orig_query =~ m{LIMIT}sio;

	my $tables = $executor->metaTables();

	my $alter_disable = join('; ', map { "ALTER TABLE $_ DISABLE KEYS" } @$tables);
	my $alter_enable = join('; ', map { "ALTER TABLE $_ ENABLE KEYS" } @$tables);

	return [
		$alter_disable,
		$orig_query." /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",
		$alter_enable
	];
}

1;
