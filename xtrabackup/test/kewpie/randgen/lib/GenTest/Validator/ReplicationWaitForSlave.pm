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

package GenTest::Validator::ReplicationWaitForSlave;

require Exporter;
@ISA = qw(GenTest::Validator GenTest);

use strict;

use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;

sub init {
	my ($validator, $executors) = @_;
	my $master_executor = $executors->[0];

	my ($slave_host, $slave_port) = $master_executor->slaveInfo();

	if (($slave_host ne '') && ($slave_port ne '')) {
		my $slave_dsn = 'dbi:mysql:host='.$slave_host.':port='.$slave_port.':user=root';
		my $slave_dbh = DBI->connect($slave_dsn, undef, undef, { RaiseError => 1 });
		$validator->setDbh($slave_dbh);
	}

	return 1;
}

sub validate {
	my ($validator, $executors, $results) = @_;

	my $master_executor = $executors->[0];

	my ($file, $pos) = $master_executor->masterStatus();
	return STATUS_OK if ($file eq '') || ($pos eq '');

	my $slave_dbh = $validator->dbh();
	return STATUS_OK if not defined $slave_dbh;

	my $wait_status = $slave_dbh->selectrow_array("SELECT MASTER_POS_WAIT(?, ?)", undef, $file, $pos);
	
	if (not defined $wait_status) {
		my @slave_status = $slave_dbh->selectrow_array("SHOW SLAVE STATUS");
		my $slave_status = $slave_status[37];
		say("Slave SQL thread has stopped with error: ".$slave_status);
		return STATUS_REPLICATION_FAILURE;
	} else {
		return STATUS_OK;
	}
}

1;
