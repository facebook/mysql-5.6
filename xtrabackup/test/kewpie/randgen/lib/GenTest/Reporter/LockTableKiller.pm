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

package GenTest::Reporter::LockTableKiller;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;

use constant PROCESSLIST_CONNECTION_ID		=> 0;
use constant PROCESSLIST_PROCESS_TIME		=> 5;
use constant PROCESSLIST_PROCESS_STATE		=> 6;
use constant PROCESSLIST_PROCESS_INFO		=> 7;

# Minimum lifetime of a LOCK TABLE before it is considered suspicious
use constant LOCK_LIFETIME_THRESHOLD		=> 10;	# Seconds

sub monitor {
	my $reporter = shift;

	my $dsn = $reporter->dsn();
	my $dbh = DBI->connect($dsn);

	if (defined GenTest::Executor::MySQL::errorType($DBI::err)) {
		return GenTest::Executor::MySQL::errorType($DBI::err);
	} elsif (not defined $dbh) {
		return STATUS_UNKNOWN_ERROR;
	}

	my $processlist = $dbh->selectall_arrayref("SHOW FULL PROCESSLIST");

	foreach my $process (@$processlist) {
		if (
			($process->[PROCESSLIST_PROCESS_INFO] =~ m{LOCK\s+TABLE}sio) &&
			($process->[PROCESSLIST_PROCESS_TIME] > LOCK_LIFETIME_THRESHOLD) && 
			($process->[PROCESSLIST_PROCESS_STATE] eq 'Table lock')
		) {
			say("Stalled LOCK TABLE: ".$process->[PROCESSLIST_PROCESS_INFO].". Killing query.");
			$dbh->do("KILL QUERY ".$process->[PROCESSLIST_CONNECTION_ID]);
			return STATUS_OK;
		}
	}

	return STATUS_OK;
}

sub type {
	return REPORTER_TYPE_PERIODIC;
}

1;
