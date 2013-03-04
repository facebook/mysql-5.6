# Copyright (C) 2009 Sun Microsystems, Inc. All rights reserved.
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

package GenTest::Stack::Stack;

require Exporter;
@ISA = qw(GenTest);

use strict;
use GenTest;
use GenTest::Stack::StackFrame;
use Data::Dumper;

use constant FRAME_NO	=> 0;
use constant FRAMES	=> 1;

1;

sub new {
	my $class = shift;

	my $stack = $class->SUPER::new({}, @_);

	$stack->[FRAME_NO] = 0;

	return $stack;
}

sub _current {
    return $_[0]->[FRAME_NO];
}

sub push {
    my ($self) = @_;
    my $arg;
    if ($self->_current() > 0) {
	$arg = $self->get("arg") if defined $self->get("arg");
    }
    $self->[FRAME_NO]++;
    $self->[$self->[FRAME_NO]]=GenTest::Stack::StackFrame->new();
    $self->set("arg",$arg) if defined $arg;
    return undef;
}


sub set {
    my ($self, $name, $value) = @_;

    $self->[$self->_current()]->set($name,$value);
    return undef;
    
}

sub get {
    my ($self, $name, $value) = @_;
    
    return $self->[$self->_current()]->get($name);
    
}

sub pop {
    my ($self,$result) = @_;
    $self->[FRAME_NO]--;
    ## Place the result on the callers frame
    $self->set("result",$result) if $self->[FRAME_NO] > 0;
    return undef;
}

1;
