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

package GenTest::Reporter::ReplicationSemiSync;

#
# The purpose of this Reporter is to test Semi-synchronous replication as follows:
#
#  At every monitoring cycle, we issue an adverse event against the slave or the master/slave connection and then:
#
# 1. Check that the slave IO thread is up to date with the master
#
# 2A. We wait for 1/2 of the timeout period, and then we check various counters to see that no transactions
#    have committed while the slave was not available OR 
#
# 2B. We wait for more than the timeout period and then we check that some transactions have moved forward
#
# 3. We restart replication in order to allow the slave to catch up, and check that the master is back to
#    semisync replication
#

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use GenTest;
use GenTest::Reporter;
use GenTest::Constants;

my $rpl_semi_sync_master_timeout = 10;

sub monitor {
	my $reporter = shift;

	say("GenTest::Reporter::ReplicationSemiSync: Test cycle starting.");

	my $prng = $reporter->prng();

	my $slave_host = $reporter->serverInfo('slave_host');
	my $slave_port = $reporter->serverInfo('slave_port');

	my $master_dsn = $reporter->dsn();
	my $slave_dsn = 'dbi:mysql:host='.$slave_host.':port='.$slave_port.':user=root';

	my $slave_dbh = DBI->connect($slave_dsn);
	my $master_dbh = DBI->connect($master_dsn);

	$master_dbh->do("SET GLOBAL rpl_semi_sync_master_enabled = 1");
	$master_dbh->do("SET GLOBAL rpl_semi_sync_master_trace_level = 80");
	$slave_dbh->do("SET GLOBAL rpl_semi_sync_slave_enabled = 1");
	$slave_dbh->do("SET GLOBAL rpl_semi_sync_slave_trace_level = 80");

	return STATUS_REPLICATION_FAILURE if waitForSlave($master_dbh, $slave_dbh, 1);
# 	sleep(1);

# 	my ($unused2, $rpl_semi_sync_master_status_first) = $master_dbh->selectrow_array("SHOW STATUS LIKE 'Rpl_semi_sync_master_status'");

# 	if (
# 		($rpl_semi_sync_master_status_first eq '') ||
# 		($rpl_semi_sync_master_status_first eq 'OFF')
# 	) {
# 		say("GenTest::Reporter::ReplicationSemiSync: Semisync replication is not enabled: rpl_semi_sync_master_status = $rpl_semi_sync_master_status_first.");
# 		return STATUS_REPLICATION_FAILURE;
# 	}

#	$master_dbh->do("SET GLOBAL rpl_semi_sync_master_timeout = ".($rpl_semi_sync_master_timeout * 1000));
#	say("GenTest::Reporter::ReplicationSemiSync: Acquiring the global read lock.");
#	$master_dbh->do("FLUSH NO_WRITE_TO_BINLOG TABLES WITH READ LOCK");
#	say("GenTest::Reporter::ReplicationSemiSync: stopping slave IO thread.");
#	$slave_dbh->do("STOP SLAVE IO_THREAD");
#	say("GenTest::Reporter::ReplicationSemiSync: stopped slave IO thread.");

#	return STATUS_REPLICATION_FAILURE if isSlaveBehind($master_dbh, $slave_dbh);

#	$master_dbh->do("FLUSH NO_WRITE_TO_BINLOG STATUS");
#	say("GenTest::Reporter::ReplicationSemiSync: Flushed status.");
#	$master_dbh->do("UNLOCK TABLES");
#	say("GenTest::Reporter::ReplicationSemiSync: Released the global read lock.");
#	my ($unusedA, $rpl_semi_sync_master_yes_tx_atflush) = $master_dbh->selectrow_array("SHOW STATUS LIKE 'Rpl_semi_sync_master_yes_tx'");
#	my ($unusedB, $rpl_semi_sync_master_no_tx_atflush) = $master_dbh->selectrow_array("SHOW STATUS LIKE 'Rpl_semi_sync_master_no_tx'");

	# Pick a sleep interval that is either more or less than the semisync timeout

	my $sleep_interval = $prng->int(0, 1) == 1 ? ($rpl_semi_sync_master_timeout * 2) : 5;
	say("GenTest::Reporter::ReplicationSemiSync: Sleeping for $sleep_interval seconds.");
	sleep($sleep_interval);

	my ($unused4, $rpl_semi_sync_master_yes_tx) = $master_dbh->selectrow_array("SHOW STATUS LIKE 'Rpl_semi_sync_master_yes_tx'");
	my ($unused5, $rpl_semi_sync_master_no_tx) = $master_dbh->selectrow_array("SHOW STATUS LIKE 'Rpl_semi_sync_master_no_tx'");
	my ($unused6, $rpl_semi_sync_master_status_after) = $master_dbh->selectrow_array("SHOW STATUS LIKE 'Rpl_semi_sync_master_status'");

	#
	# If we slept more than the semisync timeout, then we can expect that transactions have been committed
	# If we slept less, then no transactions should have committed
	#

	if ($sleep_interval > $rpl_semi_sync_master_timeout) {
                if ($rpl_semi_sync_master_status_after eq 'ON') {
                        say("GenTest::Reporter::ReplicationSemiSync: rpl_semi_sync_master_status = ON even after stopping for longer than the timeout.");
                        return STATUS_REPLICATION_FAILURE;
                } elsif ($rpl_semi_sync_master_no_tx == 0) {
			say("GenTest::Reporter::ReplicationSemiSync: Transactions were not committed asynchronously while slave was stopped for longer than the timeout.");
			say("GenTest::Reporter::ReplicationSemiSync: rpl_semi_sync_master_no_tx = $rpl_semi_sync_master_no_tx;");
		} elsif ($rpl_semi_sync_master_yes_tx > 0) {
			say("GenTest::Reporter::ReplicationSemiSync: Transactions were committed semisynchronously while slave was stopped longer than the timeout.");
			say("GenTest::Reporter::ReplicationSemiSync: rpl_semi_sync_master_yes_tx = $rpl_semi_sync_master_yes_tx;");
			return STATUS_REPLICATION_FAILURE;
		}
	} else {

#		jasonh says that this condition is not guaranteed - if we detect a slave problem, we abort immediately and do not bother
#		to wait for the full timeout
#
		if ($rpl_semi_sync_master_status_after eq 'OFF') {
			say("GenTest::Reporter::ReplicationSemiSync: rpl_semi_sync_master_status = OFF even after stopping for less than the timeout.");
			return STATUS_REPLICATION_FAILURE;
		} elsif ($rpl_semi_sync_master_no_tx > 0) {
			say("GenTest::Reporter::ReplicationSemiSync: Transactions were committed asynchronously while slave was stopped for less than the timeout.");
			say("GenTest::Reporter::ReplicationSemiSync: rpl_semi_sync_master_no_tx = $rpl_semi_sync_master_no_tx;");
			return STATUS_REPLICATION_FAILURE;
		} elsif ($rpl_semi_sync_master_yes_tx > 0) {
			say("GenTest::Reporter::ReplicationSemiSync: Transactions were committed semisynchronously while slave was stopped for less than the timeout.");
			say("GenTest::Reporter::ReplicationSemiSync: rpl_semi_sync_master_yes_tx = $rpl_semi_sync_master_yes_tx;");
			return STATUS_REPLICATION_FAILURE;
		} else {
#			return STATUS_REPLICATION_FAILURE if isSlaveBehind($master_dbh, $slave_dbh);
		}
	}
	
	say("GenTest::Reporter::ReplicationSemiSync: Starting slave IO thread.");
	$slave_dbh->do("START SLAVE IO_THREAD");

	#
	# Make sure master and slave can reconcile and semisync will be turned on again
	#

	return STATUS_REPLICATION_FAILURE if waitForSlave($master_dbh, $slave_dbh, 0);
# 	sleep(1);

# 	my ($unused7, $rpl_semi_sync_master_status_last) = $master_dbh->selectrow_array("SHOW STATUS LIKE 'Rpl_semi_sync_master_status'");
# 	if ($rpl_semi_sync_master_status_last eq 'OFF') {
# 		say("GenTest::Reporter::ReplicationSemiSync: Master has failed to return to semisync replication even after the slave has reconnected.");
# 		return STATUS_REPLICATION_FAILURE;
# 	}

# 	say("GenTest::Reporter::ReplicationSemiSync: test cycle ending with Rpl_semi_sync_master_status = $rpl_semi_sync_master_status_last.");

	return STATUS_OK;
}

sub type {
	return REPORTER_TYPE_PERIODIC;
}

sub isSlaveBehind {
	my ($master_dbh, $slave_dbh) = @_;

	my $binlogs = $master_dbh->selectall_arrayref("SHOW BINARY LOGS");
	my ($last_log_name, $last_log_pos) = ($binlogs->[$#$binlogs]->[0], $binlogs->[$#$binlogs]->[1]);
	my ($last_log_id) = $last_log_name =~ m{(\d+)}sgio;
	say("Master: last_log_name = $last_log_name; last_log_pos = $last_log_pos; $last_log_id = $last_log_id.");
			
	my $slave_status = $slave_dbh->selectrow_arrayref("SHOW SLAVE STATUS");
	my ($master_log_file, $read_master_log_pos) = ($slave_status->[5], $slave_status->[6]);
	my ($master_log_id) = $master_log_file =~ m{(\d+)}sgio;
	say("GenTest::Reporter::ReplicationSemiSync: slave: master_log_file = $master_log_file; read_master_log_pos = $read_master_log_pos; master_log_id = $master_log_id.");
	if ( 
		($last_log_id < $master_log_id) ||
		($last_log_id == $master_log_id) && ($last_log_pos > $read_master_log_pos)
	) {
		my ($unused, $rpl_semi_sync_master_status) = $master_dbh->selectrow_array("SHOW STATUS LIKE 'Rpl_semi_sync_master_status'");
		say("GenTest::Reporter::ReplicationSemiSync: Slave has lagged behind while Rpl_semi_sync_master_status = $rpl_semi_sync_master_status.");
		return STATUS_REPLICATION_FAILURE;
	}
}

sub waitForSlave {
	my ($master_dbh, $slave_dbh, $stop_slave) = @_;

	say("GenTest::Reporter::ReplicationSemiSync: Flushing tables with read lock on master...");
	$master_dbh->do("FLUSH NO_WRITE_TO_BINLOG TABLES WITH READ LOCK");
	say("GenTest::Reporter::ReplicationSemiSync: ... flushed.");

	my ($file, $pos) = $master_dbh->selectrow_array("SHOW MASTER STATUS");

	if (($file eq '') || ($pos eq '')) {
		 say("GenTest::Reporter::ReplicationSemiSync: SHOW MASTER STATUS failed.");
		 return STATUS_REPLICATION_FAILURE;
	}

	say("GenTest::Reporter::ReplicationSemiSync: Waiting for slave...");
	#say("SHOW MASTER STATUS: " . $file . ", " . $pos);
	my $wait_status = $slave_dbh->selectrow_array("SELECT MASTER_POS_WAIT(?, ?)", undef, $file, $pos);
	say("GenTest::Reporter::ReplicationSemiSync: ... slave caught up with master.");

	#my ($new_file, $new_pos) = $master_dbh->selectrow_array("SHOW MASTER STATUS");
	#say("SHOW MASTER STATUS: " . $new_file . ", " . $new_pos);

	my ($unused2, $rpl_semi_sync_master_status) = $master_dbh->selectrow_array("SHOW STATUS LIKE 'Rpl_semi_sync_master_status'");
	if (not $rpl_semi_sync_master_status eq 'ON') {
	    say("GenTest::Reporter::ReplicationSemiSync: Master has failed to return to semisync replication even after the slave has caught up.");
	    return STATUS_REPLICATION_FAILURE;
	}

	if ($stop_slave) {
	    $master_dbh->do("FLUSH NO_WRITE_TO_BINLOG STATUS");
	    say("GenTest::Reporter::ReplicationSemiSync: Flushed status.");
	    $master_dbh->do("SET GLOBAL rpl_semi_sync_master_timeout = ".($rpl_semi_sync_master_timeout * 1000));
	    say("GenTest::Reporter::ReplicationSemiSync: stopping slave IO thread.");
	    $slave_dbh->do("STOP SLAVE IO_THREAD");
	    say("GenTest::Reporter::ReplicationSemiSync: stopped slave IO thread.");
	}

	$master_dbh->do("UNLOCK TABLES");

	if (not defined $wait_status) {
		say("GenTest::Reporter::ReplicationSemiSync: MASTER_POS_WAIT() has failed. Slave SQL thread has likely stopped.");
		return STATUS_REPLICATION_FAILURE;
	}
	return 0;
}

1;
