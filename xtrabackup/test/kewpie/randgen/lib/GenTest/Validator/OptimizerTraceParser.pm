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

package GenTest::Validator::OptimizerTraceParser;

require Exporter;
@ISA = qw(GenTest::Validator GenTest);

use strict;

use GenTest;
use GenTest::Comparator;
use GenTest::Constants;
use GenTest::Result;
use GenTest::Validator;

use File::Basename;

################################################################################
# This validator retrieves the optimizer trace output for each query and
# feeds the JSON formatted tracing information to a JSON parser.
# If parsing fails, the validation fails (with a non-fatal status).
#
# Traces that are truncated due to missing bytes beyond max mem threshold are
# not attempted parsed, and will not cause test failure.
#
# This validator does not check that the contents of the traces are correct
# besides checking for valid JSON syntax.
#
# Prerequisites:
#   - optimzier tracing must be available and enabled
#   - a JSON parsing module must be available
#
# More on JSON: http://www.ietf.org/rfc/rfc4627.txt
#
################################################################################


# Some counters
my $valid_traces_count = 0;     # traces that were parsed successfully
my $missing_bytes_count = 0;    # traces that were truncated due to missing bytes (out of memory)
my $invalid_traces_count = 0;   # traces that failed to parse for unknown reason
my $no_traces_count = 0;        # statements with no generated optimizer trace
my $skipped_count = 0;          # statements for which we did not attempt to get trace

# Helper variables
my $have_json;      # a value of 1 means that JSON support is detected.
my $have_opt_trace; # a value of 1 means that optimizer_trace is available and enabled.
my $thisFile;       # the name of this validator

BEGIN {
    # Get the name of this validator (used for feedback to user).
    $thisFile = basename(__FILE__);
    $thisFile =~ s/\.pm//;  # remove .pm suffix from file name

    # Check if JSON parsing module is available.
    eval {
        require JSON;
        $have_json = 1;
    }
    

}

sub validate {
    my ($validator, $executors, $results) = @_;
    my $executor = $executors->[0];
    my $dbh = $executors->[0]->dbh();
    my $orig_result = $results->[0];
    my $orig_query = $orig_result->query();

    # Note that by default only the trace for the last executed statement will
    # be available. If we run any extra statements for some reason we need to 
    # take this into account when asking the server for the trace.
    my $extra_statements = 0; # Number of statements executed after the original.

    # We require JSON parser support to be available...
    if (!$have_json) {
        say("ERROR: $thisFile is unable to find JSON parser module, cannot continue.");
        return STATUS_ENVIRONMENT_FAILURE;
    }

    return STATUS_WONT_HANDLE if $orig_result->status() != STATUS_OK;

    # Check if optimizer_trace is enabled.
    # Save the result in a variable so we don't have to check it every time.
    if (not defined $have_opt_trace) {
        my $opt_trace_value = $dbh->selectrow_array('SELECT @@optimizer_trace');
        $extra_statements++;
        if (!($opt_trace_value =~ m{enabled=on})) {
            say('ERROR: Optimizer trace is disabled or not available. '.$thisFile.' validator cannot continue.');
            # Since tracing is per-session, we may want to just continue in this case (return STATUS_WONT_HANDLE).
            # However, we are returning a fatal error for now, to avoid accidentally thinking parsing was OK.
            #$have_opt_trace = 0;
            return STATUS_ENVIRONMENT_FAILURE;
        } else {
            $have_opt_trace = 1;
        }
    }

    # We need to retrieve the actual trace for the original query.
    #
    # We assume here that only the trace for the last executed query is 
    # available. As a work-around in case of any extra statements being executed
    # after the original as part of this validator, we re-execute the original
    # statement before continuing.
    # This should normally happen only the first time this subroutine is called.
    # If the statement is not a DML statement this is more problematic so we 
    # skip the query.
    if ($extra_statements > 0) {
        if ($orig_query !~ m{^\s*select}io) {
            $skipped_count++;
            return STATUS_WONT_HANDLE;
        }
        say("$thisFile re-executing one original statement in order to obtain correct trace.");
        $orig_result = $executor->execute($orig_query);
        if ($orig_result->status() != STATUS_OK) {
            say('ERROR: Re-execution of orignal query failed: '.$orig_result->errstr());
            say("\tFailed query was: ".$orig_query);
            return STATUS_ENVIRONMENT_FAILURE;
        }
    }

    my $trace_query = 'SELECT * FROM information_schema.OPTIMIZER_TRACE';
    my $trace_result = $executor->execute($trace_query);
    if (not defined $trace_result->data()) {
        $no_traces_count++;
        say("ERROR: $thisFile was unable to obtain optimizer trace for query $orig_query");
        #return STATUS_LENGTH_MISMATCH;
        return STATUS_TEST_FAILURE;
    }

    if ($trace_result->rows() != 1) {
        say("ERROR: Unexpected result from optimizer trace query ($thisFile): ".
            "Number of returned rows was ".$trace_result->rows().", expected 1.");
        return STATUS_ENVIRONMENT_FAILURE;
    }

    # Get the trace from the query result.
    # The result set is expected to have the following columns by default:
    #  QUERY | TRACE | MISSING_BYTES_BEYOND_MAX_MEM_SIZE
    #
    # If missing bytes, trace will not be valid JSON, it will be truncated.

    my ($query, $trace, $missing_bytes) = @{$trace_result->data()->[0]};
    
    # Filter out traces with missing bytes.
    if ($missing_bytes > 0) {
        $missing_bytes_count++;
        #say("$thisFile skipping validation of query due to missing $missing_bytes bytes from trace: $query");
        return STATUS_WONT_HANDLE;
    }

    # decode() croaks on error (e.g. invalid JSON text). 
    # Handling this through eval and subqsequent checking of $@.
    my $json_decoded = eval {
        JSON->new->decode($trace);
    };

    if ($@) {
        # There was a JSON parse failure.
        $invalid_traces_count++;
        say("ERROR: Parsing of optimizer trace failed with error: ".$@);
        say("\tTraced query was: ".$query);
        # Make sure the test does not return STATUS_OK if there is an unexpected parse failure.
        # Re-set $@ variable first, otherwise RQG will complain about Internal grammar error.
        $@ = '';
        return STATUS_CONTENT_MISMATCH;
    }

    $valid_traces_count++;
    return STATUS_OK;
}


sub DESTROY {
    say($thisFile.' statistics:');
    say("\tNumber of statments with valid JSON trace: ".$valid_traces_count);
    say("\tNumber of statments with trace missing bytes beyond max mem size: ".$missing_bytes_count);
    say("\tNumber of statments with invalid JSON trace for other reasons: ".$invalid_traces_count);
    say("\tNumber of statments with no JSON trace: ".$no_traces_count);
    say("\tNumber of skipped statments: ".$skipped_count);
}

1;
