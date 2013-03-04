# Copyright (c) 2008,2011 Oracle and/or its affiliates. All rights reserved.
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

package GenTest::Reporter::QueryTimeout;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use DBI;
use GenTest;
use GenTest::Constants;
use GenTest::Reporter;

use constant PROCESSLIST_CONNECTION_ID		=> 0;
use constant PROCESSLIST_PROCESS_TIME		=> 5;
use constant PROCESSLIST_PROCESS_STATE		=> 6;
use constant PROCESSLIST_PROCESS_INFO		=> 7;

# Default minimum lifetime for a query before it is killed
use constant DEFAULT_QUERY_LIFETIME_THRESHOLD	=> 20;	# Seconds

# The query lifetime threshold is configurable via properties.
# We check this once and store the value (or the default) in this variable.
my $q_l_t;

sub monitor {
	my $reporter = shift;

	my $dsn = $reporter->dsn();
	my $dbh = DBI->connect($dsn);

	if (defined GenTest::Executor::MySQL::errorType($DBI::err)) {
		return GenTest::Executor::MySQL::errorType($DBI::err);
	} elsif (not defined $dbh) {
		return STATUS_UNKNOWN_ERROR;
	}

	my $processlist = $dbh->selectall_arrayref("SHOW FULL PROCESSLIST");

	if (not defined $q_l_t) {
		# We only check the querytimeout option the first time the reporter runs
		$q_l_t = DEFAULT_QUERY_LIFETIME_THRESHOLD;
		$q_l_t = $reporter->properties->querytimeout 
			if defined $reporter->properties->querytimeout;
		say("QueryTimeout Reporter will use query timeout threshold of $q_l_t seconds");
	}

	foreach my $process (@$processlist) {
		if ($process->[PROCESSLIST_PROCESS_INFO] ne '') {
			if ($process->[PROCESSLIST_PROCESS_TIME] > $q_l_t + 100) {
				# Query survived QUERY_LIFETIME + 100 seconds.
				# If QUERY_LIFETIME_THRESHOLD is 20, and reporter interval is
				# 10 seconds, this means query survived more than 120 seconds and
				# 10 attempted KILL QUERY attempts. This looks like a mysqld issue.
				# Hence, we now try killing the whole thread instead.
				say("Query: ".$process->[PROCESSLIST_PROCESS_INFO]." took more than ".($q_l_t + 100). " seconds ($q_l_t + 100). Killing thread.");
				$dbh->do("KILL ".$process->[PROCESSLIST_CONNECTION_ID]);
			}elsif ($process->[PROCESSLIST_PROCESS_TIME] > $q_l_t) {
				say("Query: ".$process->[PROCESSLIST_PROCESS_INFO]." is taking more than ".($q_l_t). " seconds. Killing query.");
				$dbh->do("KILL QUERY ".$process->[PROCESSLIST_CONNECTION_ID]);
			}
		}
	}

	return STATUS_OK;
}

sub type {
	return REPORTER_TYPE_PERIODIC;
}

1;
