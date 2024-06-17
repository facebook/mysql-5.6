# Copyright (C) 2013 Monty Program Ab
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.


#################
# Goal: just crash the server, no other checks or activity is performed.
# This is a service reporter. It will be used
# in two-step flow, when we start RQG twice: first time to run the flow 
# and crash server, second time to run more flow and perform desired checks.
#################

package GenTest::Reporter::Crash;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;


my $first_reporter;

sub monitor {
	my $reporter = shift;

	# In case of two servers, we will be called twice.
	# Only kill the first server and ignore the second call.
	
	$first_reporter = $reporter if not defined $first_reporter;
	return STATUS_OK if $reporter ne $first_reporter;
	my $pid = $reporter->serverInfo('pid');
	if (!defined $pid) {
		say("ERROR: Server PID is not defined, cannot crash the server");
		return STATUS_ENVIRONMENT_FAILURE;
	} elsif (time() > $reporter->testEnd() - 19) {
		say("Sending SIGKILL to server with pid $pid...");
		kill(9, $pid);
		return STATUS_SERVER_KILLED;
	} else {
		return STATUS_OK;
	}
}

sub report {
	return STATUS_OK;
}

sub type {
	return REPORTER_TYPE_PERIODIC;
}

1;
