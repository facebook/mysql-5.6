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

use strict;
use lib 'lib';
use lib '../lib';
use DBI;

use GenTest::Constants;
use GenTest::Executor::MySQL;
use GenTest::Simplifier::SQL;
use GenTest::Simplifier::Test;

#
# Please modify those settings to fit your environment before you run this script
#

my $basedir = '/home/philips/bzr/maria-5.3-mwl128/';
my $vardir = $basedir.'/mysql-test/var';
my $dsn = 'dbi:mysql:host=127.0.0.1:port=19300:user=root:database=test';

my $original_query = "
	SELECT 1 FROM DUAL
";

# Maximum number of seconds a query will be allowed to proceed. It is assumed that most crashes will happen immediately after takeoff
my $timeout = 10;

my @mtr_options = (
#	'--mysqld=--innodb',
	'--mysqld=--log-output=file',	# Prevents excessively long CSV recovery on each startup
	'--start-and-exit',
	'--start-dirty',
	"--vardir=$vardir",
	"--master_port=19300",
	'--skip-ndbcluster',
	'--mysqld=--loose-core-file-size=1',
	'--fast',
	'--mysqld=--key_buffer_size=16K',
	'--mysqld=--table_open_cache=4',
	'--mysqld=--sort_buffer_size=64K',
	'--mysqld=--read_buffer_size=256K',
	'--mysqld=--read_rnd_buffer_size=256K',
	'--mysqld=--thread_stack=240K',
	'--mysqld=--loose-innodb_buffer_pool_size=16M',
	'--mysqld=--loose-innodb_additional_mem_pool_size=2M',
	'--mysqld=--innodb_log_file_size=5M',
	'--mysqld=--innodb_log_buffer_size=8M',
	'--mysqld=--innodb_flush_log_at_trx_commit=2',
	'1st'	# Required for proper operation of MTR --start-and-exit
);

my $orig_database = 'test';
my $new_database = 'crash';

my $executor;

start_server();

my $simplifier = GenTest::Simplifier::SQL->new(
	oracle => sub {
		my $oracle_query = shift;
		my $dbh = $executor->dbh();
	
		my $connection_id = $dbh->selectrow_array("SELECT CONNECTION_ID()");
		$dbh->do("CREATE EVENT timeout ON SCHEDULE AT CURRENT_TIMESTAMP + INTERVAL $timeout SECOND DO KILL QUERY $connection_id");

		$executor->execute($oracle_query);

		# Or, alternatively, execute as a prepared statement
		# $executor->execute("PREPARE prep_stmt FROM \"$oracle_query\"");
		# $executor->execute("EXECUTE prep_stmt");
		# $executor->execute("EXECUTE prep_stmt");
		# $executor->execute("DEALLOCATE PREPARE prep_stmt");

		$dbh->do("DROP EVENT IF EXISTS timeout");

		if (!$executor->dbh()->ping()) {
			start_server();
			return ORACLE_ISSUE_STILL_REPEATABLE;
		} else {
			return ORACLE_ISSUE_NO_LONGER_REPEATABLE;
		}
	}
);

my $simplified_query = $simplifier->simplify($original_query);
print "Simplified query:\n$simplified_query;\n\n";

my $simplifier_test = GenTest::Simplifier::Test->new(
	executors => [ $executor ],
	queries => [ $simplified_query , $original_query ]
);

my $simplified_test = $simplifier_test->simplify();

print "Simplified test\n\n";
print $simplified_test;

sub start_server {
	chdir($basedir.'/mysql-test') or die "Unable to chdir() to $basedir/mysql-test: $!";

	$executor = GenTest::Executor::MySQL->new( dsn => $dsn );
	$executor->init() if defined $executor;

	if ((not defined $executor) || (not defined $executor->dbh()) || (!$executor->dbh()->ping())) {
		system("MTR_VERSION=1 perl mysql-test-run.pl ".join(" ", @mtr_options));
		$executor = GenTest::Executor::MySQL->new( dsn => $dsn );
		$executor->init();
	}

	$executor->dbh()->do("SET GLOBAL EVENT_SCHEDULER = ON");
}
