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

package GenTest::Reporter::ValgrindErrors;

require Exporter;
@ISA = qw(GenTest::Reporter);

use strict;
use File::Spec::Functions;
use GenTest;
use GenTest::Reporter;
use GenTest::Constants;
use IO::File;

# This reporter looks for valgrind messages in the server error log, and
# prints the messages and returns a failure status if any valgrind errors 
# are found.
#

sub report {

    my $reporter = shift;

    # Look for error log file
    my $error_log = $reporter->serverInfo('errorlog');
    $error_log = $reporter->serverVariable('log_error') if $error_log eq '';
    if ($error_log eq '') {
        foreach my $file ('../log/master.err', '../mysql.err') {
            my $filename = catfile($reporter->serverVariable('datadir'), $file);
            if (-f $filename) {
                $error_log = $filename;
                last;
            }
        }
    }

    # Open log file and read valgrind messages...
    my $LogFile = IO::File->new($error_log) 
        or say("ERROR: $0 could not read log file '$error_log': $!") 
            && return STATUS_ENVIRONMENT_FAILURE;

    # We use a set of strings to look for valgrind errors. These are based on 
    # the valgrind manual + code. We compose a regex of all the strings later.
    #
    # We also try to gather the number of reported errors. Note that not all
    # issues reported by valgrind are "errors".
    #
    # These strings need to be matched as case insensitive. 
    # Special characters will be escaped using quotemeta before regex matching.
    my @valgrind_strings = (
        'are definitely lost',
        'are possibly lost',
        'byte(s) found',
        'bytes inside a block of size',
        'conditional jump',
        'during client check request',
        'illegal memory pool address',
        'invalid free',
        'invalid read of size',
        'invalid write of size',
        'jump to the invalid address',
        'mismatched free',
        'on the next line',
        'source and destination',
        'syscall param',
        'unaddressable byte',
        'uninitialised byte',
        'uninitialised value',
    );
    
    # The regular expression is composed as follows:
    #   - sort: Sort strings in array @valgrind_strings by length in reversed order 
    #     (to ensure correct matches in case of similar substrings).
    #   - map{quotemeta}: Add escape characters to all non-letter, non-digit characters in the sorted strings, 
    #     to avoid special regex interpretation of these characters.
    #   - map("($_)"): Wrap each escaped sting inside a pair of parenthesis (for proper regex alternatives).
    #   - join: Separate each block of parenthesis by the "or" operator (|).
    #   - Add valgrind line prefix.
    my $regex = "^==[0-9]+==\s+.*".
        join('|', map("($_)", map{quotemeta} sort {length($b)<=>length($a)} (@valgrind_strings)));
    $regex = qr/($regex)/i;  # quote and compile regex, case insensitive
    my @valgrind_lines;
    my $errorcount = 0;
    my $issue_detected = 0;
    while (my $line = <$LogFile>) {
        push(@valgrind_lines, $line) if $line =~ m{^==[0-9]+==\s+\S};
        if ($line =~ m{^==[0-9]+==\s+ERROR SUMMARY: ([0-9]+) errors}) {
            $errorcount = $1;
        } elsif ($line =~ m{$regex}) {
            $issue_detected = 1;
        }
    }

    if (($errorcount > 0) or $issue_detected) {
        say("Valgrind: Issues detected (error count: $errorcount). Relevant messages from log file '$error_log':");
        foreach my $line (@valgrind_lines) {
            say($line);
        }
        return STATUS_VALGRIND_FAILURE
    } else {
        say("Valgrind: No issues found in file '$error_log'.");
        return STATUS_OK;
    }
}

sub type {
    return REPORTER_TYPE_ALWAYS ;
}

1;
