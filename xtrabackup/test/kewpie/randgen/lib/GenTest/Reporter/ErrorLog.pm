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

package GenTest::Reporter::ErrorLog;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use GenTest;
use GenTest::Reporter;
use GenTest::Constants;

sub report {

	my $reporter = shift;

	# master.err-old is created when logs are rotated due to SIGHUP

	my $main_log = $reporter->serverVariable('log_error');
    if ($main_log eq '') {
        foreach my $errlog ('../log/master.err', '../mysql.err') {
            if (-f $reporter->serverVariable('datadir').'/'.$errlog) {
                $main_log = $reporter->serverVariable('datadir').'/'.$errlog;
                last;
            }
        }
    }

	foreach my $log ( $main_log, $main_log.'-old' ) {
		if ((-e $log) && (-s $log > 0)) {
			say("The last 100 lines from $log :");
			system("tail -100 $log");
		}
	}
	
	return STATUS_OK;
}

sub type {
	return REPORTER_TYPE_CRASH | REPORTER_TYPE_DEADLOCK ;
}

1;
