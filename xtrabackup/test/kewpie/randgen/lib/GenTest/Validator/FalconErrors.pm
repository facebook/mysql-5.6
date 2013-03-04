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

package GenTest::Validator::FalconErrors;

require Exporter;
@ISA = qw(GenTest::Validator GenTest);

use strict;

use GenTest;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;

1;

#
# This test examines error messages returned from Falcon in order to detect situations
# where the error message mentiones tables that were not used in the original query
#

sub validate {
        my ($comparator, $executors, $results) = @_;

	foreach my $result (@$results) {
		my $query = $result->query();
		my $error = $result->errstr();

		# This test only pertains to SELECT/INSERT/UPDATE/DELETE queries
		# It does not pertain to ALTERs because of unpredictable temporary table names
		return STATUS_OK if $query !~ m{insert|update|select|delete}sgio;

		if (
			($query =~ m{^select}sio) &&
			($error =~ m{table has uncommitted updates})
		) {
			say("Error: '".$error."' returned on a SELECT query.");
			return STATUS_DATABASE_CORRUPTION;
		}
		
	
		my $falcon_table;

		if ($error =~ m{update conflict in table .*?\.([A-Z.]*)}sio) {
			$falcon_table = $1;
		} elsif ($error =~ m{'duplicate values for key .*? in table .*?\.([A-Z.]*)}sio) {
			$falcon_table = $1;
		}

		if (
			(defined $falcon_table) &&
			($query !~ m{$falcon_table}sio)
		) {
			say("Error: '".$error."' indicates Falcon internal table mix-up.");
			return STATUS_DATABASE_CORRUPTION;
		}
	}

	return STATUS_OK;
}

1;
