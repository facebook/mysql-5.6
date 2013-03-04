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

package GenTest::ErrorFilter;

@ISA = qw(GenTest);

use GenTest;
use GenTest::IPC::Channel;

use strict;

use constant ERRORFILTER_CHANNEL => 0;
use constant ERRORFILTER_CACHE => 1;

sub new {
    my $class = shift;
    my $self = $class->SUPER::new({
        'channel' => ERRORFILTER_CHANNEL},@_);

    $self->[ERRORFILTER_CACHE] = {};

    return $self;
}

sub run {
    my ($self,@args) = @_;
    $self->[ERRORFILTER_CHANNEL]->reader;
    while (1) {
        my $msg = $self->[ERRORFILTER_CHANNEL]->recv;
        if (defined $msg) {
            my ($query, $err, $errstr) = @$msg;
            if (not defined $self->[ERRORFILTER_CACHE]->{$errstr}) {
                say("Query: $query failed: $err $errstr. Further errors of this kind will be suppressed.");
            }
            $self->[ERRORFILTER_CACHE]->{$errstr}++;
        }
        sleep 1 if !$self->[ERRORFILTER_CHANNEL]->more;
    }
    $self->[ERRORFILTER_CHANNEL]->close;
}

1;
