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

package GenTest::Validator::DrizzleErrorLogScan;

require Exporter;
@ISA = qw(GenTest::Validator);

use strict;
use GenTest;
use GenTest::Validator;
use GenTest::Constants;

my $error_log;

sub validate 
{
        my ($validator, $executors, $results) = @_;
	my $dbh = $executors->[0]->dbh();
        my @basedir= $dbh->selectrow_array('SELECT @@basedir');
        my $error_log = @basedir->[0].'/tests/var/log/master.err';
        say("$error_log");
        my $err_msg = "InnoDB: Error: unlock row could not find a 2 mode lock on the record" ;
        # my $err_msg = "lt-drizzled: Sort aborted" ;
	my $query = $results->[0]->query();

	# We check the log after each query and kill the server if we 
        # encounter the target err_msg
        open( ERR_LOG, $error_log) or die 'Could not open file:  ' .$!;
        my @list = grep /\b$err_msg\b/, <ERR_LOG>;
        say("@list->[0]");
        my $list_size = @list ;
        if ($list_size > 0)
        {
            #kill our server
            say("Message: $err_msg detected in $error_log.  Killing server");
            system("killall -9 lt-drizzled");
            say("Message: $err_msg detected after executing query $query");
            return STATUS_DATABASE_CORRUPTION ;
        }


	return STATUS_OK;
}

1;
