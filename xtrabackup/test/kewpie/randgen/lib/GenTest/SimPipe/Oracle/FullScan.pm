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

package GenTest::SimPipe::Oracle::FullScan;

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

my %option_defaults = (
	'optimizer_use_mrr'             => 'disable',
	'mrr_buffer_size'               => 262144,
	'join_cache_level'              => 0,
	'join_buffer_size'              => 131072,
	'join_buffer_space_limit'       => 1048576,
	'rowid_merge_buff_size'         => 8388608,
	'storage_engine'                => 'MyISAM',
	'optimizer_switch'              => 'index_merge=off,index_merge_union=off,index_merge_sort_union=off,index_merge_intersection=off,index_merge_sort_intersection=off,table_elimination=off,in_to_exists=off'
);


sub oracle {
	my ($oracle, $testcase) = @_;

	my $executor = GenTest::Executor->newFromDSN($oracle->dsn());
	$executor->init();
	
	my $dbh = $executor->dbh();

	foreach my $option_name (keys %option_defaults) {
		if ($option_defaults{$option_name} =~ m{^\d+$}sio) {
			$dbh->do("SET SESSION $option_name = ".$option_defaults{$option_name});
		} else {
			$dbh->do("SET SESSION $option_name = '".$option_defaults{$option_name}."'");
		}
	}

	my $testcase_string = join("\n", (
		"DROP DATABASE IF EXISTS fullscan$$;",
		"CREATE DATABASE IF NOT EXISTS fullscan$$;",
		"USE fullscan$$;",
		$testcase->mysqldOptionsToString(),
		$testcase->dbObjectsToString()
        ));

	if ($#{$testcase->queries()} > 0) {
		$testcase_string .= "\n".join(";\n", @{$testcase->queries()}[0..$#{$testcase->queries()}-1]).";\n";
	}

	open (LD, '>/tmp/last_dump.test');
	print LD $testcase_string;
	close LD;

	$dbh->do($testcase_string, { RaiseError => 1 , mysql_multi_statements => 1 });

	my $original_query = $testcase->queries()->[$#{$testcase->queries()}];
	my $original_result = $executor->execute($original_query);

#	print Dumper $original_result;
	my $original_explain = $executor->execute("EXPLAIN ".$original_query);
#	print Dumper $original_explain;

        $testcase_string .= "\n$original_query;\n";

	my @table_names = @{$dbh->selectcol_arrayref("SHOW TABLES")};
	foreach my $table_name (@table_names) {
		$dbh->do("ALTER TABLE $table_name DISABLE KEYS");
	}

	$dbh->do("SET SESSION join_cache_level = 0");
	$dbh->do("SET SESSION optimizer_use_mrr = 'disable'");
	$dbh->do("SET SESSION optimizer_switch='".$option_defaults{'optimizer_switch'}."'");

	my $no_hints_query = $original_query;
	$no_hints_query =~ s{(FORCE|IGNORE|USE)\s+KEY\s*\(.*?\)}{}sio;

	my $fullscan_result = $executor->execute($no_hints_query);

#	$dbh->do("DROP DATABASE fullscan$$");

        my $compare_outcome = GenTest::Comparator::compare($original_result, $fullscan_result);
	print "Compare outcome is: ".$compare_outcome."\n";

	if (
		($original_result->status() != STATUS_OK) ||
		($fullscan_result->status() != STATUS_OK) ||
		($compare_outcome == STATUS_OK)
	) {
		open (LR, '>/tmp/last_not_repeatable.test');
		print LR $testcase_string;
		close LR;
		return ORACLE_ISSUE_NO_LONGER_REPEATABLE;
	} else {
#		print Dumper $original_result;
#		print Dumper $fullscan_result;
		open (LR, '>/tmp/last_repeatable.test');
		print LR $testcase_string;
		close LR;
		return ORACLE_ISSUE_STILL_REPEATABLE;
	}	
}

1;
