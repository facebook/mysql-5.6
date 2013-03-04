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

package GenTest::Reporter::Shutdown;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;
use Data::Dumper;
use IPC::Open2;
use IPC::Open3;

sub report {
	my $reporter = shift;

	my $primary_port = $reporter->serverVariable('port');

	foreach my $port ($primary_port + 4, $primary_port + 2, $primary_port) {
	        my $dsn = "dbi:mysql:host=127.0.0.1:port=".$port.":user=root";
	        my $dbh = DBI->connect($dsn, undef, undef, { PrintError => 0 } );

		my $pid;
		if ($port == $primary_port) {
			$pid = $reporter->serverInfo('pid');
		} elsif (defined $dbh) {
			my ($pid_file) = $dbh->selectrow_array('SELECT @@pid_file');
		        open (PF, $pid_file) or say("Unable to obtain pid: $!");
		        read (PF, $pid, -s $pid_file);
		        close (PF);
		        $pid =~ s{[\r\n]}{}sio;
		}

		if (defined $dbh) {
			say("Shutting down server on port $port via DBI...");
			$dbh->func('shutdown', 'admin');
		}

		if (defined $pid) {
			say("Shutting down server with pid $pid with SIGTERM...");
			kill(15, $pid);

			if (!osWindows()) {
				say("Waiting for mysqld with pid $pid to terminate...");
				foreach my $i (1..60) {
					if (! -e "/proc/$pid") {
						print "\n";
						last;
					}
					sleep(1);
					print "+";
				}
				say("... waiting complete. Just in case, killing server with pid $pid with SIGKILL ...");
				kill(9, $pid);
			}
		}
	}	
	return STATUS_OK;
}

sub type {
	return REPORTER_TYPE_ALWAYS;
}

1;
