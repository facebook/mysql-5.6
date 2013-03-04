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

package GenTest::Transform::ChangePartialMatch;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';
use GenTest;
use GenTest::Transform;
use GenTest::Constants;
use Data::Dumper;

#
# This transform runs different materialization strategies on each query. Since partial matching is not reflected in the 
# EXPLAIN, we can not use the DisableChosenPlan to selectively re-execute only egligible queries. Instead, we filter out
# the queries that did not use materialization and then apply all partial matching strategies on all remaining queries.
#

sub transform {
	my ($class, $original_query, $executor) = @_;

	return STATUS_WONT_HANDLE if $original_query !~ m{^\s*SELECT}sio;
#	my $original_explain = $executor->execute("EXPLAIN EXTENDED $original_query");
#
#	if ($original_explain->status() == STATUS_SERVER_CRASHED) {
#		return STATUS_SERVER_CRASHED;
#	} elsif ($original_explain->status() ne STATUS_OK) {
#		return STATUS_ENVIRONMENT_FAILURE;
#	}
#
#	my $original_explain_string = Dumper($original_explain->data())."\n".Dumper($original_explain->warnings());
	my $original_optimizer_switch = $executor->dbh()->selectrow_array('SELECT @@optimizer_switch');

#	return STATUS_WONT_HANDLE if $original_explain_string !~ m{material}sgio;

	return [
		[
			"SET SESSION optimizer_switch='semijoin=off,in_to_exists=off,materialization=on,partial_match_rowid_merge=on,partial_match_table_scan=on';",
			"$original_query /* TRANSFORM_OUTCOME_UNORDERED_MATCH */ ;",
			"SET SESSION optimizer_switch='$original_optimizer_switch'"
		], [
			"SET SESSION optimizer_switch='semijoin=off,in_to_exists=off,materialization=on,partial_match_rowid_merge=on,partial_match_table_scan=off';",
			"$original_query /* TRANSFORM_OUTCOME_UNORDERED_MATCH */ ;",
			"SET SESSION optimizer_switch='$original_optimizer_switch'"
		], [
			"SET SESSION optimizer_switch='semijoin=off,in_to_exists=off,materialization=on,partial_match_rowid_merge=off,partial_match_table_scan=on';",
			"$original_query /* TRANSFORM_OUTCOME_UNORDERED_MATCH */ ;",
			"SET SESSION optimizer_switch='$original_optimizer_switch'"
		], [
			"SET SESSION optimizer_switch='semijoin=off,in_to_exists=off,materialization=on,partial_match_rowid_merge=off,partial_match_table_scan=off';",
			"$original_query /* TRANSFORM_OUTCOME_UNORDERED_MATCH */ ;",
			"SET SESSION optimizer_switch='$original_optimizer_switch'"
		]
	];
}

1;
