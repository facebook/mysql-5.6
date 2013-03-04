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

package GenTest::Validator::ExplicitRollback;

require Exporter;
@ISA = qw(GenTest::Validator GenTest);

use strict;

use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;

my $inconsistent_state = 0;

sub validate {
	my ($validator, $executors, $results) = @_;
	my $dsn = $executors->[0]->dsn();

	foreach my $i (0..$#$results) {
		if ($results->[$i]->status() == STATUS_TRANSACTION_ERROR) {
#			say("entering inconsistent state due to query".$results->[$i]->query());
			$inconsistent_state = 1;
		} elsif ($results->[$i]->query() =~ m{^\s*(COMMIT|START TRANSACTION|BEGIN)}sio) {
#			say("leaving inconsistent state due to query ".$results->[$i]->query());
			$inconsistent_state = 0;
		}

		if ($inconsistent_state == 1) {
#			say("Explicit rollback after query ".$results->[$i]->query());
			$executors->[$i]->dbh()->do("ROLLBACK /* Explicit ROLLBACK after a ".$results->[$i]->errstr()." error. */ ");
		}

	}
	
	return STATUS_OK;
}

1;
