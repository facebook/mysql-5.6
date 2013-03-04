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
my $oracle = GenTest::SimPipe::Oracle::FullScan->new( dsn => $dsn , basedir => '/home/philips/bzr/maria-5.3' );

my $dbh = DBI->connect($dsn, undef, undef, { mysql_multi_statements => 1, RaiseError => 1 });

my $query = "
SELECT alias2.col_datetime_key AS field1 , alias2.pk AS field2
FROM t1 AS alias1 JOIN t1 AS alias2 ON alias2.pk = alias1.col_int_key AND alias2.pk != alias1.col_varchar_key
GROUP BY field1 , field2
ORDER BY alias1.col_int_key , field2;";

$dbh->do("USE test");

my $test = '/home/philips/bzr/randgen-simpipe/case.test';
open (Q, $test) or die $!;
read (Q, my $sql, -s $test);

$dbh->do($sql);
my $testcase = GenTest::SimPipe::Testcase->newFromDBH($dbh, [ $query ]);

my $new_testcase = $testcase->simplify($oracle);

print $new_testcase->toString();
