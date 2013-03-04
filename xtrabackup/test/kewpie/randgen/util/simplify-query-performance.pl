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

use warnings;
use strict;
use DBI;
use Time::HiRes;
use lib 'lib';
use lib '../lib';

$| = 1;

use GenTest::Constants;
use GenTest::Executor::MySQL;
use GenTest::Simplifier::Test;
use GenTest::Simplifier::SQL;
use GenTest::Comparator;

#
# This script demonstrates the simplification of queries. More information is available at
#
# http://forge.mysql.com/wiki/RandomQueryGeneratorSimplification
#

my $query = "SELECT 1 FROM DUAL";

my @dsns = (
	'dbi:mysql:host=127.0.0.1:port=19300:user=root:database=test',
	'dbi:mysql:host=127.0.0.1:port=19302:user=root:database=test'
);

my $maximum_query_duration = 2;
my $performance_threshold = 1.25;

# End of user-editable part

my @executors;
my @connection_ids;

foreach my $dsn (@dsns) {
	my $executor = GenTest::Executor::MySQL->new( dsn => $dsn );
	my $init_status = $executor->init();
	exit ($init_status) if $init_status != STATUS_OK;
	push @connection_ids, $executor->dbh()->selectrow_array("SELECT CONNECTION_ID()");
	$executor->dbh()->do("SET GLOBAL EVENT_SCHEDULER=ON");
	push @executors, $executor;
}


my $simplifier = GenTest::Simplifier::SQL->new(
	oracle => sub {
		my $oracle_query = shift;
		print ".";

		my $outcome;
		my @oracle_results;
		my @oracle_times;

		foreach my $executor_id (0..$#executors) {
			my $executor = $executors[$executor_id];
			$executor->dbh()->do("CREATE EVENT timeout ON SCHEDULE AT CURRENT_TIMESTAMP + INTERVAL $maximum_query_duration SECOND DO KILL QUERY ".$connection_ids[$executor_id]);
			my $time_start = Time::HiRes::time();
			my $oracle_result = $executor->execute($oracle_query);
			my $duration = (Time::HiRes::time() - $time_start);
			$executor->dbh()->do("DROP EVENT IF EXISTS timeout");
			return ORACLE_ISSUE_NO_LONGER_REPEATABLE if $oracle_result->status() > STATUS_OK;
			push @oracle_times, $duration;
			push @oracle_results, $oracle_result;
		}
		
		my $best_time = $oracle_times[0] > $oracle_times[1] ? $oracle_times[0] : $oracle_times[1];
		my $worst_time = $oracle_times[0] < $oracle_times[1] ? $oracle_times[0] : $oracle_times[1];

		if (
			($best_time / $worst_time >= $performance_threshold) &&
			($worst_time > 1)
		) {
			print "Repeatable with: $oracle_query\n\n";
			return ORACLE_ISSUE_STILL_REPEATABLE;
		} else {
			return ORACLE_ISSUE_NO_LONGER_REPEATABLE;
		}
	}
);

my $simplified_query = $simplifier->simplify($query);

print "\nSimplified query:\n$simplified_query ;\n\n";

my @simplified_results;

foreach my $executor (@executors) {
        my $simplified_result = $executor->execute($simplified_query);
        push @simplified_results, $simplified_result;
}


my $simplifier_test = GenTest::Simplifier::Test->new(
        executors => \@executors,
        queries => [ $simplified_query, $query ],
        results => [ \@simplified_results ]
);

my $test = $simplifier_test->simplify();

print "Simplified test:\n\n";
print $test;
