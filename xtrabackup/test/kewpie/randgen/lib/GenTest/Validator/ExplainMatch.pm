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

package GenTest::Validator::ExplainMatch;

require Exporter;
@ISA = qw(GenTest::Validator GenTest);

use strict;

use GenTest;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;
use GenTest::Simplifier::Test;
use Data::Dumper;

my $match_string = 'unique row not found';

1;

sub validate {
        my ($validator, $executors, $results) = @_;

	my $executor = $executors->[0];
	my $query = $results->[0]->query();

	return STATUS_WONT_HANDLE if $query !~ m{^\s*SELECT}sio;

        my $explain_output = $executor->dbh()->selectall_arrayref("EXPLAIN $query");

	my $explain_string = Dumper $explain_output;

	if ($explain_string =~ m{$match_string}sio) {
		say("EXPLAIN $query matches $match_string");
		my $simplifier_test = GenTest::Simplifier::Test->new(
		        executors => [ $executor ],
		        queries => [ $query , "EXPLAIN $query" ]
		);
		my $simplified_test = $simplifier_test->simplify();
		say("Simplified test:");
		print $simplified_test;
		return STATUS_CUSTOM_OUTCOME;
	} else {
		return STATUS_OK;
	}
}

1;
