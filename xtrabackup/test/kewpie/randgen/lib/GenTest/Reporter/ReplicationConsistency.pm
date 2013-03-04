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

package GenTest::Reporter::ReplicationConsistency;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;

my $reporter_called = 0;

sub report {
	my $reporter = shift;

	return STATUS_WONT_HANDLE if $reporter_called == 1;
	$reporter_called = 1;

	my $master_dbh = DBI->connect($reporter->dsn(), undef, undef, {PrintError => 0});
	my $master_port = $reporter->serverVariable('port');
	my $slave_port = $master_port + 2;

        my $slave_dsn = "dbi:mysql:host=127.0.0.1:port=".$slave_port.":user=root";
        my $slave_dbh = DBI->connect($slave_dsn, undef, undef, { PrintError => 1 } );

	return STATUS_REPLICATION_FAILURE if not defined $slave_dbh;

	$slave_dbh->do("START SLAVE");

	#
	# We call MASTER_POS_WAIT at 100K increments in order to avoid buildbot timeout in case
	# one big MASTER_POS_WAIT would take more than 20 minutes.
	#

	my $sth_binlogs = $master_dbh->prepare("SHOW BINARY LOGS");
	$sth_binlogs->execute();
	while (my ($intermediate_binlog_file, $intermediate_binlog_size) = $sth_binlogs->fetchrow_array()) {
		my $intermediate_binlog_pos = $intermediate_binlog_size < 10000000 ? $intermediate_binlog_size : 10000000;
		do {
			say("Executing intermediate MASTER_POS_WAIT('$intermediate_binlog_file', $intermediate_binlog_pos).");
			my $intermediate_wait_result = $slave_dbh->selectrow_array("SELECT MASTER_POS_WAIT('$intermediate_binlog_file',$intermediate_binlog_pos)");
			if (not defined $intermediate_wait_result) {
				say("Intermediate MASTER_POS_WAIT('$intermediate_binlog_file', $intermediate_binlog_pos) failed in slave on port $slave_port. Slave replication thread not running.");
				return STATUS_REPLICATION_FAILURE;
			}
			$intermediate_binlog_pos += 10000000;
	        } while (  $intermediate_binlog_pos <= $intermediate_binlog_size );
	}

        my ($final_binlog_file, $final_binlog_pos) = $master_dbh->selectrow_array("SHOW MASTER STATUS");

	say("Executing final MASTER_POS_WAIT('$final_binlog_file', $final_binlog_pos.");
	my $final_wait_result = $slave_dbh->selectrow_array("SELECT MASTER_POS_WAIT('$final_binlog_file',$final_binlog_pos)");

	if (not defined $final_wait_result) {
		say("Final MASTER_POS_WAIT('$final_binlog_file', $final_binlog_pos) failed in slave on port $slave_port. Slave replication thread not running.");
		return STATUS_REPLICATION_FAILURE;
	} else {
		say("Final MASTER_POS_WAIT('$final_binlog_file', $final_binlog_pos) complete.");
	}

	my @all_databases = @{$master_dbh->selectcol_arrayref("SHOW DATABASES")};
	my $databases_string = join(' ', grep { $_ !~ m{^(mysql|information_schema|performance_schema)$}sgio } @all_databases );
	
	my @dump_ports = ($master_port , $slave_port);
	my @dump_files;

	foreach my $i (0..$#dump_ports) {
		say("Dumping server on port $dump_ports[$i]...");
		$dump_files[$i] = tmpdir()."/server_".$$."_".$i.".dump";
		my $dump_result = system('"'.$reporter->serverInfo('client_bindir')."/mysqldump\" --hex-blob --no-tablespaces --skip-triggers --compact --order-by-primary --skip-extended-insert --no-create-info --host=127.0.0.1 --port=$dump_ports[$i] --user=root --databases $databases_string | sort > $dump_files[$i]");
		return STATUS_ENVIRONMENT_FAILURE if $dump_result > 0;
	}

	say("Comparing SQL dumps between servers on ports $dump_ports[0] and $dump_ports[1] ...");
	my $diff_result = system("diff -u $dump_files[0] $dump_files[1]");
	$diff_result = $diff_result >> 8;

	foreach my $dump_file (@dump_files) {
		unlink($dump_file);
	}

	if ($diff_result == 0) {
		say("No differences were found between servers.");
		return STATUS_OK;
	} else {
		say("Servers have diverged.");
		return STATUS_REPLICATION_FAILURE;
	}
}

sub type {
	return REPORTER_TYPE_SUCCESS;
}

1;
