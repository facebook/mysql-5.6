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
#

package GenTest::SimPipe::Oracle::Crash;

require Exporter;
@ISA = qw(GenTest::SimPipe::Oracle GenTest);
@EXPORT = qw();

use strict;
use DBI;
use GenTest;
use GenTest::SimPipe::Oracle;
use GenTest::Constants;
use GenTest::Executor;
use GenTest::Comparator;
use Data::Dumper;

1;

sub oracle {
	my ($oracle, $testcase) = @_;

	my $executor = GenTest::Executor->newFromDSN($oracle->dsn());
	$executor->init();
	
	my $dbh = $executor->dbh();
	start_server() if not defined $dbh || !$dbh->ping();

	my $testcase_string = join("\n", (
		"CREATE DATABASE IF NOT EXISTS crash;",
		"USE crash;",
		$testcase->mysqldOptionsToString(),
		$testcase->dbObjectsToString()
	));

	$dbh->do($testcase_string, { RaiseError => 1 , mysql_multi_statements => 1 });

	foreach my $query (@{$testcase->queries()}) {
		my $sth = $dbh->prepare($query, {RaiseError => 1, mysql_multi_statements => 1});
		$sth->execute();
		say("Result: ".$sth->err()." ".$sth->errstr())
	}

	$testcase_string .= "\n".join(";\n", @{$testcase->queries()})."\n";

	# We use our home-grown ping here and not $dbh->ping() as $dbh->ping() was foind
	# to be buggy in DBD::MySQL.For some reason, it returned TRUE even on a crashed server

	my ($ping) = $dbh->selectrow_array("SELECT 'working';");

	if ($ping eq 'working') {
		say("Ping working. Not repeatable.");
		open(NR, ">/tmp/last_not_repeatable.test");
		print NR $testcase_string;
		close NR;
		return ORACLE_ISSUE_NO_LONGER_REPEATABLE;
	} else {
		say("Ping not working. Server has likely crashed. Repeatable.");
		open(NR, ">/tmp/last_repeatable.test");
		print NR $testcase_string;
		close NR;
		$oracle->startServer();
		return ORACLE_ISSUE_STILL_REPEATABLE;
	}	
}

sub startServer {
	my $oracle = shift;
	my $basedir = $oracle->basedir();

	chdir($basedir.'/mysql-test');
	system("MTR_VERSION=1 perl mysql-test-run.pl --start-and-exit --mysqld=--skip-grant-tables --mysqld=--loose-skip-pbxt --mysqld=--innodb --master_port=19300 --mysqld=--log-output=file 1st");
}

1;
