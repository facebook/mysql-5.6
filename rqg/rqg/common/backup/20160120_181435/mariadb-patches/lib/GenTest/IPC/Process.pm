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

package GenTest::IPC::Process;

@ISA = qw(GenTest);

use GenTest;
use if osWindows(), threads;

## A Process is a placeholder for an object run in a separate process.
## The contract assumes that the objects constructor is run in the
## parent process and the fork is done in Process->start and then
## obect->run() is invoked.

use Data::Dumper;

use strict;

my %processes;

use constant PROCESS_OBJECT => 0;
use constant PROCESS_PID => 1;

sub new {
    my $class = shift;

    return $class->SUPER::new({
        'object' => PROCESS_OBJECT},@_);
}


sub start {
    my ($self, @args) = @_;

    if (osWindows()) {
	my $thr = threads->create(sub{$self->[PROCESS_OBJECT]->run(@args)});
	$thr->detach();
	$self->[PROCESS_PID]=$thr->tid();
	$processes{$thr->tid()} = $self->[PROCESS_OBJECT];
	say "".(ref $self->[PROCESS_OBJECT])."(".$thr->tid().") started\n";
    } else {
	my $pid = fork();
	if ($pid == 0 ) {
	    ## Forked process
	    $self->[PROCESS_PID]=$$;
	    $self->[PROCESS_OBJECT]->run(@args);
	    say "".(ref $self->[PROCESS_OBJECT])."($$) terminated normally\n";
	    exit 0;
	} else {
	    say "".(ref $self->[PROCESS_OBJECT])."($pid) started\n";
	    $self->[PROCESS_PID] = $pid;
	    $processes{$pid} = $self->[PROCESS_OBJECT];
	    return $pid;
	}
    }
}


sub childWait {
    my (@list) = @_;
    if (@list < 1) {
        while (1) {
            my $pid = wait();
            last if $pid < 0;
            print "".(ref $processes{$pid})."($pid) stopped with status $?\n";
        }
    } else {
        my %pids;
        map {$pids{$_}=1} @list;
        while ((keys %pids) > 0) {
            my $pid = wait();
            last if $pid < 0;
            print "".(ref $processes{$pid})."($pid) stopped with status $?\n";
            delete $pids{$pid} if exists $pids{$pid};
        }
    }
}

sub childWaitStatus {
    my ($max, @list) = @_;
    my $status = 0;
    if (@list < 1) {
        while (1) {
            my $pid = wait();
            last if $pid < 0;
            $status = $? if $status < $?;
            print "".(ref $processes{$pid})."($pid) stopped with status $?\n";
            last if $status >= $max;
        }
    } else {
        my %pids;
        map {$pids{$_}=1} @list;
        while ((keys %pids) > 0) {
            my $pid = wait();
            last if $pid < 0;
            $status = $? if $status < $?;
            print "".(ref $processes{$pid})."($pid) stopped with status $?\n";
            delete $pids{$pid} if exists $pids{$pid};
            last if $status >= $max;
        }
    }
}

sub kill {
    my ($self) = @_;
    
    if (osWindows()) {
        ## Not sure yet, but the thread will enevtually die dtogether
        ## with the main program
    } else {
        say "Kill ".(ref $processes{$self->[PROCESS_PID]})."(".$self->[PROCESS_PID].")\n";
        kill(15, $self->[PROCESS_PID]);
    }
}

1;


