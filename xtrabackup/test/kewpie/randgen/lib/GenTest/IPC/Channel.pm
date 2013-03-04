# Copyright (C) 2010 Sun Microsystems, Inc. All rights reserved.
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

package GenTest::IPC::Channel;

@ISA = qw(GenTest);

use strict;

use Carp;

use IO::Handle;
use IO::Pipe;
use Data::Dumper;
use GenTest;

use constant CHANNEL_IN => 0;
use constant CHANNEL_OUT => 1;
use constant CHANNEL_PIPE => 2;
use constant CHANNEL_EOF => 3;
use constant CHANNEL_READER => 4;

sub new {
    my $class = shift;

    my $self = $class->SUPER::new({},@_);

    ## open  bi-directional pipe

    $self->[CHANNEL_IN] = IO::Handle->new();
    $self->[CHANNEL_OUT] = IO::Handle->new();
    $self->[CHANNEL_PIPE] = IO::Pipe->new($self->[CHANNEL_IN],$self->[CHANNEL_OUT]);
    
    $self->[CHANNEL_EOF]= 0;
    $self->[CHANNEL_READER] = undef;
    
    ## Turn off buffering of output. Each object is sent as one
    ## print-statement
    $self->[CHANNEL_OUT]->autoflush(1);

    return $self;
}

sub send {
    my ($self,$obj) = @_;

    croak "OUT pipe closed" if defined $self->[CHANNEL_READER] and $self->[CHANNEL_READER];

    ## Preliminary save Data::Dumper settings since this is a global setting
    my $oldindent = $Data::Dumper::Indent;
    my $oldpurity = $Data::Dumper::Purity;

    ## Make output with no newlines and suitable for eval
    $Data::Dumper::Indent = 0;
    $Data::Dumper::Purity = 1;

    my $msg = Dumper($obj);

    ## Encode newline because that is used as message separator
    ## (readline on the other end)
    $msg =~ s/\n/&NEWLINE;/g;

    my $chn = $self->[CHANNEL_OUT];
    print $chn $msg,"\n";

    ## Reset indent to old value
    $Data::Dumper::Indent = $oldindent;
    $Data::Dumper::Purity = $oldpurity;
}

sub recv {
    my ($self) = @_;
    my $obj;

    croak "IN pipe closed" if defined $self->[CHANNEL_READER] and !$self->[CHANNEL_READER];
    ## Read until eof or an object that may be evaluated is recieved
    while (!(defined $obj) and (!$self->[CHANNEL_EOF])) {
        my $line = readline $self->[CHANNEL_IN];

        ## Decode eol
        $line =~ s/&NEWLINE;/\n/g;

        ## Turn off strict vars since received message uses variables
        ## without "my"
        no strict "vars";

        ## Evaluate object
        $obj = eval $line;
        use strict "vars";
        $self->[CHANNEL_EOF] = eof $self->[CHANNEL_IN];
    };
    return $obj;
}

sub reader{
    my ($self) = @_;
    
    ## Readers don't need the output part
    close $self->[CHANNEL_OUT];
    $self->[CHANNEL_READER] = 1;
}

sub writer {
    my ($self) = @_;

    ## Writers don't need the input part
    close $self->[CHANNEL_IN];
    $self->[CHANNEL_READER] = 0;
}

sub close {
    my ($self) = @_;
    if (not defined $self->[CHANNEL_READER]) {
        close $self->[CHANNEL_OUT];
        close $self->[CHANNEL_IN];
    } elsif ($self->[CHANNEL_READER]) {
        close $self->[CHANNEL_IN];
    } else {
        close $self->[CHANNEL_OUT];
    }
}

sub more {
    my ($self) = @_;
    return not $self->[CHANNEL_EOF];
}

1;

