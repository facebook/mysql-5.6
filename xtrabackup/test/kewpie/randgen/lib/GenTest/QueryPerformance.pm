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

package GenTest::QueryPerformance;

require Exporter;
@ISA = qw(GenTest);

use strict;
use GenTest;

use constant PERFORMANCE_DBH		=> 0;
use constant PERFORMANCE_VERSION	=> 1;
use constant PERFORMANCE_QUERY		=> 2;
use constant PERFORMANCE_SESSION_STATUS	=> 3;
use constant PERFORMANCE_EXECUTION_TIME	=> 4;

1;

my %innodb_baseline;

sub new {
	my $class = shift;
	my $performance = $class->SUPER::new({
		'dbh'			=> PERFORMANCE_DBH,
		'version'		=> PERFORMANCE_VERSION,
		'query'			=> PERFORMANCE_QUERY,
		'execution_time'	=> PERFORMANCE_EXECUTION_TIME
	}, @_);

	$performance->dbh()->do("FLUSH STATUS");
	%innodb_baseline = @{$performance->dbh()->selectcol_arrayref("SHOW GLOBAL STATUS LIKE 'Innodb_%'",  { Columns=>[1,2] })};
	$performance->[PERFORMANCE_VERSION] = $performance->dbh()->selectrow_array('SELECT @@version');

	return $performance;
}

sub record {
	my $performance = shift;

	my %status_hash = @{$performance->dbh()->selectcol_arrayref("SHOW SESSION STATUS",  { Columns=>[1,2] })};

	foreach my $variable_name (keys %status_hash) {
		delete $status_hash{$variable_name} if $variable_name =~ m{^(com_|qcache_|ssl_)}sgio;
		delete $status_hash{$variable_name} if $variable_name =~ m{^(threads_created|uptime|opened_files|queries|connections)}sgio;
		delete $status_hash{$variable_name} if $variable_name =~ m{^(aria_|innodb_)}sgio;

		delete $status_hash{$variable_name} if $variable_name !~ m{^handler}sgio;
	
#		delete $status_hash{$variable_name} if $status_hash{$variable_name} eq '0';
	}

	my %innodb_hash = @{$performance->dbh()->selectcol_arrayref("SHOW GLOBAL STATUS LIKE 'Innodb_%'",  { Columns=>[1,2] })};

	foreach my $variable_name (keys %innodb_hash) {
		$status_hash{$variable_name} = $innodb_hash{$variable_name} - $innodb_baseline{$variable_name};
	}

	$performance->[PERFORMANCE_SESSION_STATUS] = \%status_hash;
}

sub query {
	return $_[0]->[PERFORMANCE_QUERY];
}

sub sessionStatusVariable {
	return $_[0]->[PERFORMANCE_SESSION_STATUS]->{$_[1]};
}

sub sessionStatusVariables {
	return $_[0]->[PERFORMANCE_SESSION_STATUS];
}

sub executionTime {
	return $_[0]->[PERFORMANCE_EXECUTION_TIME];
}

sub setExecutionTime {
	$_[0]->[PERFORMANCE_EXECUTION_TIME] = $_[1];
}

sub dbh {
	return $_[0]->[PERFORMANCE_DBH];
}

sub version {
	return $_[0]->[PERFORMANCE_VERSION];
}

1;
