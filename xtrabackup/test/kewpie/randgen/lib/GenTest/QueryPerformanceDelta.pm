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

package GenTest::QueryPerformanceDelta;

require Exporter;
@ISA = qw(GenTest);

use strict;
use GenTest;
use GenTest::Constants;

use Data::Dumper;

use constant DELTA_QUERY		=> 0;
use constant DELTA_TEMPERATURE		=> 1;
use constant DELTA_PERFORMANCES		=> 2;
use constant DELTA_SESSION_STATUS	=> 3;
use constant DELTA_EXECUTION_TIME	=> 4;

use constant EXECUTION_TIME_PRECISION	=> 3;

1;

sub new {
	my $class = shift;
	my $delta = $class->SUPER::new({
		'query'		=> DELTA_QUERY,
		'temperature'	=> DELTA_TEMPERATURE,
		'performances'	=> DELTA_PERFORMANCES
	}, @_);

	my $performances = $delta->[DELTA_PERFORMANCES];

	foreach my $variable_name (keys %{$performances->[0]->sessionStatusVariables()}) {
		my @values = (
			$performances->[0]->sessionStatusVariable($variable_name),
			$performances->[1]->sessionStatusVariable($variable_name)
		);

		next if not defined $values[0] || not defined $values[1];

		my ($delta_absolute, $delta_ratio);

		if (($values[0] == 0) && ($values[1] == 0)) {
			$delta_absolute = 0;
			$delta_ratio = 1;
		} elsif ($values[0] == 0) {
			$delta_absolute = $values[1];
			$delta_ratio = undef;
		} elsif ($values[1] == 0) {
			$delta_absolute = - $values[0];
			$delta_ratio = undef;
		} else {
			$delta_absolute = $values[1] - $values[0];
			$delta_ratio = $values[0] != 0 ? ($values[1] / $values[0]) : undef;
		}

		$delta->[DELTA_SESSION_STATUS]->{$variable_name} = [ $delta_absolute , $delta_ratio ];
	}

	my @execution_times = (
		sprintf('%.'.EXECUTION_TIME_PRECISION.'f', $performances->[0]->executionTime()),
		sprintf('%.'.EXECUTION_TIME_PRECISION.'f', $performances->[1]->executionTime()),
	);

	my $execution_time_delta = $execution_times[1] - $execution_times[0];
	my $execution_time_ratio = $execution_times[0] != 0 ? ($execution_times[1] / $execution_times[0]) : undef;
	$delta->[DELTA_EXECUTION_TIME] = [ $execution_time_delta , $execution_time_ratio ];

	return $delta;
}

sub sessionStatusVariable {
        return $_[0]->[DELTA_SESSION_STATUS]->{$_[1]};
}

sub sessionStatusVariables {
        return $_[0]->[DELTA_SESSION_STATUS];
}

sub query {
	return $_[0]->[DELTA_QUERY];
}

sub performances {
	return $_[0]->[DELTA_PERFORMANCES];
}

sub executionTime {
	return $_[0]->[DELTA_EXECUTION_TIME];
}

sub temperature {
	return $_[0]->[DELTA_TEMPERATURE];
}

sub matchesFilter {
	my ($delta, $filter) = @_;

	return STATUS_OK if not defined $filter;

	my $perl_script = "{ use strict;\nuse warnings;\n";

	my $performances = $delta->performances();

	# Prepare environment

	foreach my $performance_id (0..1) {
		my $session_status_variables = $performances->[$performance_id]->sessionStatusVariables();
  		$perl_script .= join("\n", map { "my \$$_".($performance_id+1)." = ".$session_status_variables->{$_}.";" } keys %{$session_status_variables});
	}

	my $session_status_variables = $delta->sessionStatusVariables();
	
	$perl_script .= join("\n", map {
		"my \$$_"."_delta = ".$session_status_variables->{$_}->[0].";\n".
		"my \$$_"."_ratio = ".(defined $session_status_variables->{$_}->[1] ? $session_status_variables->{$_}->[1] : 'undef').";"
	} keys %{$session_status_variables});

	$perl_script .= join("\n",
		"my \$Execution_time1 = ".$performances->[0]->executionTime().";",
		"my \$Execution_time2 = ".$performances->[1]->executionTime().";",
		"my \$Execution_time_delta = ". $delta->[DELTA_EXECUTION_TIME]->[0].";",
		"my \$Execution_time_ratio = ".(defined $delta->[DELTA_EXECUTION_TIME]->[1] ? $delta->[DELTA_EXECUTION_TIME]->[1] : 'undef').";"
	);

	my $query = $performances->[0]->query();
	$query =~ s{'}{\\'}sgio;
	$perl_script .= "my \$Query = '".$query."';\n";

	$perl_script .= "my \$Temperature = '".$delta->temperature()."';\n";
	$perl_script .= "\nif (".$filter.") { return STATUS_OK; } else { return STATUS_SKIP; } };\n";

	my $outcome = eval($perl_script);
	die "Error in filter expression: $@" if $@ ne '';

	return $outcome;
}

sub toString {
	my $delta = shift;
	my $buffer;

	my $performances = $delta->performances();
	my $query = $delta->query();
	my @status_variable_names = keys %{$performances->[0]->sessionStatusVariables()};

	my $header_format = '
Query: @*
Cache: @<<<<
                                    @<<<<<<<<<<<<<     @<<<<<<<<<<<<<          Delta              Ratio
-------------------------------------------------------------------------------------------------------';

	$buffer .= swrite($header_format, $query, $delta->temperature(), $performances->[0]->version(), $performances->[1]->version());

my $row_format_time = '
@<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<    @########.###s    @########.###s    @########.###s    ^########.##';
my $row_format_int = '
@<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<    @########         @########         @########         ^########.##';

	$buffer .= swrite($row_format_time,
		'Execution time',
		$performances->[0]->executionTime(),
		$performances->[1]->executionTime(),
		$delta->executionTime()->[0],
		$delta->executionTime()->[1]
	);

	foreach my $status_variable_name (@status_variable_names) {
		my ($val1, $val2) = (
			$performances->[0]->sessionStatusVariable($status_variable_name),
			$performances->[1]->sessionStatusVariable($status_variable_name)
		);

		next if not defined $val1 || not defined $val2;
		next if $val1 eq '' | $val2 eq '';
		next if $val1 eq $val2;

		$buffer .= swrite($row_format_int,
			$status_variable_name, $val1, $val2,
			$delta->sessionStatusVariable($status_variable_name)->[0],
			$delta->sessionStatusVariable($status_variable_name)->[1]
		);
	}

	$buffer .= "\n";
	return $buffer;
}

sub serialize {
	my $buffer = '<![CDATA[ ';;
	my $old_ident = $Data::Dumper::Indent;
	$Data::Dumper::Indent = 0;
	$buffer .=  Dumper($_[0]);
	$Data::Dumper::Indent = $old_ident;
	return $buffer." ]]>\n";
}

sub swrite {
	my $format = shift;
	$^A = "";
	formline($format,@_);
	my $output = $^A;
	$^A = "";
	return $output;
}

1;
