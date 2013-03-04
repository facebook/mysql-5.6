# Copyright (C) 2009 Sun Microsystems, Inc. All rights reserved.  Use
# is subject to license terms.
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

package GrammarTest;
use base qw(Test::Unit::TestCase);
use lib 'lib';
use GenTest::Grammar;
use GenTest::Executor;
use GenTest::Executor::Dummy;
use GenTest::Generator::FromGrammar;

use Data::Dumper;
use Time::HiRes;

sub new {
    my $self = shift()->SUPER::new(@_);
    # your state for fixture here
    return $self;
}

my $grammar;
sub set_up {
    $grammar = GenTest::Grammar->new(grammar_file => "unit/testGrammar.yy");
}

sub tear_down {
    # clean up after test
}

sub test_create_grammar {
    my $self = shift;
    
    $self->assert_not_null($grammar);
    
    $self->assert_not_null($grammar->rule("item1"));
    $self->assert_null($grammar->rule("item2"));
}

sub test_patch_grammar {
    my $self = shift;

    my $newGrammar = GenTest::Grammar->new(grammar_string => "item1 : foobar ;\nitem2: barfoo ;\n");

    my $patchedGrammar = $grammar->patch($newGrammar);

    $self->assert_not_null($patchedGrammar);
    $self->assert_not_null($patchedGrammar->rule('item2'));


    my $item1= $patchedGrammar->rule('item1');
    $self->assert_not_null($item1);
    $self->assert_matches(qr/foobar/, $item1->toString());
}

sub test_top_grammar {
    my $self =shift;

    my $topGrammar = $grammar->topGrammar(1,'foobar','query');
    $self->assert_not_null($topGrammar);
    $self->assert_not_null($topGrammar->rule('query'));
    $self->assert_null($topGrammar->rule('itme1'));
}

sub test_mask_grammar {
    my $self =shift;

    my $maskedGrammar = $grammar->mask(0xaaaa);

    $self->assert_not_null($maskedGrammar);
    my $query = $maskedGrammar->rule('query');
    $self->assert_not_null($query);
    $self->assert_does_not_match(qr/item1/,$query->toString());

}

sub test_grammar_speed {
    my $self = shift;

    my $grammar = GenTest::Grammar->new(grammar_file => 'conf/examples/example.yy');

    my $generator = GenTest::Generator::FromGrammar->new(grammar => $grammar,
                                                         seed => 0);

    my $executor = GenTest::Executor->newFromDSN("dummy");
    $executor->init();
    $executor->cacheMetaData();

    my $start = Time::HiRes::time();
    foreach $i (0.. 10000) {
        $generator->next([$executor,undef]);
    }
    my $stop = Time::HiRes::time();
    open TM,">unit/grammar.dat";
    print TM "YVALUE = ".($stop - $start)."\n";
    close TM;

    
}

1;
