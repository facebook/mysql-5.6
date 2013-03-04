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

package GenTest::Transform::DisableJoinCache;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';
use GenTest;
use GenTest::Transform;
use GenTest::Constants;
use Data::Dumper;

my @explain2switch = (
	[ 'join buffer'		=> "join_cache_level=0" ]
);

my $available_switches;

sub transform {
	my ($class, $original_query, $executor) = @_;

	if (not defined $available_switches) {
		my $optimizer_switches = $executor->dbh()->selectrow_array('SELECT @@optimizer_switch');
		my @optimizer_switches = split(',', $optimizer_switches);
		foreach my $optimizer_switch (@optimizer_switches) {
			my ($switch_name, $switch_value) = split('=', $optimizer_switch);
			$available_switches->{"optimizer_switch='$switch_name=off'"}++;
		}

		if ($executor->dbh()->selectrow_array('SELECT @@optimizer_use_mrr')) {
			$available_switches->{"optimizer_use_mrr='disable'"}++;
		}

		if ($executor->dbh()->selectrow_array('SELECT @@join_cache_level')) {
			$available_switches->{"join_cache_level=0"}++;
		}

		if ($executor->dbh()->selectrow_array('SELECT @@optimizer_join_cache_level')) {
			$available_switches->{"optimizer_join_cache_level=0"}++;
		}
	}

	return STATUS_WONT_HANDLE if $original_query !~ m{^\s*SELECT}sio;

	my $original_explain = $executor->execute("EXPLAIN EXTENDED $original_query");

	if ($original_explain->status() == STATUS_SERVER_CRASHED) {
		return STATUS_SERVER_CRASHED;
	} elsif ($original_explain->status() ne STATUS_OK) {
		return STATUS_ENVIRONMENT_FAILURE;
	}

	my $original_explain_string = Dumper($original_explain->data())."\n".Dumper($original_explain->warnings());

	my @transformed_queries;
	foreach my $explain2switch (@explain2switch) {
		my ($explain_fragment, $optimizer_switch) = ($explain2switch->[0], $explain2switch->[1]);
		next if not exists $available_switches->{$optimizer_switch};
		if ($original_explain_string =~ m{$explain_fragment}si) {
			my ($switch_name) = $optimizer_switch =~ m{^(.*?)=}sgio;
			push @transformed_queries, (
				'SET @switch_saved = @@'.$switch_name.';',
				"SET SESSION $optimizer_switch;",
				"$original_query /* TRANSFORM_OUTCOME_UNORDERED_MATCH */ ;",
				'SET SESSION '.$switch_name.'=@switch_saved'
			);
			last;
		}
	}

	if ($#transformed_queries > -1) {
		return \@transformed_queries;
	} else {
		return STATUS_WONT_HANDLE;
	}
}

1;
