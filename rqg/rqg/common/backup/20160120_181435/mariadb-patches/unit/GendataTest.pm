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
package GendataTest;
use base qw(Test::Unit::TestCase);
use lib 'lib';
use GenTest::Constants;
use GenTest::App::Gendata;
use GenTest::App::GendataSimple;
use Time::HiRes;

use Data::Dumper;

sub new {
    my $self = shift()->SUPER::new(@_);
    # your state for fixture here
    return $self;
}

my $generator;
sub set_up {
}

sub tear_down {
    # clean up after test
}

sub test_simple {
    my $self = shift;
    
    my $gen = GenTest::App::GendataSimple->new(dsn => "dummy");

    my $start = Time::HiRes::time();
    foreach my $i (1..10) {
        my $status = $gen->run();
        $self->assert_equals(STATUS_OK, $status);
    }
    my $stop = Time::HiRes::time();

    open TM,">unit/gendata1.dat";
    print TM "YVALUE = ".($stop - $start)."\n";
    close TM;
}

sub test_advanced {
    my $self = shift;

    my $gen = GenTest::App::Gendata->new(dsn => "dummy",
                                         spec_file => "unit/GendataTest.zz",
                                         rows => 10000,
                                         views => 1);

    
    my $start = Time::HiRes::time();
    
    foreach my $i (1..5) {
        my $status = $gen->run();
        $self->assert_equals(STATUS_OK, $status);
    }
    my $stop = Time::HiRes::time();

    open TM,">unit/gendata2.dat";
    print TM "YVALUE = ".($stop - $start)."\n";
    close TM;
}

1;
