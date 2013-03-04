use strict;

$| = 1;

use lib 'lib';
use GenTest::SimPipe::Testcase;
use GenTest::SimPipe::Oracle::Crash;
use GenTest;
use GenTest::Constants;
use GenTest::Executor::MySQL;
use DBI;
use Data::Dumper;

my $test = 'case.test';
open (Q, $test) or die $!;

my $dsn = 'dbi:mysql:port=19300:user=root:host=127.0.0.1:database=test';
my $oracle = GenTest::SimPipe::Oracle::Crash->new( dsn => $dsn , basedir => '/home/philips/bzr/maria-5.3' );
$oracle->startServer();

my $dbh = DBI->connect($dsn, undef, undef, { mysql_multi_statements => 1, PrintError => 1 });
$dbh->do("DROP DATABASE IF EXISTS test; CREATE DATABASE test; USE test");

while (<Q>) {
	chomp;
	next if $_ eq '';
	$_ =~ s{SESSION}{GLOBAL}sgio;
	$_ =~ s{INSERT}{INSERT IGNORE}sgio;
	$dbh->do($_);
}

my $testcase = GenTest::SimPipe::Testcase->newFromDSN( $dsn, [ 
	"SELECT 1 FROM DUAL"
]);

my $new_testcase = $testcase->simplify($oracle);

print $new_testcase->toString();
