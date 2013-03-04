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

require Exporter;

package GenTest::Validator::Performance;

@ISA = qw(GenTest GenTest::Validator);

use strict;

use GenTest;
use GenTest::Constants;
use GenTest::Executor;
use GenTest::Result;
use GenTest::Validator;
use GenTest::Comparator;
use Data::Dumper;

my %counters;

sub init {
	my ($comparator, $executors) = @_;

	foreach my $executor (@$executors) {
		if (!$executor->read_only()) {
			say("Executor for dsn: ".$executor->dsn()." has been granted more than just SELECT privilege. Please restrict the user and try again");
			return STATUS_ENVIRONMENT_FAILURE;
		} else {
			$executor->setFlags($executor->flags() | EXECUTOR_FLAG_PERFORMANCE | EXECUTOR_FLAG_HASH_DATA );
		}
	}
	return STATUS_OK;
}


sub validate {
	my ($comparator, $executors, $results) = @_;

	die "Performance validator only works with two servers" if $#$results != 1;

	if ($results->[0]->query() !~ m{^\s*SELECT}sio) {
		$counters{'non-SELECT queries'}++;
		return STATUS_WONT_HANDLE;
	} elsif ($results->[0]->status() != $results->[1]->status()) {
		say("The two servers returned different status codes for query: ".$results->[0]->query());
		say("Server 1: status: ".$results->[0]->status()."; errstr: ".$results->[0]->errstr());
		say("Server 2: status: ".$results->[1]->status()."; errstr: ".$results->[1]->errstr());
		return STATUS_ERROR_MISMATCH;
	} elsif (GenTest::Comparator::compare($results->[0], $results->[1]) != STATUS_OK) {
		say("The two servers returned different result sets for query: ".$results->[0]->query());
		return STATUS_CONTENT_MISMATCH;
	}


	my @performances = ($results->[0]->performance(), $results->[1]->performance() );
	my @status_variables = ($performances[0]->sessionStatusVariables(), $performances[1]->sessionStatusVariables());

	my @variable_names = keys %{$status_variables[0]};

	my $notable = 0;

	say("====");

	foreach my $variable_name (@variable_names) {
		my @values = ($status_variables[0]->{$variable_name} , $status_variables[1]->{$variable_name});
		next if not defined $values[0] || not defined $values[1];
		next if $values[0] eq '' || $values[1] eq '';
		next if $values[0] eq $values[1];

		my $diff = abs($values[0] - $values[1]);
		next if $diff <= 2;

		my ($bigger_server, $smaller_server);
		if ($values[0] > $values[1]) {
			$bigger_server = 0;
			$smaller_server = 1;
		} else {
			$bigger_server = 1;
			$smaller_server = 0;
		}


#		if ($values[$smaller_server] eq '0') {
#			$notable++;
#			say ("Variable $variable_name is non-zero on $bigger_server: $values[$bigger_server]");
#		} else {
			my $increase = $values[$smaller_server] == 0 ? $values[$bigger_server] : ($values[$bigger_server] / $values[$smaller_server]);
			if ($increase > 10) {
				$notable++;
# = $notable + ( $bigger_server == 0 ? 1 : -1 );
				say ("Variable $variable_name is bigger on $bigger_server: $values[$bigger_server] vs. $values[$smaller_server]");
			}
#		}
	}

	if ($notable != 0) {
		say("^^^ ($notable) query was: ".$results->[0]->query());
	}

	return STATUS_OK;	
}

1;
