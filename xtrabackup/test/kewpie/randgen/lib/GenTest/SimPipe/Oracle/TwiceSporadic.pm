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

package GenTest::SimPipe::Oracle::TwiceSporadic;

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

1;

sub oracle {
	my ($oracle, $testcase) = @_;

	my $executor = GenTest::Executor->newFromDSN($oracle->dsn());
	$executor->init();
	
	my $dbh = $executor->dbh();

	my $testcase_string = join("\n", (
		"CREATE DATABASE IF NOT EXISTS sporadic$$;",
		"USE sporadic$$;",
		$testcase->mysqldOptionsToString(),
		$testcase->dbObjectsToString()
        ));

	open (LD, '>/tmp/last_dump.test');
	print LD $testcase_string;
	close LD;

	$dbh->do($testcase_string, { RaiseError => 1 , mysql_multi_statements => 1 });

	$testcase_string .= "\n".join(";\n", @{$testcase->queries()}).";\n";

	$executor->execute($testcase->queries()->[0]);

	my @results = (
		$executor->execute($testcase->queries()->[1]."/* try 1 */"),	
		$executor->execute($testcase->queries()->[1]."/* try 2 */")
	);

	use Data::Dumper;
	print Dumper \@results;
	
        my $compare_outcome = GenTest::Comparator::compare(@results);

	$dbh->do("DROP DATABASE sporadic$$");
	
	if ($compare_outcome == STATUS_OK) {
		open (LR, '>/tmp/last_not_repeatable.test');
		print LR $testcase_string;
		close LR;
		return ORACLE_ISSUE_NO_LONGER_REPEATABLE;
	} else {
		open (LR, '>/tmp/last_repeatable.test');
		print LR $testcase_string;
		close LR;
		return ORACLE_ISSUE_STILL_REPEATABLE;
	}	
}

1;
