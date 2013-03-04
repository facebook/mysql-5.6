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

package GenTest::Reporter::BackupAndRestore;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use GenTest;
use GenTest::Reporter;
use GenTest::Constants;

my $count = 0;
my $file = '/tmp/rqg_backup';

sub monitor {
	my $reporter = shift;

	return STATUS_OK if $count > 0;

	my $dsn = $reporter->dsn();

	my $dbh = DBI->connect($dsn);

	unlink('/tmp/rqg_backup');
	say("Executing BACKUP DATABASE.");
	$dbh->do("BACKUP DATABASE test TO '/tmp/rqg_backup'");
	$count++;

	if (defined $dbh->err()) {
		return STATUS_DATABASE_CORRUPTION;
	} else {
		return STATUS_OK;
	}
}

sub report {
	my $reporter = shift;

	my $dsn = $reporter->dsn();

	my $dbh = DBI->connect($dsn);

	say("Executing RESTORE FROM.");
	$dbh->do("RESTORE FROM '/tmp/rqg_backup' OVERWRITE");

	if (defined $dbh->err()) {
		return STATUS_DATABASE_CORRUPTION;
	} else {
		return STATUS_OK;
	}
}

sub type {
	return REPORTER_TYPE_PERIODIC | REPORTER_TYPE_SUCCESS;
}

1;
