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

# Basic grammar test
# Walk through all the grammars and feed them to the Grammar
# constructor. 
#
package ExecutorTest;
use base qw(Test::Unit::TestCase);
use lib 'lib';
use GenTest::Constants;
use GenTest::Executor;

use Data::Dumper;

sub new {
    my $self = shift()->SUPER::new(@_);
    # your state for fixture here
    return $self;
}

my $executor;
sub set_up {
    $executor = GenTest::Executor->newFromDSN("dummy");
}

sub tear_down {
    # clean up after test
}

sub test_create {
    my $self = shift;
    
    $self->assert_not_null($executor);
}

sub test_functions {
    my $self = shift;

    my $type = $executor->type();

    $self->assert_equals(DB_DUMMY, $type);

    my $name = $executor->getName();

    $self->assert_equals("Dummy", $name);
    
}
1;
