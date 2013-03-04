use strict;

$| = 1;

use lib 'lib';
use GenTest::SimPipe::Testcase;
use GenTest::SimPipe::Oracle::FullScan;
use GenTest;
use GenTest::Constants;
use GenTest::Executor::MySQL;
use DBI;
use Data::Dumper;

my $dsn = 'dbi:mysql:port=9306:user=root:host=127.0.0.1:database=test';
my $oracle = GenTest::SimPipe::Oracle::FullScan->new(
	dsn => $dsn,
	basedir => '/home/philips/bzr/maria-5.3'
);

my $dbh = DBI->connect($dsn, undef, undef, { mysql_multi_statements => 1, RaiseError => 1 });

my $query = " SELECT 1 FROM DUAL ";

$dbh->do("USE test");
$dbh->do("SET SQL_MODE='NO_ENGINE_SUBSTITUTION'");

my $test = 'case.test';
open (Q, $test) or die $!;
read (Q, my $sql, -s $test);
$test =~ s{LOCAL}{GLOBAL}sgio;

$dbh->do($sql);
my $testcase = GenTest::SimPipe::Testcase->newFromDBH($dbh, [ $query ]);

my $new_testcase = $testcase->simplify($oracle);

print $new_testcase->toString();
