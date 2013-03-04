# Copyright (c) 2011 Oracle and/or its affiliates. All rights reserved.
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

my $pattern = "^ERROR"; # Modify this to look for other patterns in the error log
my $errorlog;           # Path to error log. Is assigned first time monitor() is called.

sub monitor {

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
            if($line =~ m{$pattern}) {
                say("ALARM from ErrorLogAlarm reporter: Pattern '$pattern' was".
                    " found in error log. Matching line was:");
                print($line);
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
