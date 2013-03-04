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

package IPC_P1;

use Data::Dumper;
use GenTest::IPC::Channel;

sub new {
    my $class = shift;
    my $self = {};

    $self->{IN}=shift;
    $self->{OUT}=shift;
    bless($self, $class);

    return $self;
}

sub run {
    my ($self, $arg) = @_;

    $self->{IN}->reader;
    $self->{OUT}->writer;
    
    while ($self->{IN}->more) 
    {
        my $msg = $self->{IN}->recv;
        if (defined $msg) {
            $self->{OUT}->send($msg);
        }
    }
    
    $self->{OUT}->close;
    $self->{IN}->close;
    
}

1;
