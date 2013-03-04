# Copyright (C) 2010 Sun Microsystems, Inc. All rights reserved.  Use
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

package FromGrammarTest;
use base qw(Test::Unit::TestCase);
use lib 'lib';
use GenTest::Generator::FromGrammar;

use Data::Dumper;

sub new {
    my $self = shift()->SUPER::new(@_);
    # your state for fixture here
    return $self;
}

my $generator;
sub set_up {
    $generator = GenTest::Generator::FromGrammar->
        new(grammar_file => "unit/testGrammar.yy");
}

sub tear_down {
    # clean up after test
}

sub test_create_generator {
    my $self = shift;
    
    $self->assert_not_null($generator);
}

sub test_generator_next {
    my $self = shift;

    my $x = $generator->next();
    $self->assert_equals(0, $#{$x});
    $self->assert_equals('b', $x->[0]);

    my $x = $generator->next();
    $self->assert_equals(0, $#{$x});
    $self->assert_equals('item2', $x->[0]);
}


1;
