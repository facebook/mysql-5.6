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

package GenTest::Reporter::ValgrindXMLErrors;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use GenTest;
use GenTest::Reporter;
use GenTest::Constants;

sub report {

	my $reporter = shift;

	my $valgrind_xml_log = $reporter->serverVariable('datadir')."../log/valgrind.xml";

	open (XML , $valgrind_xml_log);
	read (XML, my $xml , -s $valgrind_xml_log);

	my @errors = $xml =~ m{<error>(.*?)</error>}sgio;
	my $valgrind_failure = 0;

	foreach my $error (@errors) {
		# We discard all failures that are mostly in InnoDB
		# This allows ordinary debug builds that are compiled with valgrind suppressions to run RQG --valgrind-xml
		my $innodb_mentions =~ m{(innodb|xtradb|plugin)}siog;
		next if $innodb_mentions > 3;

		my ($what) = $error =~ m{<what>(.*?)</what>}sio;
		my ($top_file) = $error =~ m{<file>(.*?)</file>}sio;
		my ($top_fn) = $error =~ m{<fn>(.*?)</fn>}sio;
		my ($top_line) = $error =~ m{<line>(.*?)</line>}sio;
		say("Valgrind: $what at $top_file:$top_line, function '$top_fn' . See log/valgrind.xml for further details.");
		$valgrind_failure = 1;
	}

	if ($valgrind_failure) {
		return STATUS_VALGRIND_FAILURE;
	} else {
		return STATUS_OK;
	}
}

sub type {
	return REPORTER_TYPE_ALWAYS ;
}

1;
