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

use strict;
use lib 'lib';
use lib '../lib';
use DBI;

use GenTest;
use GenTest::Constants;
use GenTest::Simplifier::Grammar;
use GenTest::Generator::FromGrammar;

use Test::More tests => 1;

my $initial_grammar_file = 't/simplify-empty.yy';

open(INITIAL_GRAMMAR, $initial_grammar_file) or die $!;
read(INITIAL_GRAMMAR, my $initial_grammar_string , -s $initial_grammar_file);
close(INITIAL_GRAMMAR);

my $simplifier = GenTest::Simplifier::Grammar->new(
	oracle => sub {
		my $oracle_grammar_string = shift;

                my $tmpfile = tmpdir().time().'.yy';

                open (GRAMMAR, ">$tmpfile") or die "unable to create $tmpfile: $!";
                print GRAMMAR $oracle_grammar_string;
                my $rqg_status = system("perl gensql.pl --grammar=$tmpfile");
		$rqg_status = $rqg_status >> 8;

		unlink($tmpfile);

		if ($rqg_status == STATUS_ENVIRONMENT_FAILURE) {
			return 1;
		} else {
			return 0;
		}
	}
);

my $simplified_grammar_string = $simplifier->simplify($initial_grammar_string);
print "Simplified grammar:\n\n$simplified_grammar_string;\n\n";

ok(defined $simplified_grammar_string, "simplify-grammar");

