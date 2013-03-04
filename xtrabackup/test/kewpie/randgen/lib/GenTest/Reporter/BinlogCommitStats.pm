package GenTest::Reporter::BinlogCommitStats;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;

sub report {
	my $reporter = shift;

	my $dbh = DBI->connect($reporter->dsn(), undef, undef, {PrintError => 0});

	return STATUS_WONT_HANDLE if not defined $dbh;

	my $stats = $dbh->selectcol_arrayref("
		SELECT VARIABLE_VALUE
		FROM INFORMATION_SCHEMA.GLOBAL_STATUS
		WHERE VARIABLE_NAME IN ('binlog_commits','binlog_group_commits')
		ORDER BY VARIABLE_NAME
	");

	my ($binlog_commits, $binlog_group_commits) = ($stats->[0], $stats->[1]);

	say("binlog_commits: $binlog_commits; binlog_group_commits: $binlog_group_commits; diff: ".($binlog_commits - $binlog_group_commits));
	return STATUS_OK;
}

sub type {
	return REPORTER_TYPE_ALWAYS;
}

1;
