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

my $initial_grammar_file = 't/simplify-grammar.yy';

open(INITIAL_GRAMMAR, $initial_grammar_file) or die $!;
read(INITIAL_GRAMMAR, my $initial_grammar_string , -s $initial_grammar_file);
close(INITIAL_GRAMMAR);

my $simplifier = GenTest::Simplifier::Grammar->new(
	oracle => sub {
		my $oracle_grammar_string = shift;
		my $generator = GenTest::Generator::FromGrammar->new(
			grammar_string => $oracle_grammar_string
		);

		foreach my $queries (1..1000) {
			my $query = $generator->next();
			return 0 if not defined $query;
			my $sql = join('; ', @$query);
			return 0 if $sql eq '';
			if (
				($sql =~ m{select}sio) &&
				($sql =~ m{where}sio) &&
				($sql =~ m{C1|C2}sio) &&
				($sql =~ m{S3}sio) &&
				($sql =~ m{order by}sio) &&
				($sql =~ m{group by}sio) && 
				($sql !~ m{limit}sio) &&
				($sql =~ m{AA|BB}) &&
				($sql =~ m{F1})
			) {
				say("Query $sql matches.");
				return 1;
			}
		}
		say("No queries matched.");
		return 0;
	}
);

my $simplified_grammar_string = $simplifier->simplify($initial_grammar_string);
print "Simplified grammar:\n\n$simplified_grammar_string;\n\n";

ok(defined $simplified_grammar_string, "simplify-grammar");

