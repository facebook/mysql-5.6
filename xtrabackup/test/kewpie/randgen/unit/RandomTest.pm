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

package RandomTest;
use base qw(Test::Unit::TestCase);
use lib 'lib';
use Data::Dumper;
use GenTest::Random;

sub new {
    my $self = shift()->SUPER::new(@_);
    # your state for fixture here
    return $self;
}

sub set_up {
    # provide fixture
}

sub tear_down {
    # clean up after test
}

sub test_create_prng {
    my $self = shift;
    
    my $obj = GenTest::Random->new(seed => 1);
    
    $self->assert_not_null($obj);
    
}

sub test_shuffle_array {
    my $self = shift;
    
    my $obj = GenTest::Random->new(seed => 2);
    $self->assert_not_null($obj);
    my $input = [1,2,3,4,5,6,7,8,9,10];
    my $output = [7,9,4,1,2,3,6,10,5,8];
    $obj->shuffleArray($input);
    $self->assert_deep_equals($input,$output);
}

1;
