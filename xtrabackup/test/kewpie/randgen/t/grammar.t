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


use Data::Dumper;
use GenTest::Grammar;
use GenTest::Generator::FromGrammar;

my %grammars = (
	'1simplest'		=> '1 a A',
	'2numbers'		=> '1 11 111',
	'2quotes'		=> "\" ' `",
	'3single_char_backtick'	=> '`1` `A` `a`',
	'3single_char_quotes'	=> "'1' 'A' 'a'",
	'3single_char_doubleq'	=> '"1" "A" "a"',
	'4composite'		=> 'A 1A1 A1A a1a 1a1 `1A1` `A1A` `a1a` `1a1`',
	'5identifiers'		=> 'a.a a.a.a A.A A.A.A',
);

use Test::More tests => 29;

foreach my $grammar_name (sort keys %grammars) {

	# Test grammar before and after reconstruction

	my $reconstructed_grammar_obj = GenTest::Grammar->new(
		grammar_string => 'query: '.$grammars{$grammar_name}
	);

	my @grammar_variants = (
		'query: '.$grammars{$grammar_name},	# Initial grammar
		$reconstructed_grammar_obj->toString()	# Reconstructed grammar
	);

	foreach my $grammar_id (0..1) {	
		my $generator = GenTest::Generator::FromGrammar->new(
			grammar_string => $grammar_variants[$grammar_id]
		);
		my $output = $generator->next()->[0];
		ok($grammars{$grammar_name} eq $output, $grammar_name.'-'.$grammar_id);
	}
}

my @generator_vars;

$generator_vars[0] = GenTest::Generator::FromGrammar->new(
        grammar_string => 'query: _digit _varchar(1) _letter _varchar(255) _varchar'
);

$generator_vars[1] = GenTest::Generator::FromGrammar->new(
	grammar_string => $generator_vars[0]->grammar()->toString()
);

foreach my $generator_id (0..1) {
	my $output_vars = $generator_vars[$generator_id]->next()->[0];
	my @output_vars = split(' ', $output_vars);
	ok($output_vars[0] =~ m{[0-9]}, 'digit');
	ok($output_vars[1] =~ m{[a-z]}, 'varchar1');
	ok($output_vars[2] =~ m{[a-z]}, 'letter');
	ok($output_vars[3] =~ m{[a-z]+}, 'varchar1');
	ok($output_vars[4] =~ m{[a-z]+}, 'varchar2');
}

my @generator_perl;

$generator_perl[0] = GenTest::Generator::FromGrammar->new(
	grammar_string => 'query: {{ { $foo = 0; return $foo }} } A B C $foo'
);

$generator_perl[1] = GenTest::Generator::FromGrammar->new(
	grammar_string => $generator_perl[0]->grammar()->toString()
);

foreach my $perl_id (0..1) {
	my $output_perl = $generator_perl[$perl_id]->next()->[0];
	ok($output_perl eq '0 A B C 0', '6perl'.$perl_id);
}

my $grammar_compact = GenTest::Grammar->new(
		grammar_string	=> '
rule1:
	| a | b | c;

view_replace:
        # Only 20 %
         | | | | OR REPLACE ;

rule2:
	x | y | z ;
',
		grammar_flags	=> GRAMMAR_FLAG_COMPACT_RULES
);

my $elements = $grammar_compact->rule('view_replace')->components();
ok ($#$elements == 1, 'grammar_compact');
