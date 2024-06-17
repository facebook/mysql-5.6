# Copyright (c) 2011,2012 Oracle and/or its affiliates. All rights reserved.
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

package GenTest::Reporter::ErrorLogAlarm;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use GenTest;
use GenTest::Reporter;
use GenTest::Constants;

# Modify this to look for other patterns in the error log. 
# Note: do not modify $pattern to be defined using double quotes (") but leave as single quotes (') 
# as double quotes require a different escaping sequence for "[" (namely "\\[" it seems)
my $pattern = '^Error:|^ERROR|\[ERROR\]|allocated at line|missing DBUG_RETURN|^safe_mutex:|Invalid.*old.*table or database|InnoDB: Warning|InnoDB: Error:|InnoDB: Operating system error|Error while setting value|\[Warning\] Invalid';

# Modify this to filter out false positive patern matches (will improve over time)
my $reject_pattern = 'Lock wait timeout exceeded|Deadlock found when trying to get lock|innodb_log_block_size has been changed|Sort aborted:';

# Path to error log. Is assigned first time monitor() is called.
my $errorlog;

sub monitor {
    if (defined $ENV{RQG_CALLBACK}) {
	say "ErrorLogAlarm monitor not yet implemented for Callback environments";
	return STATUS_OK;
    }
    my $reporter = shift;

    if (not defined $errorlog) {
        $errorlog = $reporter->serverInfo('errorlog');
        if ($errorlog eq '') {
            # Error log was not found. Report the issue and continue.
            say("WARNING: Error log not found! ErrorLogAlarm Reporter does not work as intended!");
            return STATUS_OK;
        } else {
            # INFO
            say("ErrorLogAlarm Reporter will monitor the log file ".$errorlog);
        }
    }

    if ((-e $errorlog) && (-s $errorlog > 0)) {
        open(LOG, $errorlog);
        while(my $line = <LOG>) {
            # Case insensitive search required for (observed) programming 
            # incosistencies like "InnoDB: ERROR:" instead of "InnoDB: Error:"
            if(($line =~ m{$pattern}i) && ($line !~ m{$reject_pattern}i)) {
                say("ALARM from ErrorLogAlarm reporter: Pattern '$pattern' was".
                    " found in error log. Matching line was:");
                say($line);
                close LOG;
                return STATUS_ALARM;
            }
        }
        close LOG;

        ## Alternative, non-portable implementation:
        #my $grepresult = system('grep '.$pattern.' '.$errorlog.' > /dev/null 2>&1');
        #if ($grepresult == 0) {
        #    say("ALARM $pattern found in error log file.");
        #    return STATUS_ALARM;
        #}

    }
    return STATUS_OK;
}


sub report {
    my $reporter = shift;
    my $logfile = $reporter->serverInfo('errorlog');
    my $description = 
        'ErrorLogAlarm Reporter raised an alarm. Found pattern \''.$pattern.
        '\' in error log file '.$logfile;
    
    my $incident = GenTest::Incident->new(
        timestamp => isoTimestamp(),
        result    => 'fail',
        description => $description
        # TODO: Add matched line as signature?
    );

    return STATUS_OK, $incident;
}


sub type {
    return REPORTER_TYPE_PERIODIC | REPORTER_TYPE_ALWAYS;
}

1;
