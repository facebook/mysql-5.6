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

package GenTest::Reporter::ReplicationThreadRestarter;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use GenTest;
use GenTest::Reporter;
use GenTest::Constants;

sub monitor {

	my $reporter = shift;

	my $prng = $reporter->prng();

	my $slave_host = $reporter->serverInfo('slave_host');
	my $slave_port = $reporter->serverInfo('slave_port');

	my $slave_dsn = 'dbi:mysql:host='.$slave_host.':port='.$slave_port.':user=root';
	my $slave_dbh = DBI->connect($slave_dsn);

	my $verb = $prng->arrayElement(['START','STOP']);
	my $threads = $prng->arrayElement([
		'',
		'IO_THREAD',
		'IO_THREAD, SQL_THREAD',
		'SQL_THREAD, IO_THREAD',
		'SQL_THREAD'
	]);

	my $query = $verb.' SLAVE '.$threads;

	if (defined $slave_dbh) {
		$slave_dbh->do($query);
		if ($slave_dbh->err()) {
			say("Query: $query failed: ".$slave_dbh->errstr());
			return STATUS_REPLICATION_FAILURE;
		} else {
			return STATUS_OK;
		}
	} else {
		return STATUS_SERVER_CRASHED;
	}
}

sub type {
	return REPORTER_TYPE_PERIODIC;
}

1;
