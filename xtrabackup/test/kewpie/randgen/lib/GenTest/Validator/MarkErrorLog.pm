# Copyright (c) 2008, 2010 Oracle and/or its affiliates, Inc. All
# rights reserved.  Use is subject to license terms.
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

package GenTest::Validator::MarkErrorLog;

require Exporter;
@ISA = qw(GenTest::Validator);

use strict;
use GenTest;
use GenTest::Validator;
use GenTest::Constants;

my $error_log;

sub validate {
    my ($validator, $executors, $results) = @_;
	my $dbh = $executors->[0]->dbh();

	if (not defined $error_log) {
		my ($foo, $error_log_mysql) = $dbh->selectrow_array("SHOW VARIABLES LIKE 'log_error'");

		if ($error_log_mysql ne '') {
			$error_log = $error_log_mysql;
		} else {
			my ($bar, $datadir_mysql) = $dbh->selectrow_array("SHOW VARIABLES LIKE 'datadir'");
            foreach my $errlog ('../log/master.err', '../mysql.err') {
                if (-f $datadir_mysql.'/'.$errlog) {
                    $error_log = $datadir_mysql.'/'.$errlog;
                    last;
                }
            }
            
		}
        say ("MarkErrorLog found errorlog at " . $error_log);
	}
	

    
	my $query = $results->[0]->query();

	open(LOG, ">>$error_log") or die "unable to open $error_log: $!";
	print LOG isoTimestamp()." [$$] Query: $query\n";
	close LOG;

	return STATUS_OK;
}

1;
