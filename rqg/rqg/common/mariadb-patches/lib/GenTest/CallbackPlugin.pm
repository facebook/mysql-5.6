# Copyright (c) 2012, Oracle and/or its affiliates. All rights
# reserved.
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

package GenTest::CallbackPlugin;

use strict;
use Carp;
use GenTest;

## This module is intended for plugins (Reporters, validators etc)
## which need to call back to the framework that started RQG to
## perform certain tasks. E.g. if the db server is running another
## place in the network, Bactrace.pm can't be performed by RQG itself
## and then have to call back to the framework to get the task
## performed.

## Usage:
##  if (defined $ENV{RQG_CALLBACK}) {
##      GenTest::CallbackPlugin("Something");
## } else {
##      do whatever in RQG
## }

## This assumes that it is (in the given framework that have set
## RQG_CALLBACK) that the command
##    $RQG_CALLBACK Something
## Will give some meaningful output

sub run {
    my ($argument) = @_;
    
    my $command = $ENV{RQG_CALLBACK} ." ". $argument;

    say("Running callback command $command");

    my $output = `$command`;
    
    return "$output";
}

1;
