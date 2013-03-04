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

package GenTest::Reporter::CloneSlave;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;
use GenTest::Comparator;
use Data::Dumper;
use IPC::Open2;
use IPC::Open3;

my $first_reporter;
my $clone_done;
my $client_basedir;

#
# We abort the test in case not enough binlog events have been generated
# since the test can contain FLUSH , we check for either enough binlogs
# OR enough events in last binlog .
#

use constant MINIMUM_BINLOG_COUNT	=> 10;
use constant MINIMUM_BINLOG_SIZE	=> 1000000;

sub monitor {
	my $reporter = shift;

	# In case of two servers, we will be called twice.
	# Only clone a slave when called for the master

        $first_reporter = $reporter if not defined $first_reporter;
        return STATUS_OK if $reporter ne $first_reporter;

        my $pid = $reporter->serverInfo('pid');

	return STATUS_OK if time() < ($reporter->testStart() + ($reporter->testDuration() / 2)) ;
	return STATUS_OK if $clone_done == 1;

	$clone_done = 1;
	
	my $basedir = $reporter->serverVariable('basedir');

	foreach my $path ("$basedir/../client", "$basedir/../bin", "$basedir/client/RelWithDebInfo", "$basedir/client/Debug", "$basedir/client", "$basedir/bin") {
	        if (-e $path) {
	                $client_basedir = $path;
	                last;
	        }
	}

	die "can't determine client_basedir; basedir = $basedir" if not defined $client_basedir;

	my $pid = $reporter->serverInfo('pid');
	my $binary = $reporter->serverInfo('binary');
	my $language = $reporter->serverVariable('language');
	my $lc_messages_dir = $reporter->serverVariable('lc_messages_dir');
	my $datadir = $reporter->serverVariable('datadir');
	$datadir =~ s{[\\/]$}{}sgio;
	my $slave_datadir = $datadir.'_clonedslave';
	mkdir($slave_datadir);
	my $master_port = $reporter->serverVariable('port');
	my $slave_port = $master_port + 4;
	my $pid = $reporter->serverInfo('pid');
	my $plugin_dir = $reporter->serverVariable('plugin_dir');
	my $plugins = $reporter->serverPlugins();
	my $engine = $reporter->serverVariable('storage_engine');

	my $master_dbh = DBI->connect($reporter->dsn());

	my @mysqld_options = (
		'--no-defaults',
		'--server-id=3',
		'--core-file',
		'--loose-console',
		'--language='.$language,
		'--loose-lc-messages-dir='.$lc_messages_dir,
		'--datadir="'.$slave_datadir.'"',
		'--log-output=file',
		'--skip-grant-tables',
		'--general-log',
		'--relay-log=clonedslave-relay',
		'--general_log_file="'.$slave_datadir.'/clonedslave.log"',
		'--log_error="'.$slave_datadir.'/clonedslave.err"',
		'--datadir="'.$slave_datadir.'"',
		'--port='.$slave_port,
		'--loose-plugin-dir='.$plugin_dir,
		'--max-allowed-packet=20M',
		'--innodb',
		'--sql_mode="NO_ENGINE_SUBSTITUTION"'
	);

	foreach my $plugin (@$plugins) {
		push @mysqld_options, '--plugin-load='.$plugin->[0].'='.$plugin->[1];
	};

	my $mysqld_command = $binary.' '.join(' ', @mysqld_options).' 2>&1';
	say("Starting a new mysqld for the cloned slave.");
	say("$mysqld_command.");
	my $mysqld_pid = open2(\*RDRFH, \*WTRFH, $mysqld_command);

	my $slave_dbh;

	foreach my $try (1..120) {
		sleep(1);
		$slave_dbh = DBI->connect("dbi:mysql:user=root:host=127.0.0.1:port=".$slave_port, undef, undef, { RaiseError => 0 , PrintError => 0 } );
		next if not defined $slave_dbh;
		last if $slave_dbh->ping();
	}

	return STATUS_ENVIRONMENT_FAILURE if not defined $slave_dbh;

	say("Cloned slave has started.");
	
        my @all_databases = @{$master_dbh->selectcol_arrayref("SHOW DATABASES")};
        my $databases_string = join(' ', grep { $_ !~ m{^(mysql|information_schema|performance_schema)$}sgio } @all_databases );

	my $dump_file = $slave_datadir.'/'.time().'.dump';
	say("Dumping master to $dump_file ...");
	my $mysqldump_command = $client_basedir.'/mysqldump --max_allowed_packet=25M --net_buffer_length=1M -uroot --protocol=tcp --port='.$master_port.' --single-transaction --master-data --skip-tz-utc --databases '.$databases_string.' > '.$dump_file;
	say($mysqldump_command);
	system($mysqldump_command);
	return STATUS_ENVIRONMENT_FAILURE if $? != 0;
	say("Mysqldump done.");

	say("Loading dump from $dump_file into cloned slave ...");
	my $mysql_command = $client_basedir.'/mysql -uroot --max_allowed_packet=30M --protocol=tcp --port='.$slave_port.' < '.$dump_file;
	say($mysql_command);
	system($mysql_command);
	return STATUS_ENVIRONMENT_FAILURE if $? != 0;
	say("Mysql done.");

	say("Issuing START SLAVE on the cloned slave.");

	$slave_dbh->do("
		CHANGE MASTER TO
		MASTER_PORT = ".$master_port.",
		MASTER_HOST = '127.0.0.1',
		MASTER_USER = 'root',
		MASTER_CONNECT_RETRY = 1
	");

	$slave_dbh->do("START SLAVE");

	return STATUS_OK;
}

sub report {
	my $reporter = shift;

	my $basedir = $reporter->serverVariable('basedir');

	foreach my $path ("$basedir/../client", "$basedir/../bin", "$basedir/client/RelWithDebInfo", "$basedir/client/Debug", "$basedir/client", "$basedir/bin") {
	        if (-e $path) {
	                $client_basedir = $path;
	                last;
	        }
	}

	die "can't determine client_basedir; basedir = $basedir" if not defined $client_basedir;

	my $master_port = $reporter->serverVariable('port');
	my $slave_port = $master_port + 4;
	my $master_dbh = DBI->connect($reporter->dsn());
	my $slave_dbh = DBI->connect("dbi:mysql:user=root:host=127.0.0.1:port=".$slave_port, undef, undef, { RaiseError => 1 } );

	say("Issuing START SLAVE on the cloned slave.");
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
                        say("Executing intermediate MASTER_POS_WAIT('$intermediate_binlog_file', $intermediate_binlog_pos) on cloned slave.");
                        my $intermediate_wait_result = $slave_dbh->selectrow_array("SELECT MASTER_POS_WAIT('$intermediate_binlog_file', $intermediate_binlog_pos)");
                        if (not defined $intermediate_wait_result) {
                                say("Intermediate MASTER_POS_WAIT('$intermediate_binlog_file', $intermediate_binlog_pos) failed on cloned slave on port $slave_port.");
                                return STATUS_REPLICATION_FAILURE;
                        }
                        $intermediate_binlog_pos += 10000000;
                } while (  $intermediate_binlog_pos <= $intermediate_binlog_size );
        }


	my ($final_binlog_file, $final_binlog_pos) = $master_dbh->selectrow_array("SHOW MASTER STATUS");
	exit_test(STATUS_UNKNOWN_ERROR) if !defined $final_binlog_file;

        say("Waiting for cloned slave to catch up..., file $final_binlog_file, pos $final_binlog_pos .");
	my $final_wait_result = $slave_dbh->selectrow_array("SELECT MASTER_POS_WAIT('$final_binlog_file', $final_binlog_pos)");

	if (not defined $final_wait_result) {
                say("MASTER_POS_WAIT() failed. Cloned slave replication thread not running.");
                return STATUS_REPLICATION_FAILURE;        }
	
	say("Cloned slave caught up.");

        my @all_databases = @{$master_dbh->selectcol_arrayref("SHOW DATABASES")};
        my $databases_string = join(' ', grep { $_ !~ m{^(mysql|information_schema|performance_schema)$}sgio } @all_databases );
		
	my @dump_ports = ($master_port, $slave_port);
	my @dump_files;

	foreach my $i (0..$#dump_ports) {
                say("Dumping server on port $dump_ports[$i]...");
		$dump_files[$i] = tmpdir()."/server_".$$."_".$i.".dump";

		my $dump_result = system("\"$client_basedir/mysqldump\" --hex-blob --no-tablespaces --skip-triggers --compact --order-by-primary --skip-extended-insert --no-create-info --host=127.0.0.1 --port=$dump_ports[$i] --user=root --databases $databases_string | sort > $dump_files[$i]") >> 8;
		return STATUS_ENVIRONMENT_FAILURE if $dump_result > 0;
        }

	say("Comparing SQL dumps...");
	my $diff_result = system("diff -u $dump_files[0] $dump_files[1]") >> 8;

	if ($diff_result == 0) {
		say("No differences were found between master and cloned slave.");
        }

        foreach my $dump_file (@dump_files) {
                unlink($dump_file);
        }

	return $diff_result == 0 ? STATUS_OK : STATUS_REPLICATION_FAILURE;
}

sub type {
	return REPORTER_TYPE_PERIODIC | REPORTER_TYPE_SUCCESS;
}

1;
