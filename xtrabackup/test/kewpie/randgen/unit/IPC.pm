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

# Do a simple run of scripts to see that they're sound
#
package IPC;
use base qw(Test::Unit::TestCase);
use lib 'lib';
use lib 'unit';

use Data::Dumper;

use GenTest::IPC::Channel;
use GenTest::IPC::Process;

use IPC_P1;

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

sub testChannel {
    my $self = shift;

    my $outgoing = GenTest::IPC::Channel->new();
    $self->assert_not_null($outgoing);
    
    my $incoming = GenTest::IPC::Channel->new();
    $self->assert_not_null($incoming);

    my $relay = IPC_P1->new($outgoing,$incoming);
    $self->assert_not_null($relay);

    my $relay_p = GenTest::IPC::Process->new(object=>$relay);
    $self->assert_not_null($relay_p);

    if (fork()) {
	$relay_p->start();
    } else {
	$incoming->close();
	$outgoing->writer();
	$outgoing->send(["foo","bar"]);
	$outgoing->close();
	exit 0;
    }

    $outgoing->writer;
    $incoming->reader;

    $message = ['foo','bar'];

    $outgoing->send($message);
    $outgoing->send($message);

    $outgoing->close;
    
    my $n=0;
    while ($incoming->more) {
        my $in_message = $incoming->recv;
        $self->assert_not_null($in_message);
        $self->assert_num_equals(1,$#{$in_message});
        $self->assert_str_equals($message->[0],$in_message->[0]);
        $self->assert_str_equals($message->[1],$in_message->[1]);
	$n++;
    }

    $self->assert_num_equals(3,$n);

    $incoming->close;
}


1;
