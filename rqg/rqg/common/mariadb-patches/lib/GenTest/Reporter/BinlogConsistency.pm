# Copyright (C) 2013 Monty Program Ab
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.


#################
# Goal: check that binary logs contents correctly reflects the server data.
# 
# The reporter switches the server into read-only mode, takes a data dump,
# shuts down the server, starts a new clean one, feeds binary log from 
# the first server to the new one, takes a data dump again, 
# and compares two dumps.
#################

package GenTest::Reporter::BinlogConsistency;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;
use File::Copy;

use DBServer::MySQL::MySQLd;

my $first_reporter;

sub report {
	my $reporter = shift;

	$first_reporter = $reporter if not defined $first_reporter;
	return STATUS_OK if $reporter ne $first_reporter;

	my $server = $reporter->properties->servers->[0];
	my $status;
	my $vardir = $server->vardir();
	my $datadir = $server->datadir();
	my $port = $server->port();

	my $client = DBServer::MySQL::MySQLd::_find(undef,
		[$reporter->serverVariable('basedir')],
		osWindows()?["client/Debug","client/RelWithDebInfo","client/Release","bin"]:["client","bin"],
		osWindows()?"mysql.exe":"mysql"
	);

	unless ($client) {
		say("ERROR: Could not find mysql client. Status will be set to ENVIRONMENT_FAILURE");
		return STATUS_ENVIRONMENT_FAILURE;
	}

	$client .= " -uroot --host=127.0.0.1 --port=$port --force --protocol=tcp test";


	my $binlog = DBServer::MySQL::MySQLd::_find(undef,
											 [$reporter->serverVariable('basedir')],
											 osWindows()?["client/Debug","client/RelWithDebInfo","client/Release","bin"]:["client","bin"],
											 osWindows()?"mysqlbinlog.exe":"mysqlbinlog");

	unless ($binlog) {
		say("ERROR: Could not find mysqlbinlog. Status will be set to ENVIRONMENT_FAILURE");
		return STATUS_ENVIRONMENT_FAILURE;
	}

	my $dbh = DBI->connect($reporter->dsn());

	unless (defined $dbh) {
		say("ERROR: Could not connect to the server, nothing to dump. Status will be set to ENVIRONMENT_FAILURE");
		return STATUS_ENVIRONMENT_FAILURE;
	}

#	$dbh->do("SET GLOBAL sql_log_bin = OFF");
	my @all_binlogs = @{$dbh->selectcol_arrayref("SHOW BINARY LOGS")};
	my $binlogs_string = join(' ', map { "$vardir/vardir_orig/data/$_" } @all_binlogs );

	say("Dumping the original server...");
	$status = $reporter->dump_all($dbh, $vardir."/original.dump");
	$dbh->disconnect();
	if ($status > STATUS_OK) {
		say("ERROR: mysqldump finished with an error");
		return $status;
	}

	$status = $server->stopServer();
	sleep(5);
	$dbh = DBI->connect($reporter->dsn());

	if (defined $dbh) {
		say("ERROR: Can still connect to the server, shutdown failed. Status will be set to ENVIRONMENT_FAILURE");
		return STATUS_ENVIRONMENT_FAILURE;
	}

	my $tmpvardir = $vardir.'_'.time().'_tmp';
	move($vardir,$tmpvardir);
	
	say("Starting a new server ...");
	say("Creating a clean database...");
	$server->createMysqlBase();

	move($tmpvardir,$vardir.'/vardir_orig');
	my $status = $server->startServer();

	if ($status > STATUS_OK) {
		say("ERROR: Server startup finished with an error");
		return $status;
	}

	my $dbh_new = DBI->connect($reporter->dsn());

	unless (defined $dbh_new) {
		say("ERROR: Could not connect to the newly started server. Status will be set to ENVIRONMENT_FAILURE");
		return STATUS_ENVIRONMENT_FAILURE;
	}

	say("Feeding binary logs of the original server to the new one (@all_binlogs)");
	$status = system("$binlog $binlogs_string | $client") >> 8;
	if ($status > STATUS_OK) {
		say("ERROR: Feeding binary logs to the server finished with an error");
		return $status;
	}

	say("Dumping the new server...");
	$status = $reporter->dump_all($dbh_new, $vardir."/restored.dump");
	$dbh_new->disconnect();
	if ($status > STATUS_OK) {
		say("ERROR: mysqldump finished with an error");
		return $status;
	}

	say("Comparing SQL dumps between servers...");
	$status = system("diff -u $vardir/vardir_orig/original.dump.sorted $vardir/restored.dump.sorted") >> 8;

	unlink("$vardir/vardir_orig/original.dump.sorted");
	unlink("$vardir/restored.dump.sorted");

	if ($status == STATUS_OK) {
		say("No differences were found between servers.");
		return STATUS_OK;
	} else {
		say("Servers have diverged.");
		return STATUS_RECOVERY_FAILURE;
	}
}

sub dump_all {
	my ($reporter, $dbh, $dumpfile) = @_;
	my $server = $reporter->properties->servers->[0];

	my @all_databases = @{$dbh->selectcol_arrayref("SHOW DATABASES")};
	my $databases_string = join(' ', grep { $_ !~ m{^(mysql|information_schema|performance_schema)$}sgio } @all_databases );

	# no-create-info is needed because some table options don't survive server restart (e.g. AUTO_INCREMENT for InnoDB tables)
	# force is needed e.g. for views which reference invalid tables
	my $dump_result = $server->dumpdb($databases_string, $dumpfile);

	# We don't check "real" mysqldump exit code, because it might be bad due to view problems etc.,
	# but we still want to continue. But if sort fails, that's really bad because it must mean the file doesn't exist,
	# or something equally fatal.
	$dump_result = system("sort $dumpfile > $dumpfile.sorted");
	if ($dump_result > 0) {
		say("ERROR: dump returned error code $dump_result. Status will be set to ENVIRONMENT_FAILURE");
		return STATUS_ENVIRONMENT_FAILURE;
	}
}


sub type {
	return REPORTER_TYPE_DATA | REPORTER_TYPE_END;
}


1;
